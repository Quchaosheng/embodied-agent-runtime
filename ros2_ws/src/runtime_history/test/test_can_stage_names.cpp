#include "runtime_history/can_stage_names.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <string_view>

// The names a consumer in another repository has to match. They are asserted here
// so the recorded vocabulary and the documented one cannot drift apart silently.
TEST(CanStageNamesTest, NamesAreTheFrozenVocabulary)
{
  EXPECT_STREQ(runtime_history::can_stage::kEncodeStart, "can_encode_start");
  EXPECT_STREQ(runtime_history::can_stage::kEncodeEnd, "can_encode_end");
  EXPECT_STREQ(runtime_history::can_stage::kFrameSent, "can_frame_sent");
  EXPECT_STREQ(runtime_history::can_stage::kAckWaitStart, "can_ack_wait_start");
  EXPECT_STREQ(runtime_history::can_stage::kAckTimeout, "can_ack_timeout");
  EXPECT_STREQ(runtime_history::can_stage::kRetryScheduled, "can_retry_scheduled");
  EXPECT_STREQ(runtime_history::can_stage::kAckReceived, "can_ack_received");
  EXPECT_STREQ(runtime_history::can_stage::kRetryExhausted, "can_retry_exhausted");
}

TEST(CanStageNamesTest, NamesAreUniqueAndNonEmpty)
{
  const std::set<std::string_view> names{
    runtime_history::can_stage::kEncodeStart,
    runtime_history::can_stage::kEncodeEnd,
    runtime_history::can_stage::kFrameSent,
    runtime_history::can_stage::kAckWaitStart,
    runtime_history::can_stage::kAckTimeout,
    runtime_history::can_stage::kRetryScheduled,
    runtime_history::can_stage::kAckReceived,
    runtime_history::can_stage::kRetryExhausted,
  };

  EXPECT_EQ(names.size(), 8U);
  for (const auto & name : names) {
    EXPECT_FALSE(name.empty());
    EXPECT_EQ(name.rfind("can_", 0), 0U);
  }
}
