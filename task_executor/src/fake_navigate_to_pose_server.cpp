#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

using namespace std::chrono_literals;

class FakeNavigateToPoseServer : public rclcpp::Node {
 public:
  using Action = nav2_msgs::action::NavigateToPose;
  using GoalHandle = rclcpp_action::ServerGoalHandle<Action>;

  FakeNavigateToPoseServer() : Node("fake_navigate_to_pose_server") {
    const auto feedback_delay_ms = declare_parameter<int>("feedback_delay_ms", 50);
    if (feedback_delay_ms <= 0) {
      throw std::invalid_argument("feedback_delay_ms must be greater than zero");
    }
    abort_first_n_goals_ = declare_parameter<int>("abort_first_n_goals", 0);
    if (abort_first_n_goals_ < 0) {
      throw std::invalid_argument("abort_first_n_goals must not be negative");
    }
    feedback_delay_ = std::chrono::milliseconds(feedback_delay_ms);
    server_ = rclcpp_action::create_server<Action>(
        this, "navigate_to_pose",
        [](const rclcpp_action::GoalUUID&, std::shared_ptr<const Action::Goal>) {
          return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
        },
        [](const std::shared_ptr<GoalHandle>) { return rclcpp_action::CancelResponse::ACCEPT; },
        [this](const std::shared_ptr<GoalHandle> goal_handle) {
          const auto goal_number = ++goal_count_;
          std::thread(&FakeNavigateToPoseServer::execute, this, goal_handle,
                      goal_number <= abort_first_n_goals_)
              .detach();
        });
  }

 private:
  bool finish_cancel_if_requested(const std::shared_ptr<GoalHandle>& goal_handle) {
    if (!goal_handle->is_canceling()) {
      return false;
    }
    auto result = std::make_shared<Action::Result>();
    result->error_msg = "cancelled by fake server";
    goal_handle->canceled(result);
    return true;
  }

  void execute(const std::shared_ptr<GoalHandle> goal_handle, bool abort_goal) {
    std::this_thread::sleep_for(100ms);
    for (float distance : {3.0F, 2.0F, 1.0F}) {
      if (finish_cancel_if_requested(goal_handle)) {
        return;
      }
      auto feedback = std::make_shared<Action::Feedback>();
      feedback->distance_remaining = distance;
      goal_handle->publish_feedback(feedback);
      std::this_thread::sleep_for(feedback_delay_);
    }
    if (finish_cancel_if_requested(goal_handle)) {
      return;
    }
    auto result = std::make_shared<Action::Result>();
    if (abort_goal) {
      result->error_msg = "aborted by fake server";
      goal_handle->abort(result);
    } else {
      goal_handle->succeed(result);
    }
  }

  std::atomic<int> goal_count_{0};
  int abort_first_n_goals_{0};
  std::chrono::milliseconds feedback_delay_{50};
  rclcpp_action::Server<Action>::SharedPtr server_;
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FakeNavigateToPoseServer>());
  rclcpp::shutdown();
  return 0;
}
