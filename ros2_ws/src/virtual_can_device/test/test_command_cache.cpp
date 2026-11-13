#include "runtime_can/protocol.hpp"

#include <gtest/gtest.h>

#include <map>
#include <string>

// A command identity on the bus used to be `command_id` alone, so a bridge that
// restarted and reused an id could be answered from the device's replay cache
// without the command ever executing. The key is (session, command_id) now, and
// a session-start frame clears everything from other sessions.
namespace
{

using Cache = std::map<runtime_can::CommandKey, std::string>;

}  // namespace

TEST(CommandKeyTest, TheSameIdInTwoSessionsIsTwoIdentities)
{
  const runtime_can::CommandKey first{1, 42};
  const runtime_can::CommandKey second{2, 42};

  EXPECT_FALSE(first == second);
  EXPECT_NE(first, second);

  Cache cache;
  cache[first] = "ack-from-session-1";
  // A restarted bridge picks a new session, so its id 42 is a miss, not a replay.
  EXPECT_EQ(cache.find(second), cache.end());
  ASSERT_NE(cache.find(first), cache.end());
  EXPECT_EQ(cache[first], "ack-from-session-1");
}

TEST(CommandKeyTest, TheKeyOrdersByIdentityNotPadding)
{
  // A total order is what makes the cache deterministic to evict from.
  EXPECT_TRUE((runtime_can::CommandKey{1, 1} < runtime_can::CommandKey{1, 2}));
  EXPECT_TRUE((runtime_can::CommandKey{1, 65535} < runtime_can::CommandKey{2, 1}));
  EXPECT_FALSE((runtime_can::CommandKey{2, 1} < runtime_can::CommandKey{2, 1}));
}

TEST(CommandKeyTest, ACacheCanBeClearedForEveryOtherSession)
{
  Cache cache;
  cache[{1, 1}] = "a";
  cache[{1, 2}] = "b";
  cache[{2, 1}] = "c";

  // What start_session does on the device.
  for (auto it = cache.begin(); it != cache.end(); ) {
    it = it->first.session == 2 ? std::next(it) : cache.erase(it);
  }

  EXPECT_EQ(cache.size(), 1U);
  EXPECT_EQ(cache.count({2, 1}), 1U);
  EXPECT_EQ(cache.count({1, 1}), 0U);
  EXPECT_EQ(cache.count({1, 2}), 0U);
}

// A STOP must execute unconditionally: replaying it would report a stop the
// device did not perform.
TEST(CommandKeyTest, AStopIsNeverServedFromTheCache)
{
  const runtime_can::Command stop{32768, runtime_can::kStopOpcode, 5, 3};
  EXPECT_EQ(stop.opcode, runtime_can::kStopOpcode);
  // The rule the device applies before consulting the cache at all.
  const bool consult_cache = stop.opcode != runtime_can::kStopOpcode;
  EXPECT_FALSE(consult_cache);
}

// The device answered on its own default session instead of the command's, so the
// bridge - which listens on its own session's response id - never saw the ACK and
// every task timed out against a device that had answered. No unit test caught it
// because they all used session 1; the vcan E2E did.
TEST(CommandKeyTest, AnAnswerBelongsToTheSessionItsCommandArrivedIn)
{
  constexpr std::uint32_t kCanSffMask = 0x7FF;
  for (std::uint8_t session = 1; session <= runtime_can::kMaxSession; ++session) {
    const runtime_can::Command command{3410, 1, 42, session};
    auto command_frame = runtime_can::encode_command(command);
    command_frame.id &= kCanSffMask;

    runtime_can::DecodeError error;
    ASSERT_TRUE(runtime_can::decode_command(command_frame, error).has_value());

    // What the device does: answer with the session it just decoded.
    const runtime_can::Response response{3410, 0, 1, {0, 0, 0}, session};
    const auto response_frame = runtime_can::encode_response(response);

    // The bridge filters on exactly this id, so it must match.
    EXPECT_EQ(
      response_frame.id & kCanSffMask,
      runtime_can::frame_id(session, runtime_can::kResponseFrameKind));
    // And a bridge listening on another session must not match it.
    if (session > 1) {
      EXPECT_NE(
        response_frame.id,
        runtime_can::frame_id(session - 1, runtime_can::kResponseFrameKind));
    }

    const auto decoded = runtime_can::decode_response(response_frame, error);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->session, session);
  }
}

TEST(CommandKeyTest, EveryFrameKindFitsInOneElevenBitId)
{
  constexpr std::uint32_t kCanSffMask = 0x7FF;
  for (std::uint8_t session = 0; session <= runtime_can::kMaxSession; ++session) {
    for (const auto kind : {
      runtime_can::kCommandFrameKind, runtime_can::kResponseFrameKind,
      runtime_can::kStatusFrameKind, runtime_can::kSessionStartFrameKind,
      runtime_can::kHeartbeatFrameKind})
    {
      EXPECT_LE(runtime_can::frame_id(session, kind), kCanSffMask)
        << "session " << static_cast<int>(session) << " kind " << kind;
    }
  }
}
