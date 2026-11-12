// The workflow entry point is where every client converges, so it is where a
// reused task_id has to be refused. The gateway already checked it for its own
// callers; the adapters submit over ROS and never went through it.
#include "task_orchestrator_node.hpp"

#include "robot_task_interfaces/action/execute_task.hpp"
#include "robot_task_interfaces/action/execute_workflow.hpp"
#include "runtime_history/store.hpp"
#include "std_msgs/msg/bool.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

namespace
{
using namespace std::chrono_literals;
using ExecuteTask = robot_task_interfaces::action::ExecuteTask;
using ExecuteWorkflow = robot_task_interfaces::action::ExecuteWorkflow;

// A task server that answers immediately: these cases are about the workflow
// entry decision, not about what the child does.
class ImmediateTaskServer : public rclcpp::Node
{
public:
  explicit ImmediateTaskServer(const std::string & name_space)
  : Node("immediate_task_server", name_space)
  {
    server_ = rclcpp_action::create_server<ExecuteTask>(
      this, "execute_task",
      [](const rclcpp_action::GoalUUID &, const std::shared_ptr<const ExecuteTask::Goal>) {
        return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
      },
      [](const std::shared_ptr<rclcpp_action::ServerGoalHandle<ExecuteTask>>) {
        return rclcpp_action::CancelResponse::ACCEPT;
      },
      [](const std::shared_ptr<rclcpp_action::ServerGoalHandle<ExecuteTask>> handle) {
        auto result = std::make_shared<ExecuteTask::Result>();
        result->outcome = ExecuteTask::Result::COMPLETED;
        result->message = "completed";
        handle->succeed(result);
      });
  }

private:
  rclcpp_action::Server<ExecuteTask>::SharedPtr server_;
};

class EntryValidationTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    if (!rclcpp::ok()) {
      int argc = 0;
      rclcpp::init(argc, nullptr);
    }
  }

  void SetUp() override
  {
    database_ = (std::filesystem::temp_directory_path() /
      ("orchestrator_entry_" + std::to_string(::getpid()) + "_" +
      std::to_string(counter_++) + ".sqlite3")).string();
    std::filesystem::remove(database_);
    // Opened first so the file and its schema exist before the orchestrator's
    // read-only connection is pointed at it.
    store_ = std::make_unique<runtime_history::Store>(database_);
  }

  void TearDown() override
  {
    store_.reset();
    for (const char * suffix : {"", "-wal", "-shm"}) {
      std::filesystem::remove(database_ + suffix);
    }
  }

  static inline int counter_{0};
  std::string database_;
  std::unique_ptr<runtime_history::Store> store_;
};

ExecuteWorkflow::Goal workflow_goal(const std::string & task_id)
{
  ExecuteWorkflow::Goal goal;
  goal.request_id = "request-" + task_id;
  goal.workflow_id = "single_task";
  goal.task_id = task_id;
  goal.target_id = "dock_a";
  goal.allowed_duration.sec = 5;
  return goal;
}

// Sends one goal and returns the handle, which is null when it was rejected.
std::shared_ptr<rclcpp_action::ClientGoalHandle<ExecuteWorkflow>> send(
  const rclcpp_action::Client<ExecuteWorkflow>::SharedPtr & client,
  const ExecuteWorkflow::Goal & goal)
{
  auto future = client->async_send_goal(goal);
  if (future.wait_for(2s) != std::future_status::ready) {
    return nullptr;
  }
  return future.get();
}

