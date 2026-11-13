#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace runtime_gateway
{

// A request's state used to be a free-form string compared as a literal in C++,
// so a typo compiled and created a state nothing ever matched, and the one
// "terminal" writer accepted any move, twice. This is the closed set plus the
// legal transitions.
enum class RequestState
{
  // The request is registered and a goal is being dispatched.
  kSubmitting,
  kRunning,
  kCanceling,
  // Terminal states. A closed set: everything here ends the request.
  kCompleted,
  kCanceled,
  kSafeStop,
  kDeviceFault,
  kRejected,
  kTimedOut,
  kCancelFailed,
  kUncertain,
  kAborted,
};

std::string_view to_string(RequestState state);
// Returns nullopt for a string that is not one of the declared states, which is
// what makes an unknown state a reported error instead of a new state.
std::optional<RequestState> parse_state(std::string_view name);

bool is_terminal(RequestState state);

// Declared moves. Everything else is refused, which is the property the registry
// never had: a request could reach any state, in any order, any number of times.
bool is_legal_transition(RequestState from, RequestState to);

// The legal outcome for a terminal state, so the two cannot disagree.
std::uint8_t outcome_for(RequestState state);

struct Transition
{
  bool applied{false};
  // Set when the move was refused; the caller logs it rather than silently
  // leaving the request in the wrong state.
  std::string reason;
};

}
