#include "runtime_gateway/request_registry.hpp"

#include <gtest/gtest.h>

namespace
{

runtime_gateway::RequestRecord record(
  std::string request_id = "request-1", std::string task_id = "task-1",
  std::string target_id = "dock_a")
{
  return {std::move(request_id), std::move(task_id), std::move(target_id), "single_task",
    "SUBMITTING", 0, 0, ""};
}

TEST(RequestRegistryTest, RejectsEmptyIdentifiers)
{
  runtime_gateway::RequestRegistry registry;
  EXPECT_EQ(registry.insert(record("", "task-1")), runtime_gateway::InsertResult::INVALID);
  EXPECT_EQ(registry.insert(record("request-1", "")), runtime_gateway::InsertResult::INVALID);
  EXPECT_EQ(registry.insert(record("request-1", "task-1", "")),
      runtime_gateway::InsertResult::INVALID);
}

TEST(RequestRegistryTest, InsertsAndReturnsFirstRequest)
{
  runtime_gateway::RequestRegistry registry;
  const auto expected = record();
  ASSERT_EQ(registry.insert(expected), runtime_gateway::InsertResult::INSERTED);
  ASSERT_TRUE(registry.get_by_request_id(expected.request_id).has_value());
  EXPECT_EQ(registry.get_by_request_id(expected.request_id)->task_id, expected.task_id);
}

TEST(RequestRegistryTest, DistinguishesExactAndConflictingDuplicates)
{
  runtime_gateway::RequestRegistry registry;
  const auto expected = record();
  ASSERT_EQ(registry.insert(expected), runtime_gateway::InsertResult::INSERTED);
  EXPECT_EQ(registry.insert(expected), runtime_gateway::InsertResult::DUPLICATE);
  auto conflicting = expected;
  conflicting.task_id = "task-2";
  EXPECT_EQ(registry.insert(conflicting), runtime_gateway::InsertResult::CONFLICT);
  EXPECT_EQ(registry.size(), 1U);
}

TEST(RequestRegistryTest, RejectsDifferentRequestForExistingTaskId)
{
  runtime_gateway::RequestRegistry registry;
  ASSERT_EQ(registry.insert(record("request-1", "task-1")),
      runtime_gateway::InsertResult::INSERTED);
  EXPECT_EQ(
    registry.insert(record("request-2", "task-1")),
    runtime_gateway::InsertResult::CONFLICT);
  ASSERT_TRUE(registry.get_by_task_id("task-1").has_value());
  EXPECT_EQ(registry.get_by_task_id("task-1")->request_id, "request-1");
}

TEST(RequestRegistryTest, TreatsDifferentTargetAsConflictingRequest)
{
  runtime_gateway::RequestRegistry registry;
  ASSERT_EQ(registry.insert(record()), runtime_gateway::InsertResult::INSERTED);
  EXPECT_EQ(
    registry.insert(record("request-1", "task-1", "home")),
    runtime_gateway::InsertResult::CONFLICT);
  EXPECT_EQ(registry.size(), 1U);
}

TEST(RequestRegistryTest, MakesTerminalUpdateVisibleByBothIdentifiers)
{
  runtime_gateway::RequestRegistry registry;
  ASSERT_EQ(registry.insert(record()), runtime_gateway::InsertResult::INSERTED);
  ASSERT_TRUE(registry.update_terminal("request-1", "COMPLETED", 0, 0, "done"));
  const auto by_request = registry.get_by_request_id("request-1");
  const auto by_task = registry.get_by_task_id("task-1");
  ASSERT_TRUE(by_request.has_value());
  ASSERT_TRUE(by_task.has_value());
  EXPECT_EQ(by_request->state, "COMPLETED");
  EXPECT_EQ(by_task->message, "done");
  EXPECT_FALSE(registry.update_terminal("missing", "FAILED", 2, 1, "missing"));
}

TEST(RequestRegistryTest, ClearsRecordsForShutdown)
{
  runtime_gateway::RequestRegistry registry;
  ASSERT_EQ(registry.insert(record()), runtime_gateway::InsertResult::INSERTED);
  registry.clear();
  EXPECT_EQ(registry.size(), 0U);
  EXPECT_FALSE(registry.get_by_request_id("request-1").has_value());
}

}  // namespace