TEST_F(EntryValidationTest, RefusesATaskIdThatIsAlreadyRecorded)
{
  runtime_history::TaskRecord record{
    "already-ran", "request-already-ran", "single_task", "goal-1", "dock_a", 1, 0, 0, 12,
    "done", 1};
  ASSERT_TRUE(store_->insert(record));

  const std::string name_space = "/entry_validation_recorded";
  auto server = std::make_shared<ImmediateTaskServer>(name_space);
  rclcpp::NodeOptions options;
  options.arguments({"--ros-args", "-r", "__ns:=" + name_space});
  options.parameter_overrides({rclcpp::Parameter("database_path", database_)});
  auto orchestrator = task_orchestrator::make_task_orchestrator_node(options);
  auto client_node = std::make_shared<rclcpp::Node>("entry_client", name_space);
  auto client = rclcpp_action::create_client<ExecuteWorkflow>(client_node, "execute_workflow");
  auto ready = client_node->create_publisher<std_msgs::msg::Bool>("/runtime/ready", 10);

  rclcpp::executors::MultiThreadedExecutor spin(rclcpp::ExecutorOptions(), 3);
  spin.add_node(server);
  spin.add_node(orchestrator);
  spin.add_node(client_node);
  std::thread spin_thread([&]() {spin.spin();});
  ASSERT_TRUE(client->wait_for_action_server(2s));
  std_msgs::msg::Bool ready_message;
  ready_message.data = true;
  for (int count = 0; count < 5; ++count) {
    ready->publish(ready_message);
    std::this_thread::sleep_for(10ms);
  }

  // The reused identity is refused...
  EXPECT_EQ(send(client, workflow_goal("already-ran")), nullptr);
  // ...and an unused one still goes through, so the check is not a blanket
  // rejection of everything after one recorded task.
  auto handle = send(client, workflow_goal("never-ran"));
  ASSERT_NE(handle, nullptr);
  auto result_future = client->async_get_result(handle);
  ASSERT_EQ(result_future.wait_for(3s), std::future_status::ready);
  EXPECT_EQ(result_future.get().code, rclcpp_action::ResultCode::SUCCEEDED);

  spin.cancel();
  spin_thread.join();
  spin.remove_node(client_node);
  spin.remove_node(orchestrator);
  spin.remove_node(server);
}

TEST_F(EntryValidationTest, RefusesAnEmptyTaskId)
{
  const std::string name_space = "/entry_validation_empty";
  auto server = std::make_shared<ImmediateTaskServer>(name_space);
  rclcpp::NodeOptions options;
  options.arguments({"--ros-args", "-r", "__ns:=" + name_space});
  options.parameter_overrides({rclcpp::Parameter("database_path", database_)});
  auto orchestrator = task_orchestrator::make_task_orchestrator_node(options);
  auto client_node = std::make_shared<rclcpp::Node>("entry_client_empty", name_space);
  auto client = rclcpp_action::create_client<ExecuteWorkflow>(client_node, "execute_workflow");

  rclcpp::executors::MultiThreadedExecutor spin(rclcpp::ExecutorOptions(), 3);
  spin.add_node(server);
  spin.add_node(orchestrator);
  spin.add_node(client_node);
  std::thread spin_thread([&]() {spin.spin();});
  ASSERT_TRUE(client->wait_for_action_server(2s));

  auto goal = workflow_goal("");
  goal.task_id.clear();
  EXPECT_EQ(send(client, goal), nullptr);

  spin.cancel();
  spin_thread.join();
  spin.remove_node(client_node);
  spin.remove_node(orchestrator);
  spin.remove_node(server);
}

TEST_F(EntryValidationTest, WithoutADatabaseTheCheckIsSkippedNotSilentlyPassed)
{
  // An orchestrator started without database_path keeps working; the warning it
  // logs is the record that this entry point is not checking. Pinned so the
  // default cannot quietly change into "opens an empty file and checks nothing".
  const std::string name_space = "/entry_validation_nodatabase";
  auto server = std::make_shared<ImmediateTaskServer>(name_space);
  rclcpp::NodeOptions options;
  options.arguments({"--ros-args", "-r", "__ns:=" + name_space});
  auto orchestrator = task_orchestrator::make_task_orchestrator_node(options);
  auto client_node = std::make_shared<rclcpp::Node>("entry_client_nodb", name_space);
  auto client = rclcpp_action::create_client<ExecuteWorkflow>(client_node, "execute_workflow");
  auto ready = client_node->create_publisher<std_msgs::msg::Bool>("/runtime/ready", 10);

  rclcpp::executors::MultiThreadedExecutor spin(rclcpp::ExecutorOptions(), 3);
  spin.add_node(server);
  spin.add_node(orchestrator);
  spin.add_node(client_node);
  std::thread spin_thread([&]() {spin.spin();});
  ASSERT_TRUE(client->wait_for_action_server(2s));
  std_msgs::msg::Bool ready_message;
  ready_message.data = true;
  for (int count = 0; count < 5; ++count) {
    ready->publish(ready_message);
    std::this_thread::sleep_for(10ms);
  }

  auto handle = send(client, workflow_goal("unchecked-task"));
  ASSERT_NE(handle, nullptr);
  auto result_future = client->async_get_result(handle);
  ASSERT_EQ(result_future.wait_for(3s), std::future_status::ready);

  spin.cancel();
  spin_thread.join();
  spin.remove_node(client_node);
  spin.remove_node(orchestrator);
  spin.remove_node(server);
}

}  // namespace
