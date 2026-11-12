#include "device_bridge/command_policy.hpp"

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
  EXPECT_EQ(device_bridge::device_error_code(1), 301);
  EXPECT_EQ(device_bridge::device_error_code(2), 302);
}

// A device that keeps its replay cache across a bridge restart refuses a STOP
// id it has already seen, so the first id must not be a fixed value.
TEST(CommandPolicyTest, StopCommandIdIsDerivedFromEntropyInsideTheStopRange)
{
  for (const std::uint32_t entropy : {0U, 1U, 0x7FFFU, 0x8000U, 0x12345678U, 0xFFFFFFFFU}) {
    EXPECT_GE(device_bridge::stop_command_id_from(entropy), runtime_can::kStopCommandIdMin) <<
      entropy;
  }
  EXPECT_EQ(device_bridge::stop_command_id_from(0U), runtime_can::kStopCommandIdMin);
  EXPECT_EQ(device_bridge::stop_command_id_from(0x7FFFU), 0xFFFFU);
  EXPECT_EQ(device_bridge::stop_command_id_from(0x8000U), runtime_can::kStopCommandIdMin);
  EXPECT_NE(device_bridge::stop_command_id_from(1U), device_bridge::stop_command_id_from(2U));
}

TEST(CommandPolicyTest, NextStopCommandIdWrapsInsideTheStopRange)
{
  EXPECT_EQ(device_bridge::next_stop_command_id(0x8000U), 0x8001U);
  EXPECT_EQ(device_bridge::next_stop_command_id(0xFFFFU), runtime_can::kStopCommandIdMin);
  // An application-range id is never continued as a STOP id.
  EXPECT_EQ(device_bridge::next_stop_command_id(0x7FFFU), runtime_can::kStopCommandIdMin);
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
