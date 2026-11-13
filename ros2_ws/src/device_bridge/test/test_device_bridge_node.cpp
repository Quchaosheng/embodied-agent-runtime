#include "runtime_can/in_memory_transport.hpp"
#include "runtime_can/protocol.hpp"

#include "device_bridge/device_bridge_node.hpp"

#include "robot_task_interfaces/msg/device_state.hpp"

#include <gtest/gtest.h>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include <rclcpp/executors/multi_threaded_executor.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using ExecuteDeviceCommand = robot_task_interfaces::action::ExecuteDeviceCommand;

namespace
{

// Everything the node actually does used to be covered only by the vcan E2E,
// which needs a live interface and the whole stack. These tests drive the same
// code through an injected transport, so they run in colcon test and CI.
class DeviceBridgeNodeTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite() {rclcpp::init(0, nullptr);}
  static void TearDownTestSuite() {rclcpp::shutdown();}

  void SetUp() override
  {
    transport_ = std::make_shared<runtime_can::InMemoryTransport>();
    node_ = std::make_shared<DeviceBridgeNode>(transport_, options());
    client_node_ = std::make_shared<rclcpp::Node>("bridge_test_client_" +
        std::to_string(counter_++));
    client_ = rclcpp_action::create_client<ExecuteDeviceCommand>(
      client_node_, "execute_device_command");
    // An Action server that nothing spins never answers a goal, so the executor
    // runs beside the test for the whole case.
    executor_ = std::make_shared<rclcpp::executors::MultiThreadedExecutor>();
    executor_->add_node(node_);
    executor_->add_node(client_node_);
    spin_thread_ = std::thread([this]() {executor_->spin();});
    ASSERT_TRUE(client_->wait_for_action_server(5s));
  }

  void TearDown() override
  {
    executor_->cancel();
    if (spin_thread_.joinable()) {
      spin_thread_.join();
    }
    executor_.reset();
    node_.reset();
    client_.reset();
    client_node_.reset();
    transport_.reset();
  }

  static rclcpp::NodeOptions options()
  {
    // The bridge's own wait bounds are shortened so a test never depends on the
    // production defaults, which are seconds long.
    return rclcpp::NodeOptions()
           .parameter_overrides({
        rclcpp::Parameter("max_ack_timeout_ms", 500),
        rclcpp::Parameter("stop_timeout_ms", 200),
        rclcpp::Parameter("receive_poll_ms", 5),
        rclcpp::Parameter("completion_timeout_ms", 500),
        rclcpp::Parameter("diagnostic_period_ms", 50),
           });
  }

  // Mirrors the executor's goal: one command with a bounded ACK wait.
  std::shared_ptr<ExecuteDeviceCommand::Goal> make_goal(
    const std::uint32_t command_id = 5, const std::int32_t argument = 42)
  {
    auto goal = std::make_shared<ExecuteDeviceCommand::Goal>();
    goal->command_id = command_id;
    goal->opcode = 1;
    goal->argument = argument;
    goal->ack_timeout.sec = 0;
    goal->ack_timeout.nanosec = 100000000;  // 100 ms
    return goal;
  }

  void answer_ack(const std::uint8_t result_code = 0)
  {
    transport_->set_responder(
      [this, result_code](const runtime_can::RawFrame & frame) {
        runtime_can::DecodeError decode_error;
        const auto command = runtime_can::decode_command(frame, decode_error);
        if (!command) {
          return;
        }
        transport_->inject(runtime_can::encode_response(
            runtime_can::Response{command->command_id, result_code, 1, {0, 0, 0},
            command->session}));
      });
  }

  std::vector<runtime_can::Command> sent_commands() const
  {
    std::vector<runtime_can::Command> commands;
    for (const auto & frame : transport_->sent_frames()) {
      runtime_can::DecodeError decode_error;
      const auto decoded = runtime_can::decode_command(frame, decode_error);
      if (decoded) {
        commands.push_back(*decoded);
      }
    }
    return commands;
  }

  std::shared_ptr<runtime_can::InMemoryTransport> transport_;
  std::shared_ptr<DeviceBridgeNode> node_;
  rclcpp::Node::SharedPtr client_node_;
  rclcpp_action::Client<ExecuteDeviceCommand>::SharedPtr client_;
  std::shared_ptr<rclcpp::executors::MultiThreadedExecutor> executor_;
  std::thread spin_thread_;
  static std::atomic<int> counter_;
};

std::atomic<int> DeviceBridgeNodeTest::counter_{0};

// Reports the actual outcome on timeout instead of failing inside a helper that
// then has to return an invalid value.
rclcpp_action::ClientGoalHandle<ExecuteDeviceCommand>::WrappedResult wait_result(
  const rclcpp_action::Client<ExecuteDeviceCommand>::SharedPtr & client,
  const std::shared_ptr<rclcpp_action::ClientGoalHandle<ExecuteDeviceCommand>> & handle,
  const std::chrono::seconds timeout = 10s)
{
  auto future = client->async_get_result(handle);
  EXPECT_EQ(future.wait_for(timeout), std::future_status::ready)
    << "the bridge did not settle the goal within " << timeout.count() << " s";
  return future.get();
}

}

