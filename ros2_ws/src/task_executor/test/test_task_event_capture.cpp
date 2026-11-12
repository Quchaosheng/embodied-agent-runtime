#include "task_executor/task_event_capture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

robot_task_interfaces::msg::TaskEvent sample(
  const std::string & task_id = "task-1",
  const std::string & message = "task completed through Device Bridge")
{
  robot_task_interfaces::msg::TaskEvent event;
  event.stamp.sec = 1791790000;
  event.stamp.nanosec = 123456789;
  event.task_id = task_id;
  event.target_id = "dock_a";
  event.request_id = "request-1";
  event.workflow_id = "single_task";
  event.goal_id = "1a2b3c";
  event.action_status = 1;
  event.outcome = 0;
  event.error_code = 0;
  event.duration_ms = 37;
  event.message = message;
  return event;
}

}

// The field names are the cross-repository contract: the consumer in
// workbench-mobile-home-robot reads exactly these keys. A rename here has to be a
// deliberate interface change, not a refactor.
TEST(TaskEventCaptureTest, EmitsTheKeysTheConsumerReads)
{
  const auto jsonl = task_executor::TaskEventCapture::to_jsonl(sample());

  for (const char * key : {
    "\"stamp\":{\"sec\":1791790000,\"nanosec\":123456789}",
    "\"task_id\":\"task-1\"",
    "\"target_id\":\"dock_a\"",
    "\"request_id\":\"request-1\"",
    "\"workflow_id\":\"single_task\"",
    "\"goal_id\":\"1a2b3c\"",
    "\"action_status\":1",
    "\"outcome\":0",
    "\"error_code\":0",
    "\"duration_ms\":37",
    "\"message\":\"task completed through Device Bridge\"",
  })
  {
    EXPECT_NE(jsonl.find(key), std::string::npos) << "missing " << key;
  }
  EXPECT_EQ(jsonl.back(), '\n');
}

TEST(TaskEventCaptureTest, EscapesIdentifiersThatComeFromTheNetwork)
{
  const auto jsonl = task_executor::TaskEventCapture::to_jsonl(
    sample("task-\"quoted\"-\\back", "line\nbreak"));

  EXPECT_NE(jsonl.find("task-\\\"quoted\\\"-\\\\back"), std::string::npos);
  EXPECT_NE(jsonl.find("line\\nbreak"), std::string::npos);
  // Exactly one line: an unescaped newline would split the record.
  EXPECT_EQ(std::count(jsonl.begin(), jsonl.end(), '\n'), 1);
}

TEST(TaskEventCaptureTest, AppendsOneLinePerEventToTheNamedFile)
{
  const std::string path = testing::TempDir() + "/task-events-capture.jsonl";
  std::remove(path.c_str());

  {
    task_executor::TaskEventCapture capture(path);
    ASSERT_TRUE(capture.enabled());
    EXPECT_TRUE(capture.append(sample("task-1")));
    EXPECT_TRUE(capture.append(sample("task-2")));
  }

  std::ifstream input(path);
  ASSERT_TRUE(input.is_open());
  std::stringstream buffer;
  buffer << input.rdbuf();
  const std::string content = buffer.str();

  EXPECT_EQ(std::count(content.begin(), content.end(), '\n'), 2);
  EXPECT_NE(content.find("\"task_id\":\"task-1\""), std::string::npos);
  EXPECT_NE(content.find("\"task_id\":\"task-2\""), std::string::npos);
  std::remove(path.c_str());
}

TEST(TaskEventCaptureTest, AnEmptyPathDisablesCaptureInsteadOfFailing)
{
  task_executor::TaskEventCapture capture("");

  EXPECT_FALSE(capture.enabled());
  EXPECT_FALSE(capture.append(sample()));
}
