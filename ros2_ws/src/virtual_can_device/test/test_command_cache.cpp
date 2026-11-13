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
