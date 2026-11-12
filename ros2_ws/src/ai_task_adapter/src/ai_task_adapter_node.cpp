#include "ai_task_adapter/natural_language_planner.hpp"

#include "robot_task_interfaces/action/execute_workflow.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

// Submits through ExecuteWorkflow, not ExecuteTask. Everything that must hold
// for every task -- the workflow allowlist, the ready precondition, the reused
// task_id check and the fault latch -- lives above this adapter, and reaching
// past it to ExecuteTask skipped all of it. README says adapters submit only
// allowlisted workflows; this is what makes that true for the rule-based one.
class AiTaskAdapterNode : public rclcpp::Node
{
public:
  using ExecuteWorkflow = robot_task_interfaces::action::ExecuteWorkflow;
  using GoalHandle = rclcpp_action::ClientGoalHandle<ExecuteWorkflow>;

  AiTaskAdapterNode()
  : Node("ai_task_adapter")
  {
    request_ = declare_parameter<std::string>("request", "go to dock_a");
    task_id_ = declare_parameter<std::string>("task_id", "ai_demo_1");
    request_id_ = declare_parameter<std::string>("request_id", "");
    workflow_id_ = declare_parameter<std::string>("workflow_id", "single_task");
    allowed_duration_ms_ = declare_parameter<std::int64_t>("allowed_duration_ms", 1000);
    cancel_after_ms_ = declare_parameter<std::int64_t>("cancel_after_ms", -1);
    const auto targets = declare_parameter<std::vector<std::string>>(
      "targets", {"dock_a", "home"});
    planner_ = std::make_unique<ai_task_adapter::NaturalLanguagePlanner>(targets);
    action_client_ = rclcpp_action::create_client<ExecuteWorkflow>(this, "execute_workflow");
  }

  bool run()
  {
    using namespace std::chrono_literals;
    if (task_id_.empty() || allowed_duration_ms_ <= 0 ||
      allowed_duration_ms_ >
      static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) * 1000)
    {
      RCLCPP_ERROR(get_logger(), "Invalid task_id or allowed_duration_ms");
      exit_code_ = 2;
      return false;
    }

    builtin_interfaces::msg::Duration duration;
    duration.sec = static_cast<std::int32_t>(allowed_duration_ms_ / 1000);
    duration.nanosec = static_cast<std::uint32_t>(
      (allowed_duration_ms_ % 1000) * 1000000);

    ai_task_adapter::PlanError plan_error;
    const auto plan = planner_->plan(request_, duration, task_id_, plan_error);
    if (!plan) {
      RCLCPP_ERROR(
        get_logger(), "Plan rejected error=%s code=%u request=%s",
        ai_task_adapter::to_string(plan_error).data(),
        ai_task_adapter::error_code(plan_error), request_.c_str());
      exit_code_ = 2;
      return false;
    }

    // The workflow's request identity defaults to its task identity only when
    // the caller supplied none: two invocations with the same task_id are the
    // same request and must be deduplicated as such, not look like two.
    const std::string request_id = request_id_.empty() ? plan->task_id : request_id_;

    RCLCPP_INFO(
      get_logger(),
      "Structured plan request_id=%s workflow_id=%s task_id=%s target_id=%s duration_ms=%ld",
      request_id.c_str(), workflow_id_.c_str(), plan->task_id.c_str(),
      plan->target_id.c_str(), allowed_duration_ms_);
    if (!action_client_->wait_for_action_server(5s)) {
      RCLCPP_ERROR(get_logger(), "ExecuteWorkflow server not available after 5 seconds");
      exit_code_ = 1;
      return false;
    }

    ExecuteWorkflow::Goal goal;
    goal.request_id = request_id;
    goal.workflow_id = workflow_id_;
    goal.task_id = plan->task_id;
    goal.target_id = plan->target_id;
    goal.allowed_duration = plan->allowed_duration;

    rclcpp_action::Client<ExecuteWorkflow>::SendGoalOptions options;
    options.goal_response_callback = std::bind(
      &AiTaskAdapterNode::goal_response_callback, this, std::placeholders::_1);
    options.feedback_callback = std::bind(
      &AiTaskAdapterNode::feedback_callback, this, std::placeholders::_1,
      std::placeholders::_2);
    options.result_callback = std::bind(
      &AiTaskAdapterNode::result_callback, this, std::placeholders::_1);
    action_client_->async_send_goal(goal, options);
    return true;
  }

  int exit_code() const
  {
    return exit_code_;
  }

private:
  void goal_response_callback(const GoalHandle::SharedPtr & goal_handle)
  {
    if (!goal_handle) {
      // A rejection is a decision, not a transport failure: the workflow entry
      // point refused the identity, the allowlist or the ready state.
      RCLCPP_WARN(get_logger(), "ExecuteWorkflow goal rejected");
      exit_code_ = 2;
      rclcpp::shutdown();
      return;
    }
    RCLCPP_INFO(get_logger(), "ExecuteWorkflow goal accepted");
    goal_handle_ = goal_handle;
    if (cancel_after_ms_ >= 0) {
      cancel_timer_ = create_wall_timer(
        std::chrono::milliseconds(cancel_after_ms_),
        [this]() {
          cancel_timer_->cancel();
          RCLCPP_INFO(get_logger(), "Sending ExecuteWorkflow cancel request");
          action_client_->async_cancel_goal(goal_handle_);
        });
    }
  }

  void feedback_callback(
    GoalHandle::SharedPtr,
    const std::shared_ptr<const ExecuteWorkflow::Feedback> feedback)
  {
    RCLCPP_INFO(
      get_logger(), "ExecuteWorkflow feedback node=%s progress=%.2f",
      feedback->active_node.c_str(), feedback->progress);
  }

  void result_callback(const GoalHandle::WrappedResult & wrapped_result)
  {
    if (cancel_timer_) {
      cancel_timer_->cancel();
    }
    switch (wrapped_result.code) {
      case rclcpp_action::ResultCode::SUCCEEDED:
        exit_code_ = 0;
        break;
      case rclcpp_action::ResultCode::CANCELED:
        exit_code_ = 3;
        break;
      case rclcpp_action::ResultCode::ABORTED:
        exit_code_ = 4;
        break;
      default:
        exit_code_ = 5;
        break;
    }

    if (wrapped_result.result) {
      RCLCPP_INFO(
        get_logger(), "ExecuteWorkflow result code=%d outcome=%u error_code=%u message=%s",
        static_cast<int>(wrapped_result.code), wrapped_result.result->outcome,
        wrapped_result.result->error_code, wrapped_result.result->message.c_str());
    } else {
      RCLCPP_ERROR(get_logger(), "ExecuteWorkflow returned no result");
    }
    rclcpp::shutdown();
  }

  std::string request_;
  std::string request_id_;
  std::string task_id_;
  std::string workflow_id_;
  std::int64_t allowed_duration_ms_;
  std::int64_t cancel_after_ms_;
  int exit_code_{5};
  std::unique_ptr<ai_task_adapter::NaturalLanguagePlanner> planner_;
  rclcpp_action::Client<ExecuteWorkflow>::SharedPtr action_client_;
  GoalHandle::SharedPtr goal_handle_;
  rclcpp::TimerBase::SharedPtr cancel_timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<AiTaskAdapterNode>();
  if (!node->run()) {
    rclcpp::shutdown();
    return node->exit_code();
  }
  rclcpp::spin(node);
  return node->exit_code();
}
