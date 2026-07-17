#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "ament_index_cpp/get_package_share_directory.hpp"
#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "diagnostic_msgs/msg/key_value.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "std_msgs/msg/bool.hpp"
#include "task_contract/action/execute_task.hpp"
#include "task_contract/msg/task_event.hpp"
#include "task_executor/target_map.hpp"
#include "task_guard/task_guard.hpp"
#include "tf2/time.hpp"
#include "tf2_ros/buffer.h"
#include "tf2_ros/transform_listener.h"

using namespace std::chrono_literals;

class ExecuteTaskServer : public rclcpp::Node {
 public:
  using ExecuteTask = task_contract::action::ExecuteTask;
  using TaskEvent = task_contract::msg::TaskEvent;
  using OuterHandle = rclcpp_action::ServerGoalHandle<ExecuteTask>;
  using NavigateToPose = nav2_msgs::action::NavigateToPose;
  using InnerHandle = rclcpp_action::ClientGoalHandle<NavigateToPose>;
  using InnerResultFuture = std::shared_future<InnerHandle::WrappedResult>;

  ExecuteTaskServer()
      : Node("execute_task_server"),
        targets_(task_executor::load_targets_from_yaml(
            ament_index_cpp::get_package_share_directory("task_executor") +
            "/config/targets.yaml")),
        policy_(task_guard::load_policy_from_yaml(
            ament_index_cpp::get_package_share_directory("task_guard") +
            "/config/task_policy.yaml")),
        guard_(policy_) {
    localization_check_enabled_ = declare_parameter<bool>("localization_check_enabled", true);
    require_device_ready_ = declare_parameter<bool>("require_device_ready", false);
    const auto device_timeout_ms = declare_parameter<int>("device_ready_timeout_ms", 1000);
    if (device_timeout_ms <= 0) {
      throw std::invalid_argument("device_ready_timeout_ms must be positive");
    }
    device_ready_timeout_ = std::chrono::milliseconds(device_timeout_ms);
    map_frame_ = declare_parameter<std::string>("map_frame", "map");
    base_frame_ = declare_parameter<std::string>("base_frame", "base_link");
    if (map_frame_.empty() || base_frame_.empty() || map_frame_ == base_frame_) {
      throw std::invalid_argument("map_frame and base_frame must be non-empty and different");
    }
    tf_buffer_ = std::make_unique<tf2_ros::Buffer>(get_clock());
    tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_, this, false);
    nav_client_ = rclcpp_action::create_client<NavigateToPose>(this, "navigate_to_pose");
    device_ready_subscription_ = create_subscription<std_msgs::msg::Bool>(
        "/device_ready", 10, [this](const std_msgs::msg::Bool& message) {
          device_ready_state_.store(message.data);
          const auto now = std::chrono::steady_clock::now().time_since_epoch();
          last_device_status_ns_.store(
              std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
        });
    diagnostics_publisher_ =
        create_publisher<diagnostic_msgs::msg::DiagnosticArray>("/diagnostics", 10);
    diagnostics_timer_ = create_wall_timer(1s, [this]() { publish_diagnostics(); });
    event_publisher_ = create_publisher<TaskEvent>(
        "task_events", rclcpp::QoS(rclcpp::KeepLast(50)).reliable().transient_local());
    server_ = rclcpp_action::create_server<ExecuteTask>(
        this, "execute_task",
        [](const rclcpp_action::GoalUUID&, std::shared_ptr<const ExecuteTask::Goal>) {
          return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
        },
        [](const std::shared_ptr<OuterHandle>) { return rclcpp_action::CancelResponse::ACCEPT; },
        [this](const std::shared_ptr<OuterHandle> handle) {
          std::thread(&ExecuteTaskServer::execute, this, handle).detach();
        });
  }

 private:
  class TaskReservation {
   public:
    explicit TaskReservation(std::atomic<bool>& active) : active_(active) {
      bool expected = false;
      acquired_ = active_.compare_exchange_strong(expected, true);
    }

    ~TaskReservation() {
      if (acquired_) {
        active_.store(false);
      }
    }

    TaskReservation(const TaskReservation&) = delete;
    TaskReservation& operator=(const TaskReservation&) = delete;

    bool acquired() const { return acquired_; }

   private:
    std::atomic<bool>& active_;
    bool acquired_{false};
  };

  bool localization_ready() const {
    return !localization_check_enabled_ ||
           tf_buffer_->canTransform(map_frame_, base_frame_, tf2::TimePointZero);
  }

