#include "runtime_gateway/request_state.hpp"

#include <array>

namespace runtime_gateway
{

namespace
{

struct StateName
{
  RequestState state;
  std::string_view name;
  std::uint8_t outcome;
};

// The outcome each terminal state settles with. Non-terminal states carry the
// in-progress value the wire already used.
constexpr std::array<StateName, 11> kStates{{
  {RequestState::kSubmitting, "SUBMITTING", 0},
  {RequestState::kRunning, "RUNNING", 0},
  {RequestState::kCanceling, "CANCELING", 0},
  {RequestState::kCompleted, "COMPLETED", 0},
  {RequestState::kCanceled, "CANCELED", 1},
  {RequestState::kSafeStop, "SAFE_STOP", 2},
  {RequestState::kDeviceFault, "DEVICE_FAULT", 3},
  // No action ever started: outside the four action outcomes on purpose.
  {RequestState::kRejected, "REJECTED", 4},
  {RequestState::kTimedOut, "TIMED_OUT", 4},
  {RequestState::kCancelFailed, "CANCEL_FAILED", 2},
  {RequestState::kUncertain, "UNCERTAIN", 2},
}};

}  // namespace

std::string_view to_string(const RequestState state)
{
  for (const auto & entry : kStates) {
    if (entry.state == state) {
      return entry.name;
    }
  }
  if (state == RequestState::kAborted) {
    return "ABORTED";
  }
  return "UNKNOWN";
}

std::optional<RequestState> parse_state(const std::string_view name)
{
  for (const auto & entry : kStates) {
    if (entry.name == name) {
      return entry.state;
    }
  }
  if (name == "ABORTED") {
    return RequestState::kAborted;
  }
  return std::nullopt;
}

bool is_terminal(const RequestState state)
{
  switch (state) {
    case RequestState::kCompleted:
    case RequestState::kCanceled:
    case RequestState::kSafeStop:
    case RequestState::kDeviceFault:
    case RequestState::kRejected:
    case RequestState::kTimedOut:
    case RequestState::kCancelFailed:
    case RequestState::kUncertain:
    case RequestState::kAborted:
      return true;
    case RequestState::kSubmitting:
    case RequestState::kRunning:
    case RequestState::kCanceling:
      return false;
  }
  return true;
}

bool is_legal_transition(const RequestState from, const RequestState to)
{
  // A terminal state is final: a second terminal transition used to be allowed,
  // which is why several commits in the history are about a duplicate one.
  if (is_terminal(from)) {
    return false;
  }
  if (from == to) {
    // Re-entering the same state is a no-op, not a transition.
    return false;
  }
  switch (from) {
    case RequestState::kSubmitting:
      // Dispatch either ran, was refused, or never answered.
      return to == RequestState::kRunning || is_terminal(to) ||
             to == RequestState::kCanceling;
    case RequestState::kRunning:
      return to == RequestState::kCanceling || is_terminal(to);
    case RequestState::kCanceling:
      // A cancel either settles or fails to be confirmed.
      return to == RequestState::kCanceled || to == RequestState::kSafeStop ||
             to == RequestState::kCancelFailed || to == RequestState::kUncertain ||
             to == RequestState::kTimedOut || to == RequestState::kDeviceFault ||
             to == RequestState::kCompleted || to == RequestState::kAborted;
    default:
      return false;
  }
}

std::uint8_t outcome_for(const RequestState state)
{
  for (const auto & entry : kStates) {
    if (entry.state == state) {
      return entry.outcome;
    }
  }
  return 2;
}

}
