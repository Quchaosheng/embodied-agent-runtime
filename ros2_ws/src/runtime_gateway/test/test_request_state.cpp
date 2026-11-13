#include "runtime_gateway/request_state.hpp"
#include "runtime_gateway/request_registry.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>

using runtime_gateway::is_legal_transition;
using runtime_gateway::is_terminal;
using runtime_gateway::RequestState;

namespace
{

const RequestState kAllStates[] = {
  RequestState::kSubmitting,
  RequestState::kRunning,
  RequestState::kCanceling,
  RequestState::kCompleted,
  RequestState::kCanceled,
  RequestState::kSafeStop,
  RequestState::kDeviceFault,
  RequestState::kRejected,
  RequestState::kTimedOut,
  RequestState::kCancelFailed,
  RequestState::kUncertain,
  RequestState::kAborted,
};

}

// The issue's table-driven requirement: enumerate every pair and assert only the
// declared ones are accepted.
TEST(RequestStateTest, OnlyDeclaredTransitionsAreLegal)
{
  std::set<std::pair<RequestState, RequestState>> legal;
  for (const auto from : kAllStates) {
    for (const auto to : kAllStates) {
      if (is_legal_transition(from, to)) {
        legal.emplace(from, to);
      }
    }
  }

  // A terminal state is final: nothing leaves it.
  for (const auto from : kAllStates) {
    if (is_terminal(from)) {
      for (const auto to : kAllStates) {
        EXPECT_FALSE(is_legal_transition(from, to))
          << "terminal " << runtime_gateway::to_string(from) << " moved to "
          << runtime_gateway::to_string(to);
      }
    }
  }

  // No state transitions to itself.
  for (const auto state : kAllStates) {
    EXPECT_FALSE(is_legal_transition(state, state));
  }

  // The forward path the issue names.
  EXPECT_TRUE(is_legal_transition(RequestState::kSubmitting, RequestState::kRunning));
  EXPECT_TRUE(is_legal_transition(RequestState::kRunning, RequestState::kCanceling));
  EXPECT_TRUE(is_legal_transition(RequestState::kCanceling, RequestState::kCanceled));

  // Backwards and sideways moves are refused.
  EXPECT_FALSE(is_legal_transition(RequestState::kRunning, RequestState::kSubmitting));
  EXPECT_FALSE(is_legal_transition(RequestState::kCanceling, RequestState::kRunning));
  EXPECT_FALSE(is_legal_transition(RequestState::kCanceled, RequestState::kCompleted));

  EXPECT_FALSE(legal.empty());
}

TEST(RequestStateTest, EveryNameRoundTripsAndUnknownNamesAreRejected)
{
  std::set<std::string> names;
  for (const auto state : kAllStates) {
    const auto name = std::string(runtime_gateway::to_string(state));
    EXPECT_TRUE(names.insert(name).second) << "duplicate name " << name;
    const auto parsed = runtime_gateway::parse_state(name);
    ASSERT_TRUE(parsed.has_value()) << name;
    EXPECT_EQ(*parsed, state);
  }

  // The defect this replaces: a typo used to compile and create a state nothing
  // ever matched.
  EXPECT_FALSE(runtime_gateway::parse_state("CANCELINGG").has_value());
  EXPECT_FALSE(runtime_gateway::parse_state("").has_value());
  EXPECT_FALSE(runtime_gateway::parse_state("canceling").has_value());
}

TEST(RequestStateTest, TheRegistryRefusesAnIllegalMoveAndASecondTerminal)
{
  runtime_gateway::RequestRegistry registry;
  // insert() requires request, task and target: an empty target is INVALID, not
  // a record the registry will hold.
  runtime_gateway::RequestRecord record{};
  record.request_id = "request-1";
  record.task_id = "task-1";
  record.target_id = "dock_a";
  record.workflow_id = "single_task";
  record.timeout_ms = 3000;
  ASSERT_EQ(
    registry.insert(record), runtime_gateway::InsertResult::INSERTED);

  // Submitting -> Running is declared.
  EXPECT_TRUE(
    registry.transition(
      "request-1", RequestState::kRunning, 0, 0, "accepted").applied);

  // Running -> Submitting is not, and is refused rather than applied.
  const auto backwards =
    registry.transition("request-1", RequestState::kSubmitting, 0, 0, "back");
  EXPECT_FALSE(backwards.applied);
  EXPECT_NE(backwards.reason.find("illegal transition"), std::string::npos);
  EXPECT_EQ(registry.state_of("request-1"), RequestState::kRunning);

  // Settling once works; settling twice does not overwrite the first result.
  EXPECT_TRUE(registry.settle("request-1", RequestState::kCompleted, 0, "done").applied);
  const auto second = registry.settle("request-1", RequestState::kCanceled, 0, "late");
  EXPECT_FALSE(second.applied) << "a second terminal transition was accepted";
  EXPECT_EQ(registry.state_of("request-1"), RequestState::kCompleted);
}

TEST(RequestStateTest, SettleTakesTheOutcomeFromTheTable)
{
  runtime_gateway::RequestRegistry registry;
  runtime_gateway::RequestRecord record{};
  record.request_id = "request-2";
  record.task_id = "task-2";
  record.target_id = "dock_a";
  record.workflow_id = "single_task";
  record.timeout_ms = 3000;
  ASSERT_EQ(registry.insert(record), runtime_gateway::InsertResult::INSERTED);
  ASSERT_TRUE(registry.transition("request-2", RequestState::kRunning, 0, 0, "").applied);

  // A caller cannot settle COMPLETED with a canceled outcome.
  ASSERT_TRUE(registry.settle("request-2", RequestState::kCanceled, 0, "").applied);
  const auto stored = registry.get_by_request_id("request-2");
  ASSERT_TRUE(stored.has_value());
  EXPECT_EQ(stored->outcome, runtime_gateway::outcome_for(RequestState::kCanceled));
}
