#include <chrono>
#include <cmath>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "ament_index_cpp/get_package_share_directory.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "task_contract/action/execute_task.hpp"
#include "task_executor/target_map.hpp"
#include "task_guard/task_guard.hpp"

using namespace std::chrono_literals;

class ExecuteTaskServer : public rclcpp::Node {
 public:
  using ExecuteTask = task_contract::action::ExecuteTask;
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
    nav_client_ = rclcpp_action::create_client<NavigateToPose>(this, "navigate_to_pose");
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

  void abort_with(const std::shared_ptr<OuterHandle>& handle, task_contract::ErrorCode code,
                  const std::string& detail, std::uint32_t attempts = 0) {
    auto result = std::make_shared<ExecuteTask::Result>();
    result->final_state = ExecuteTask::Result::STATE_FAILED;
    result->error_code = static_cast<std::uint8_t>(code);
    result->detail = detail;
    result->attempts = attempts;
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
  }

  void execute(const std::shared_ptr<OuterHandle>& handle) {
    const auto started_at = std::chrono::steady_clock::now();
    const task_guard::RobotContext ready{true, true, false};
    const auto validation = guard_.validate(request(*handle->get_goal()), ready);
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
        auto result = std::make_shared<ExecuteTask::Result>();
        result->final_state = ExecuteTask::Result::STATE_CANCELLED;
        result->attempts = attempt - 1;
        result->detail = "task cancelled before the next navigation attempt";
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

      auto result_future = nav_client_->async_get_result(inner);
      while (result_future.wait_for(0ms) != std::future_status::ready) {
        if (handle->is_canceling()) {
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
          handle->canceled(result);
          return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= task_deadline) {
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
      handle->abort(result);
      return;
    }
  }

  task_executor::TargetMap targets_;
  task_guard::GuardPolicy policy_;
  task_guard::TaskGuard guard_;
  rclcpp_action::Client<NavigateToPose>::SharedPtr nav_client_;
  rclcpp_action::Server<ExecuteTask>::SharedPtr server_;
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ExecuteTaskServer>());
  rclcpp::shutdown();
  return 0;
}
