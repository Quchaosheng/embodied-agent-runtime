#include "robot_task_interfaces/error_code.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>

using robot_task_interfaces::error_code::Layer;

TEST(ErrorCodeTest, NoTwoConstantsShareAValue)
{
  std::size_t count = 0;
  const auto * entries = robot_task_interfaces::error_code::table(count);
  ASSERT_GT(count, 0u);

  std::set<std::uint16_t> seen;
  for (std::size_t index = 0; index < count; ++index) {
    EXPECT_TRUE(seen.insert(entries[index].code).second)
      << "duplicate error code " << entries[index].code << " at "
      << entries[index].name;
  }
}

TEST(ErrorCodeTest, EveryConstantSitsInsideItsOwnLayerRange)
{
  std::size_t count = 0;
  const auto * entries = robot_task_interfaces::error_code::table(count);
  for (std::size_t index = 0; index < count; ++index) {
    EXPECT_EQ(
      robot_task_interfaces::error_code::layer_of(entries[index].code),
      entries[index].layer)
      << entries[index].name << " = " << entries[index].code
      << " is reported as layer "
      << robot_task_interfaces::error_code::to_string(
      robot_task_interfaces::error_code::layer_of(entries[index].code));
  }
}

TEST(ErrorCodeTest, TheDeviceRejectionRangeCannotReachAnyOtherLayer)
{
  // This is the collision the table exists to remove: `300 + result_code` used to
  // run to 555 and swallow the gateway's 400 and 500 codes.
  using namespace robot_task_interfaces::error_code;
  EXPECT_GT(kDeviceRejectedBase, kGatewayGoalResponseTimeout);
  EXPECT_GT(kDeviceRejectedBase, kDeviceCompletionTimeout);
  EXPECT_LE(kDeviceRejectedMax, 1255u);

  for (std::uint32_t result_code = 0; result_code <= kDeviceResultCodeMax; ++result_code) {
    const auto code = device_rejected(static_cast<std::uint8_t>(result_code));
    EXPECT_EQ(layer_of(code), Layer::kDevice)
      << "result_code " << result_code << " produced " << code;
  }
}

TEST(ErrorCodeTest, TheOldCollisionIsGone)
{
  using namespace robot_task_interfaces::error_code;
  // 205/206 used to mean both "device refused the STOP"/"completion timeout" and
  // "orchestrator shutting down"/"precondition failed".
  EXPECT_EQ(layer_of(kOrchestratorShutdown), Layer::kOrchestrator);
  EXPECT_EQ(layer_of(kOrchestratorPreconditionFailed), Layer::kOrchestrator);
  EXPECT_NE(layer_of(kDeviceStopRejected), Layer::kOrchestrator);
  EXPECT_NE(layer_of(kDeviceCompletionTimeout), Layer::kOrchestrator);
  EXPECT_NE(kDeviceStopRejected, kOrchestratorShutdown);
  EXPECT_NE(kDeviceCompletionTimeout, kOrchestratorPreconditionFailed);

  // 400/409/500/503/504 are gateway-only, and no device code can produce them.
  EXPECT_EQ(layer_of(kGatewayNotAllowlisted), Layer::kGateway);
  EXPECT_EQ(layer_of(kGatewayConflict), Layer::kGateway);
  EXPECT_EQ(layer_of(kGatewayCancelFailed), Layer::kGateway);
  EXPECT_EQ(layer_of(kGatewayUnavailable), Layer::kGateway);
  for (std::uint32_t result_code = 0; result_code <= kDeviceResultCodeMax; ++result_code) {
    const auto code = device_rejected(static_cast<std::uint8_t>(result_code));
    EXPECT_NE(code, kGatewayNotAllowlisted);
    EXPECT_NE(code, kGatewayUnavailable);
  }
}

TEST(ErrorCodeTest, AnUndefinedCodeIsReportedAsNoLayer)
{
  using namespace robot_task_interfaces::error_code;
  EXPECT_EQ(layer_of(0), Layer::kNone);
  EXPECT_EQ(layer_of(99), Layer::kNone);
  EXPECT_EQ(layer_of(600), Layer::kNone);
  EXPECT_EQ(layer_of(999), Layer::kNone);
  EXPECT_EQ(layer_of(1256), Layer::kNone);
}
