#pragma once

#include "runtime_can/protocol.hpp"

#include <builtin_interfaces/msg/duration.hpp>

#include <chrono>
#include <cstdint>
#include <optional>

namespace device_bridge
{

constexpr std::uint16_t kErrorInvalidGoal = 100;
constexpr std::uint16_t kErrorTransport = 200;
constexpr std::uint16_t kErrorAckTimeout = 201;
constexpr std::uint16_t kErrorShutdown = 202;
constexpr std::uint16_t kErrorCanceled = 203;
constexpr std::uint16_t kErrorStopTimeout = 204;
constexpr std::uint16_t kErrorStopRejected = 205;
// A device that reports execution separately did not finish before the bound.
constexpr std::uint16_t kErrorCompletionTimeout = 206;
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

// A device may keep its command replay cache across a bridge restart, so STOP
// ids cannot start over at a fixed value: the device would answer a new STOP
// from a stale entry and refuse it as a conflict. Kept here so the id rules
// are testable without a CAN interface.

// How many STOP ids one stop sequence may try when the device refuses them.
constexpr std::uint8_t kMaxStopAttempts = 3;

// Maps any entropy value to a STOP command id.
std::uint16_t stop_command_id_from(std::uint32_t entropy);

// The id after `current`, wrapping inside the STOP range.
std::uint16_t next_stop_command_id(std::uint16_t current);

// What an acknowledged command settles as. This is the state machine the node
// follows, kept here so the decision is testable without a CAN interface.
enum class Acknowledgement
{
  // The protocol only acknowledges; the result means "received", not "finished".
  kAckOnlyCompleted,
  kAccepted,
  kExecuting,
  kCompleted,
  kFaulted,
  // No status arrived before the completion bound.
  kTimedOut,
};

// Maps one device status, or its absence, to the action the bridge takes.
// A fault is settled by its own state, never by the timeout.
Acknowledgement classify_status(
  const std::optional<runtime_can::Completion> & status, bool completion_reported);

}
