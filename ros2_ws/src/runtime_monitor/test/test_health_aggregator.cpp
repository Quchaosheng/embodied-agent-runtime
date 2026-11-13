#include "runtime_monitor/health_aggregator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>

namespace
{
diagnostic_msgs::msg::DiagnosticStatus status(
  const std::string & name, const std::uint8_t level)
{
  diagnostic_msgs::msg::DiagnosticStatus value;
  value.name = name;
  value.level = level;
  value.message = "test";
  return value;
}
// Publishes every default component at one level, so a case can focus on the
// component it is actually about.
void publish_all(
  runtime_monitor::HealthAggregator & aggregator, const std::uint8_t level,
  const runtime_monitor::SteadyTime at, const std::string & skip = "")
{
  for (const auto & name : runtime_monitor::default_components()) {
    if (name == skip) {
      continue;
    }
    aggregator.update(status(name, level), at);
  }
}

}  // namespace

TEST(HealthAggregatorTest, WaitsForBothCoreSources)
{
  runtime_monitor::HealthAggregator aggregator(std::chrono::milliseconds(2000));
  const runtime_monitor::SteadyTime now{};
  EXPECT_TRUE(aggregator.update(
    status(runtime_monitor::kDeviceBridgeName, diagnostic_msgs::msg::DiagnosticStatus::OK), now));
  const auto health = aggregator.evaluate(now);
  EXPECT_FALSE(health.ready);
  EXPECT_EQ(health.level, diagnostic_msgs::msg::DiagnosticStatus::STALE);
  EXPECT_FALSE(health.task_executor.seen);
}