  task_guard::RobotContext robot_context(bool task_active) const {
    return {localization_ready(), nav_client_->action_server_is_ready(), task_active,
            device_ready()};
  }

  bool device_ready() const {
    if (!require_device_ready_) {
      return true;
    }
    const auto last_status_ns = last_device_status_ns_.load();
    if (!device_ready_state_.load() || last_status_ns <= 0) {
      return false;
    }
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    const auto timeout_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(device_ready_timeout_).count();
    return now_ns >= last_status_ns && now_ns - last_status_ns <= timeout_ns;
  }

  static diagnostic_msgs::msg::KeyValue diagnostic_value(const std::string& key, bool value) {
    diagnostic_msgs::msg::KeyValue item;
    item.key = key;
    item.value = value ? "true" : "false";
    return item;
  }

  void publish_diagnostics() {
    const auto context = robot_context(task_active_.load());
    diagnostic_msgs::msg::DiagnosticStatus status;
    status.name = get_fully_qualified_name() + std::string("/readiness");
    status.hardware_id = "embodied_runtime";
    if (!context.localization_ready || !context.navigation_ready || !context.device_ready) {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::ERROR;
      if (!context.localization_ready) {
        status.message = "localization transform unavailable";
      } else if (!context.navigation_ready) {
        status.message = "NavigateToPose Action unavailable";
      } else {
        status.message = "device heartbeat unavailable or stale";
      }
    } else if (!localization_check_enabled_) {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
      status.message = "localization transform check disabled";
    } else {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
      status.message = context.task_active ? "ready; task active" : "ready; idle";
    }
    status.values.push_back(diagnostic_value("localization_ready", context.localization_ready));
    status.values.push_back(diagnostic_value("navigation_ready", context.navigation_ready));
    status.values.push_back(diagnostic_value("task_active", context.task_active));
    status.values.push_back(diagnostic_value("device_ready", context.device_ready));
    status.values.push_back(diagnostic_value("device_ready_required", require_device_ready_));
    diagnostic_msgs::msg::KeyValue frames;
    frames.key = "required_transform";
    frames.value = map_frame_ + " -> " + base_frame_;
    status.values.push_back(frames);

    diagnostic_msgs::msg::DiagnosticArray message;
    message.header.stamp = now();
    message.status.push_back(std::move(status));
    diagnostics_publisher_->publish(message);
  }

  task_contract::TaskRequest request(const ExecuteTask::Goal& goal) const {
    return {goal.contract_version, static_cast<task_contract::TaskAction>(goal.action),
            goal.task_id, goal.target, goal.deadline_s};
  }

  geometry_msgs::msg::PoseStamped pose_for(const std::string& target) {
    const auto& configured = targets_.at(target);
    geometry_msgs::msg::PoseStamped pose;
    pose.header.frame_id = configured.frame_id;
    pose.header.stamp = now();
    pose.pose.position.x = configured.x;
    pose.pose.position.y = configured.y;
    pose.pose.orientation.z = std::sin(configured.yaw / 2.0);
    pose.pose.orientation.w = std::cos(configured.yaw / 2.0);
    return pose;
  }

  void publish_event(const std::shared_ptr<OuterHandle>& handle, std::uint8_t state,
                     std::uint8_t error_code, std::uint32_t attempt, const std::string& detail) {
    TaskEvent event;
    event.stamp = now();
    event.task_id = handle->get_goal()->task_id;
    event.state = state;
    event.error_code = error_code;
    event.attempt = attempt;
    event.detail = detail;
    event_publisher_->publish(event);
  }

  void abort_with(const std::shared_ptr<OuterHandle>& handle, task_contract::ErrorCode code,
                  const std::string& detail, std::uint32_t attempts = 0) {
    auto result = std::make_shared<ExecuteTask::Result>();
    result->final_state = ExecuteTask::Result::STATE_FAILED;
    result->error_code = static_cast<std::uint8_t>(code);
    result->detail = detail;
    result->attempts = attempts;
    publish_event(handle, TaskEvent::STATE_FAILED, result->error_code, attempts, detail);
    handle->abort(result);
  }

  bool cancel_and_confirm(const InnerHandle::SharedPtr& inner, InnerResultFuture& result_future,
                          std::chrono::steady_clock::time_point deadline, std::string& detail) {
    auto cancel_future = nav_client_->async_cancel_goal(inner);
    if (cancel_future.wait_until(deadline) != std::future_status::ready) {
      detail = "navigation cancel request timed out";
      return false;
    }

    const auto cancel_response = cancel_future.get();
    if (!cancel_response || cancel_response->goals_canceling.empty()) {
      detail = "navigation cancel request was rejected";
      return false;
    }
    if (result_future.wait_until(deadline) != std::future_status::ready ||
        result_future.get().code != rclcpp_action::ResultCode::CANCELED) {
      detail = "navigation did not reach the canceled state";
      return false;
    }
    return true;
  }