TEST_F(DeviceBridgeNodeTest, AckedCommandCompletes)
{
  answer_ack();
  auto goal_handle = client_->async_send_goal(*make_goal());
  ASSERT_TRUE(goal_handle.wait_for(5s) == std::future_status::ready);

  if (goal_handle.wait_for(5s) != std::future_status::ready) {
    FAIL() << "the bridge never answered the goal request";
  }
  const auto result = wait_result(client_, goal_handle.get());
  EXPECT_EQ(result.code, rclcpp_action::ResultCode::SUCCEEDED);
  EXPECT_EQ(result.result->outcome, ExecuteDeviceCommand::Result::COMPLETED);
  EXPECT_EQ(result.result->error_code, 0);

  const auto commands = sent_commands();
  ASSERT_EQ(commands.size(), 1u);
  EXPECT_EQ(commands.front().command_id, 5);
  EXPECT_EQ(commands.front().argument, 42);
}

TEST_F(DeviceBridgeNodeTest, MissingAckTimesOutAndSendsAStop)
{
  // A command that is never acknowledged must end in a reported SAFE_STOP, and
  // the STOP has to reach the wire: that is the whole point of the bridge.
  auto goal_handle = client_->async_send_goal(*make_goal());
  ASSERT_TRUE(goal_handle.wait_for(5s) == std::future_status::ready);

  if (goal_handle.wait_for(5s) != std::future_status::ready) {
    FAIL() << "the bridge never answered the goal request";
  }
  const auto result = wait_result(client_, goal_handle.get());
  EXPECT_EQ(result.code, rclcpp_action::ResultCode::ABORTED);
  EXPECT_EQ(result.result->outcome, ExecuteDeviceCommand::Result::SAFE_STOP);

  bool saw_stop = false;
  for (const auto & command : sent_commands()) {
    if (command.opcode == runtime_can::kStopOpcode) {
      saw_stop = true;
      EXPECT_EQ(command.command_id >= runtime_can::kStopCommandIdMin, true);
    }
  }
  EXPECT_TRUE(saw_stop) << "no STOP reached the wire after an ACK timeout";
}

TEST_F(DeviceBridgeNodeTest, DeviceRejectionIsReportedAsADeviceFault)
{
  answer_ack(7);
  auto goal_handle = client_->async_send_goal(*make_goal());
  ASSERT_TRUE(goal_handle.wait_for(5s) == std::future_status::ready);

  if (goal_handle.wait_for(5s) != std::future_status::ready) {
    FAIL() << "the bridge never answered the goal request";
  }
  const auto result = wait_result(client_, goal_handle.get());
  EXPECT_EQ(result.code, rclcpp_action::ResultCode::ABORTED);
  EXPECT_EQ(result.result->outcome, ExecuteDeviceCommand::Result::DEVICE_FAULT);
  EXPECT_EQ(
    result.result->error_code,
    device_bridge::device_error_code(7));
}

TEST_F(DeviceBridgeNodeTest, CompletionProtocolWaitsForTheStatusFrame)
{
  // The device ACKs, then reports it is executing, then that it finished: a
  // status arrival must settle the goal, not the ACK.
  const auto transport = transport_;
  transport->set_responder(
    [transport](const runtime_can::RawFrame & frame) {
      runtime_can::DecodeError decode_error;
      const auto command = runtime_can::decode_command(frame, decode_error);
      if (!command) {
        return;
      }
      transport->inject(runtime_can::encode_response(
          runtime_can::Response{command->command_id, 0, 1, {0, 0, 0},
          command->session}));
      runtime_can::StatusFrame executing;
      executing.command_id = command->command_id;
      executing.session = command->session;
      executing.completion = runtime_can::Completion::kExecuting;
      transport->inject(runtime_can::encode_status(executing));
      runtime_can::StatusFrame completed;
      completed.command_id = command->command_id;
      completed.session = command->session;
      completed.completion = runtime_can::Completion::kCompleted;
      transport->inject(runtime_can::encode_status(completed));
    });

  // Completion reporting is opt-in: a protocol that only ACKs would otherwise be
  // waited on until the completion bound.
  node_->set_parameters({rclcpp::Parameter("completion_states_enabled", true)});
  auto goal_handle = client_->async_send_goal(*make_goal());
  if (goal_handle.wait_for(5s) != std::future_status::ready) {
    FAIL() << "the bridge never answered the goal request";
  }
  const auto result = wait_result(client_, goal_handle.get());
  EXPECT_EQ(result.code, rclcpp_action::ResultCode::SUCCEEDED);
  EXPECT_EQ(result.result->outcome, ExecuteDeviceCommand::Result::COMPLETED);
}

