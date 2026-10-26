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
