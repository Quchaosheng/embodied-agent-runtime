#include "device_bridge/command_policy.hpp"

#include <gtest/gtest.h>

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
  EXPECT_EQ(device_bridge::device_error_code(1), 301);
  EXPECT_EQ(device_bridge::device_error_code(2), 302);
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