TEST_F(DeviceBridgeNodeTest, CancelAfterAckSendsAStopAndReportsCanceled)
{
  const auto transport = transport_;
  std::atomic_bool cancel_sent{false};
  transport->set_responder(
    [transport, &cancel_sent](const runtime_can::RawFrame & frame) {
      runtime_can::DecodeError decode_error;
      const auto command = runtime_can::decode_command(frame, decode_error);
      if (!command) {
        return;
      }
      if (command->opcode == runtime_can::kStopOpcode) {
        // A STOP only counts as acknowledged when the device also reports it
        // stopped; answering NORMAL is a refusal.
        transport->inject(runtime_can::encode_response(runtime_can::Response{
        command->command_id, 0,
        robot_task_interfaces::msg::DeviceState::STOPPED, {0, 0, 0},
        command->session}));
        return;
      }
      // The ACK never arrives; the caller cancels while the bridge is waiting.
      (void)cancel_sent;
    });

  auto goal_handle = client_->async_send_goal(*make_goal());
  ASSERT_TRUE(goal_handle.wait_for(5s) == std::future_status::ready);
  std::this_thread::sleep_for(50ms);
  auto cancel_response = client_->async_cancel_goal(goal_handle.get());
  ASSERT_TRUE(cancel_response.wait_for(5s) == std::future_status::ready);

  if (goal_handle.wait_for(5s) != std::future_status::ready) {
    FAIL() << "the bridge never answered the goal request";
  }
  const auto result = wait_result(client_, goal_handle.get());
  EXPECT_EQ(result.code, rclcpp_action::ResultCode::CANCELED);
  EXPECT_EQ(result.result->outcome, ExecuteDeviceCommand::Result::CANCELED);

  bool saw_stop = false;
  for (const auto & command : sent_commands()) {
    saw_stop = saw_stop || command.opcode == runtime_can::kStopOpcode;
  }
  EXPECT_TRUE(saw_stop) << "a cancel after the command reached the wire must STOP";
}

TEST_F(DeviceBridgeNodeTest, TransportErrorIsReportedWithoutHanging)
{
  transport_->fail_send("interface is down");
  auto goal_handle = client_->async_send_goal(*make_goal());
  ASSERT_TRUE(goal_handle.wait_for(5s) == std::future_status::ready);

  if (goal_handle.wait_for(5s) != std::future_status::ready) {
    FAIL() << "the bridge never answered the goal request";
  }
  const auto result = wait_result(client_, goal_handle.get());
  EXPECT_EQ(result.code, rclcpp_action::ResultCode::ABORTED);
  EXPECT_EQ(result.result->outcome, ExecuteDeviceCommand::Result::SAFE_STOP);
  EXPECT_EQ(result.result->error_code, device_bridge::kErrorTransport);
}

TEST_F(DeviceBridgeNodeTest, StopRejectedByTheDeviceIsReportedAsSafeStop)
{
  // The device answers the STOP with a non-zero result and a non-STOPPED mode:
  // the caller has to learn that the device refused, not that it stopped.
  const auto transport = transport_;
  transport->set_responder(
    [transport](const runtime_can::RawFrame & frame) {
      runtime_can::DecodeError decode_error;
      const auto command = runtime_can::decode_command(frame, decode_error);
      if (!command) {
        return;
      }
      if (command->opcode == runtime_can::kStopOpcode) {
        transport->inject(runtime_can::encode_response(
            runtime_can::Response{command->command_id, 9, 1, {0, 0, 0},
            command->session}));
        return;
      }
      // Never acknowledge the command, so the bridge times out and stops.
    });

  auto goal_handle = client_->async_send_goal(*make_goal());
  ASSERT_TRUE(goal_handle.wait_for(5s) == std::future_status::ready);

  if (goal_handle.wait_for(5s) != std::future_status::ready) {
    FAIL() << "the bridge never answered the goal request";
  }
  const auto result = wait_result(client_, goal_handle.get());
  EXPECT_EQ(result.code, rclcpp_action::ResultCode::ABORTED);
  EXPECT_EQ(result.result->outcome, ExecuteDeviceCommand::Result::SAFE_STOP);
  EXPECT_EQ(result.result->error_code, device_bridge::kErrorStopRejected);
}

TEST_F(DeviceBridgeNodeTest, ShutdownWithAnActiveCommandStopsTheDevice)
{
  // The bridge used to crash on clean shutdown with a command in flight, and no
  // test below the vcan E2E could see it.
  auto goal_handle = client_->async_send_goal(*make_goal(9));
  ASSERT_TRUE(goal_handle.wait_for(5s) == std::future_status::ready);
  std::this_thread::sleep_for(50ms);

  node_->request_shutdown();
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!node_->shutdown_complete() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(10ms);
  }
  EXPECT_TRUE(node_->shutdown_complete());

  bool saw_stop = false;
  for (const auto & command : sent_commands()) {
    saw_stop = saw_stop || command.opcode == runtime_can::kStopOpcode;
  }
  EXPECT_TRUE(saw_stop) << "shutdown with an active command must STOP the device";
}