  void publish_recovering(const std::shared_ptr<OuterHandle>& handle, std::uint32_t attempt) {
    auto feedback = std::make_shared<ExecuteTask::Feedback>();
    feedback->state = ExecuteTask::Feedback::STATE_RECOVERING;
    feedback->attempt = attempt;
    feedback->detail = "navigation failed; retrying within the original deadline";
    handle->publish_feedback(feedback);
    publish_event(handle, TaskEvent::STATE_RECOVERING, 0, attempt, feedback->detail);
  }

  void execute(const std::shared_ptr<OuterHandle>& handle) {
    const auto started_at = std::chrono::steady_clock::now();
    publish_event(handle, TaskEvent::STATE_VALIDATING, 0, 0,
                  "validating task contract, policy, and robot context");
    TaskReservation reservation(task_active_);
    const auto context = robot_context(!reservation.acquired());
    const auto validation = guard_.validate(request(*handle->get_goal()), context);
    if (!validation.accepted()) {
      abort_with(handle, validation.code, validation.detail);
      return;
    }

    const auto task_deadline = started_at + std::chrono::seconds(handle->get_goal()->deadline_s);
    const auto nav_wait_deadline = std::min(task_deadline, std::chrono::steady_clock::now() + 2s);
    const auto nav_wait = nav_wait_deadline - std::chrono::steady_clock::now();
    if (nav_wait <= std::chrono::steady_clock::duration::zero() ||
        !nav_client_->wait_for_action_server(nav_wait)) {
      if (std::chrono::steady_clock::now() >= task_deadline) {
        abort_with(handle, task_contract::ErrorCode::kTaskTimedOut,
                   "task deadline expired while waiting for navigation");
      } else {
        abort_with(handle, task_contract::ErrorCode::kNavigationNotReady,
                   "navigate_to_pose server is unavailable");
      }
      return;
    }

    NavigateToPose::Goal nav_goal;
    nav_goal.pose = pose_for(handle->get_goal()->target);
    const auto cancel_timeout = std::chrono::milliseconds(policy_.cancel_confirmation_timeout_ms);

    for (std::uint32_t attempt = 1; attempt <= policy_.max_navigation_attempts; ++attempt) {
      if (handle->is_canceling()) {
        publish_event(handle, TaskEvent::STATE_CANCELLING, 0, attempt - 1,
                      "task cancelled before the next navigation attempt");
        auto result = std::make_shared<ExecuteTask::Result>();
        result->final_state = ExecuteTask::Result::STATE_CANCELLED;
        result->attempts = attempt - 1;
        result->detail = "task cancelled before the next navigation attempt";
        publish_event(handle, TaskEvent::STATE_CANCELLED, 0, result->attempts, result->detail);
        handle->canceled(result);
        return;
      }
      if (std::chrono::steady_clock::now() >= task_deadline) {
        abort_with(handle, task_contract::ErrorCode::kTaskTimedOut,
                   "task deadline expired before the next navigation attempt", attempt - 1);
        return;
      }

      auto options = rclcpp_action::Client<NavigateToPose>::SendGoalOptions();
      const std::weak_ptr<OuterHandle> weak_handle = handle;
      options.feedback_callback =
          [weak_handle, attempt](InnerHandle::SharedPtr,
                                 const std::shared_ptr<const NavigateToPose::Feedback> feedback) {
            if (const auto outer = weak_handle.lock()) {
              auto out = std::make_shared<ExecuteTask::Feedback>();
              out->state = ExecuteTask::Feedback::STATE_RUNNING;
              out->attempt = attempt;
              out->distance_remaining = feedback->distance_remaining;
              out->detail = "navigation in progress";
              outer->publish_feedback(out);
            }
          };

      publish_event(handle, TaskEvent::STATE_DISPATCHING, 0, attempt,
                    "sending named target to navigate_to_pose");
      auto goal_future = nav_client_->async_send_goal(nav_goal, options);
      const auto goal_response_deadline =
          std::min(task_deadline, std::chrono::steady_clock::now() + 2s);
      if (goal_future.wait_until(goal_response_deadline) != std::future_status::ready) {
        if (std::chrono::steady_clock::now() >= task_deadline) {
          abort_with(handle, task_contract::ErrorCode::kTaskTimedOut,
                     "task deadline expired before navigation accepted the Goal", attempt - 1);
        } else {
          abort_with(handle, task_contract::ErrorCode::kNavRejected,
                     "navigation Goal response timed out", attempt - 1);
        }
        return;
      }
      const auto inner = goal_future.get();
      if (!inner) {
        abort_with(handle, task_contract::ErrorCode::kNavRejected, "navigation Goal was rejected",
                   attempt - 1);
        return;
      }

      publish_event(handle, TaskEvent::STATE_RUNNING, 0, attempt, "navigation Goal accepted");
      auto result_future = nav_client_->async_get_result(inner);
      while (result_future.wait_for(0ms) != std::future_status::ready) {
        if (handle->is_canceling()) {
          publish_event(handle, TaskEvent::STATE_CANCELLING, 0, attempt,
                        "propagating cancellation to navigation");
          std::string detail;
          if (!cancel_and_confirm(inner, result_future,
                                  std::chrono::steady_clock::now() + cancel_timeout, detail)) {
            abort_with(handle, task_contract::ErrorCode::kCancelUnconfirmed, detail, attempt);
            return;
          }

          auto result = std::make_shared<ExecuteTask::Result>();
          result->final_state = ExecuteTask::Result::STATE_CANCELLED;
          result->attempts = attempt;
          result->detail = "navigation cancellation confirmed";
          publish_event(handle, TaskEvent::STATE_CANCELLED, 0, attempt, result->detail);
          handle->canceled(result);
          return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= task_deadline) {
          publish_event(handle, TaskEvent::STATE_CANCELLING,
                        static_cast<std::uint8_t>(task_contract::ErrorCode::kTaskTimedOut), attempt,
                        "task deadline expired; cancelling navigation");
          std::string detail;
          if (!cancel_and_confirm(inner, result_future, task_deadline + cancel_timeout, detail)) {
            abort_with(handle, task_contract::ErrorCode::kCancelUnconfirmed,
                       "task deadline expired; " + detail, attempt);
          } else {
            abort_with(handle, task_contract::ErrorCode::kTaskTimedOut,
                       "task deadline expired; navigation cancellation confirmed", attempt);
          }
          return;
        }

        result_future.wait_until(std::min(task_deadline, now + 20ms));
      }

      const auto nav_result = result_future.get();
      if (nav_result.code == rclcpp_action::ResultCode::SUCCEEDED) {
        auto result = std::make_shared<ExecuteTask::Result>();
        result->final_state = ExecuteTask::Result::STATE_SUCCEEDED;
        result->attempts = attempt;
        result->detail = "navigation succeeded";
        publish_event(handle, TaskEvent::STATE_SUCCEEDED, 0, attempt, result->detail);
        handle->succeed(result);
        return;
      }

      if (attempt < policy_.max_navigation_attempts) {
        publish_recovering(handle, attempt);
        continue;
      }

      auto result = std::make_shared<ExecuteTask::Result>();
      result->final_state = ExecuteTask::Result::STATE_SAFE_STOP;
      result->error_code = static_cast<std::uint8_t>(task_contract::ErrorCode::kRecoveryExhausted);
      result->attempts = attempt;
      result->detail = "navigation recovery attempts exhausted";
      publish_event(handle, TaskEvent::STATE_SAFE_STOP, result->error_code, attempt,
                    result->detail);
      handle->abort(result);
      return;
    }
  }

  task_executor::TargetMap targets_;
  task_guard::GuardPolicy policy_;
  task_guard::TaskGuard guard_;
  bool localization_check_enabled_{true};
  bool require_device_ready_{false};
  std::chrono::milliseconds device_ready_timeout_{1000};
  std::string map_frame_;
  std::string base_frame_;
  std::atomic<bool> task_active_{false};
  std::atomic<bool> device_ready_state_{false};
  std::atomic<std::int64_t> last_device_status_ns_{0};
  std::unique_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
  rclcpp_action::Client<NavigateToPose>::SharedPtr nav_client_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr device_ready_subscription_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diagnostics_publisher_;
  rclcpp::TimerBase::SharedPtr diagnostics_timer_;
  rclcpp::Publisher<TaskEvent>::SharedPtr event_publisher_;
  rclcpp_action::Server<ExecuteTask>::SharedPtr server_;
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ExecuteTaskServer>());
  rclcpp::shutdown();
  return 0;
}
