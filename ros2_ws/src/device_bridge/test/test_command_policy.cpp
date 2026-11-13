#include "device_bridge/command_policy.hpp"

#include "robot_task_interfaces/error_code.hpp"

#include <gtest/gtest.h>

#include "runtime_can/protocol.hpp"

TEST(CommandPolicyTest, AcceptsOnlyTheApplicationCommandIdRange)
{
  // The partition belongs to runtime_can; STOP owns everything at or above
  // kStopCommandIdMin, so an ordinary command there is refused.
  EXPECT_TRUE(device_bridge::application_command_id_in_range(1));
  EXPECT_TRUE(
    device_bridge::application_command_id_in_range(runtime_can::kApplicationCommandIdMax));
  EXPECT_FALSE(device_bridge::application_command_id_in_range(0));
  EXPECT_FALSE(
    device_bridge::application_command_id_in_range(runtime_can::kStopCommandIdMin));
  EXPECT_FALSE(device_bridge::application_command_id_in_range(0xFFFFU));
  EXPECT_FALSE(device_bridge::application_command_id_in_range(0x10000U));
}

TEST(CommandPolicyTest, ApplicationAndStopIdRangesDoNotOverlap)
{
  // Pins the invariant the check depends on, so widening either constant fails here.
  EXPECT_EQ(
    static_cast<std::uint32_t>(runtime_can::kApplicationCommandIdMax) + 1U,
    static_cast<std::uint32_t>(runtime_can::kStopCommandIdMin));
}

TEST(CommandPolicyTest, RoundsPositiveSubMillisecondDurationUp)
{
  builtin_interfaces::msg::Duration duration;
  duration.nanosec = 1;
  std::chrono::milliseconds timeout;

  EXPECT_TRUE(device_bridge::duration_to_milliseconds(duration, 10, timeout));
  EXPECT_EQ(timeout.count(), 1);
}

TEST(CommandPolicyTest, RejectsZeroNegativeMalformedAndTooLargeDurations)
{
  builtin_interfaces::msg::Duration duration;
  std::chrono::milliseconds timeout;

  EXPECT_FALSE(device_bridge::duration_to_milliseconds(duration, 10, timeout));

  duration.sec = -1;
  EXPECT_FALSE(device_bridge::duration_to_milliseconds(duration, 10, timeout));

  duration.sec = 0;
  duration.nanosec = 1000000000U;
  EXPECT_FALSE(device_bridge::duration_to_milliseconds(duration, 10, timeout));

  duration.nanosec = 100000000U;
  EXPECT_FALSE(device_bridge::duration_to_milliseconds(duration, 10, timeout));
}

TEST(CommandPolicyTest, MapsDeviceResultToStableErrorNamespace)
{
  // Read from the shared table rather than a literal, and assert the property
  // that matters: a device code can never be mistaken for another layer's.
  using robot_task_interfaces::error_code::Layer;
  EXPECT_EQ(
    device_bridge::device_error_code(1),
    robot_task_interfaces::error_code::device_rejected(1));
  EXPECT_EQ(
    device_bridge::device_error_code(2),
    robot_task_interfaces::error_code::device_rejected(2));
  EXPECT_EQ(
    robot_task_interfaces::error_code::layer_of(device_bridge::device_error_code(1)),
    Layer::kDevice);
  EXPECT_EQ(
    robot_task_interfaces::error_code::layer_of(device_bridge::device_error_code(255)),
    Layer::kDevice);
  // The old 300+result_code range overlapped the gateway's 400/500 codes.
  EXPECT_NE(
    device_bridge::device_error_code(200),
    robot_task_interfaces::error_code::kGatewayNotAllowlisted);
}

// The ACK-only path must stay available for devices that only acknowledge, and
// must be distinguishable from a device that actually reported completion.
TEST(CommandPolicyTest, AckOnlyCompletionIsLabelledNotAssumed)
{
  using device_bridge::Acknowledgement;
  EXPECT_EQ(
    device_bridge::classify_status(std::nullopt, false),
    Acknowledgement::kAckOnlyCompleted);
}

TEST(CommandPolicyTest, MapsEachReportedDeviceState)
{
  using device_bridge::Acknowledgement;
  using runtime_can::Completion;
  EXPECT_EQ(
    device_bridge::classify_status(Completion::kAccepted, true), Acknowledgement::kAccepted);
  EXPECT_EQ(
    device_bridge::classify_status(Completion::kExecuting, true), Acknowledgement::kExecuting);
  EXPECT_EQ(
    device_bridge::classify_status(Completion::kCompleted, true), Acknowledgement::kCompleted);
  EXPECT_EQ(
    device_bridge::classify_status(Completion::kFaulted, true), Acknowledgement::kFaulted);
}

// "Executing" without a bound is not a state machine: silence from a protocol
// that reports completion is a timeout, never a completion.
TEST(CommandPolicyTest, MissingStatusOnACompletionProtocolIsATimeout)
{
  EXPECT_EQ(
    device_bridge::classify_status(std::nullopt, true),
    device_bridge::Acknowledgement::kTimedOut);
}

// A fault settles as a fault even though no further status may arrive.
TEST(CommandPolicyTest, FaultIsSettledByItsOwnState)
{
  const auto faulted = device_bridge::classify_status(runtime_can::Completion::kFaulted, true);
  EXPECT_EQ(faulted, device_bridge::Acknowledgement::kFaulted);
  EXPECT_NE(faulted, device_bridge::Acknowledgement::kTimedOut);
}