TEST(HealthAggregatorTest, WarnRemainsReady)
{
  runtime_monitor::HealthAggregator aggregator(std::chrono::milliseconds(2000));
  const runtime_monitor::SteadyTime now{};
  // Everything healthy first, then the one component this case is about: the
  // helper publishes all of them, so it has to run before the override.
  publish_all(aggregator, diagnostic_msgs::msg::DiagnosticStatus::OK, now);
  aggregator.update(
    status(runtime_monitor::kDeviceBridgeName, diagnostic_msgs::msg::DiagnosticStatus::WARN), now);
  const auto health = aggregator.evaluate(now);
  EXPECT_TRUE(health.ready);
  EXPECT_EQ(health.level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
}

TEST(HealthAggregatorTest, ErrorMakesRuntimeNotReady)
{
  runtime_monitor::HealthAggregator aggregator(std::chrono::milliseconds(2000));
  const runtime_monitor::SteadyTime now{};
  publish_all(aggregator, diagnostic_msgs::msg::DiagnosticStatus::OK, now);
  aggregator.update(
    status(runtime_monitor::kDeviceBridgeName, diagnostic_msgs::msg::DiagnosticStatus::ERROR), now);
  const auto health = aggregator.evaluate(now);
  EXPECT_FALSE(health.ready);
  EXPECT_EQ(health.level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
}

TEST(HealthAggregatorTest, StaleTakesPrecedenceAndRecoveryIsAllowed)
{
  runtime_monitor::HealthAggregator aggregator(std::chrono::milliseconds(2000));
  const runtime_monitor::SteadyTime start{};
  aggregator.update(
    status(runtime_monitor::kDeviceBridgeName, diagnostic_msgs::msg::DiagnosticStatus::OK), start);
  aggregator.update(
    status(runtime_monitor::kTaskExecutorName, diagnostic_msgs::msg::DiagnosticStatus::OK), start);
  publish_all(aggregator, diagnostic_msgs::msg::DiagnosticStatus::OK, start);
  EXPECT_FALSE(aggregator.evaluate(start + std::chrono::milliseconds(2001)).ready);

  const auto recovered_at = start + std::chrono::milliseconds(2100);
  aggregator.update(
    status(runtime_monitor::kDeviceBridgeName, diagnostic_msgs::msg::DiagnosticStatus::OK),
    recovered_at);
  aggregator.update(
    status(runtime_monitor::kTaskExecutorName, diagnostic_msgs::msg::DiagnosticStatus::OK),
    recovered_at);
  publish_all(aggregator, diagnostic_msgs::msg::DiagnosticStatus::OK, recovered_at);
  EXPECT_TRUE(aggregator.evaluate(recovered_at).ready);
}

TEST(HealthAggregatorTest, IgnoresUnknownAndOwnSummary)
{
  runtime_monitor::HealthAggregator aggregator(std::chrono::milliseconds(2000));
  const runtime_monitor::SteadyTime now{};
  EXPECT_FALSE(aggregator.update(status("other/component", 0), now));
  EXPECT_FALSE(aggregator.update(status(runtime_monitor::kSystemName, 0), now));
}

// The issue's core case: a component that is configured but silent must make the
// runtime not-ready, instead of being dropped from the summary.
TEST(HealthAggregatorTest, AConfiguredComponentThatNeverPublishesIsReportedNotIgnored)
{
  runtime_monitor::HealthAggregator aggregator(std::chrono::milliseconds(2000));
  const runtime_monitor::SteadyTime now{};
  publish_all(
    aggregator, diagnostic_msgs::msg::DiagnosticStatus::OK, now,
    runtime_monitor::kTaskOrchestratorName);

  const auto health = aggregator.evaluate(now);
  EXPECT_FALSE(health.ready);
  EXPECT_EQ(health.level, diagnostic_msgs::msg::DiagnosticStatus::STALE);
  EXPECT_NE(health.message.find(runtime_monitor::kTaskOrchestratorName), std::string::npos);

  // It is present in the report, named, and marked unseen rather than absent.
  const auto found = std::find_if(
    health.components.begin(), health.components.end(),
    [](const runtime_monitor::ComponentHealth & component) {
      return component.name == runtime_monitor::kTaskOrchestratorName;
    });
  ASSERT_NE(found, health.components.end());
  EXPECT_FALSE(found->seen);
}

TEST(HealthAggregatorTest, EveryDefaultComponentIsRequired)
{
  runtime_monitor::HealthAggregator aggregator(std::chrono::milliseconds(2000));
  const runtime_monitor::SteadyTime now{};
  publish_all(aggregator, diagnostic_msgs::msg::DiagnosticStatus::OK, now);
  EXPECT_TRUE(aggregator.evaluate(now).ready);

  // Dropping any single one of them is enough to lose readiness.
  for (const auto & name : runtime_monitor::default_components()) {
    runtime_monitor::HealthAggregator one_missing(std::chrono::milliseconds(2000));
    publish_all(one_missing, diagnostic_msgs::msg::DiagnosticStatus::OK, now, name);
    EXPECT_FALSE(one_missing.evaluate(now).ready) << name << " was not required";
  }
}

TEST(HealthAggregatorTest, TheComponentListIsConfiguration)
{
  runtime_monitor::HealthAggregator aggregator(
    std::chrono::milliseconds(2000), {runtime_monitor::kRuntimeGatewayName});
  const runtime_monitor::SteadyTime now{};

  // A component that is not configured is not adopted, even at ERROR.
  EXPECT_FALSE(aggregator.update(
    status(runtime_monitor::kDeviceBridgeName, diagnostic_msgs::msg::DiagnosticStatus::ERROR),
    now));
  EXPECT_FALSE(aggregator.evaluate(now).ready);

  EXPECT_TRUE(aggregator.update(
    status(runtime_monitor::kRuntimeGatewayName, diagnostic_msgs::msg::DiagnosticStatus::OK),
    now));
  EXPECT_TRUE(aggregator.evaluate(now).ready);
}

TEST(HealthAggregatorTest, AnEmptyComponentListIsRejected)
{
  EXPECT_THROW(
    runtime_monitor::HealthAggregator(std::chrono::milliseconds(2000), {}),
    std::invalid_argument);
}
