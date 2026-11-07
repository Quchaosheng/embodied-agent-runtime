#pragma once

#include <builtin_interfaces/msg/duration.hpp>

#include <chrono>
#include <cstdint>

namespace device_bridge
{

constexpr std::uint16_t kErrorInvalidGoal = 100;
constexpr std::uint16_t kErrorTransport = 200;
constexpr std::uint16_t kErrorAckTimeout = 201;
constexpr std::uint16_t kErrorShutdown = 202;
constexpr std::uint16_t kErrorCanceled = 203;
constexpr std::uint16_t kErrorStopTimeout = 204;
constexpr std::uint16_t kErrorStopRejected = 205;
constexpr std::uint16_t kErrorDeviceRejectedBase = 300;

// Convert the wire-level relative duration into a bounded local timeout.
bool duration_to_milliseconds(
  const builtin_interfaces::msg::Duration & duration,
  std::int64_t max_milliseconds,
  std::chrono::milliseconds & result);

// An application command must stay inside runtime_can's application ID range.
// IDs at or above kStopCommandIdMin belong to STOP, so admitting one would let an
// application ACK and a STOP ACK share an identity.
bool application_command_id_in_range(std::uint32_t command_id);

std::uint16_t device_error_code(std::uint8_t result_code);

}
