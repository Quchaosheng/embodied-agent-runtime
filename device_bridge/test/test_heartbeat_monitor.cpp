#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>

#include "device_bridge/heartbeat_monitor.hpp"

using namespace std::chrono_literals;

TEST(HeartbeatMonitorTest, AcceptsReadyHeartbeatUntilTimeout) {
  device_bridge::HeartbeatMonitor monitor(0x321, 500ms);
  const auto now = device_bridge::HeartbeatMonitor::TimePoint{} + 1s;
  const std::array<std::uint8_t, 2> payload{1, 1};

  EXPECT_TRUE(monitor.observe(0x321, payload.data(), payload.size(), now));
  EXPECT_TRUE(monitor.ready(now + 500ms));
  EXPECT_FALSE(monitor.ready(now + 501ms));
}

TEST(HeartbeatMonitorTest, NotReadyFlagFailsClosedImmediately) {
  device_bridge::HeartbeatMonitor monitor(0x321, 500ms);
  const auto now = device_bridge::HeartbeatMonitor::TimePoint{} + 1s;
  const std::array<std::uint8_t, 2> ready{1, 1};
  const std::array<std::uint8_t, 2> stopped{1, 0};

  EXPECT_TRUE(monitor.observe(0x321, ready.data(), ready.size(), now));
  EXPECT_TRUE(monitor.observe(0x321, stopped.data(), stopped.size(), now + 1ms));
  EXPECT_FALSE(monitor.ready(now + 1ms));
}

TEST(HeartbeatMonitorTest, RejectsWrongIdentifierVersionLengthAndReservedFlags) {
  device_bridge::HeartbeatMonitor monitor(0x321, 500ms);
  const auto now = device_bridge::HeartbeatMonitor::TimePoint{} + 1s;
  const std::array<std::uint8_t, 2> valid{1, 1};
  const std::array<std::uint8_t, 2> wrong_version{2, 1};
  const std::array<std::uint8_t, 2> reserved_flag{1, 3};

  EXPECT_FALSE(monitor.observe(0x320, valid.data(), valid.size(), now));
  EXPECT_FALSE(monitor.observe(0x321, wrong_version.data(), wrong_version.size(), now));
  EXPECT_FALSE(monitor.observe(0x321, valid.data(), 1, now));
  EXPECT_FALSE(monitor.observe(0x321, reserved_flag.data(), reserved_flag.size(), now));
  EXPECT_EQ(monitor.accepted_frames(), 0U);
  EXPECT_EQ(monitor.rejected_frames(), 4U);
  EXPECT_FALSE(monitor.ready(now));
}

TEST(HeartbeatMonitorTest, RejectsInvalidConfiguration) {
  EXPECT_THROW(device_bridge::HeartbeatMonitor(0x800, 500ms), std::invalid_argument);
  EXPECT_THROW(device_bridge::HeartbeatMonitor(0x321, 0ms), std::invalid_argument);
}
