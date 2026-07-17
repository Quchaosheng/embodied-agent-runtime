#include <gtest/gtest.h>

#include <stdexcept>

#include "task_guard/task_guard.hpp"

namespace {

task_contract::TaskRequest valid_request() {
  return {
      task_contract::kContractVersion,
      task_contract::TaskAction::kNavigate,
      "contract-test",
      "dock",
      90,
  };
}

task_guard::RobotContext ready_context() {
  return {
      true,
      true,
      false,
  };
}

TEST(TaskGuardTest, AcceptsAllowedTaskForReadyRobot) {
  task_guard::TaskGuard guard;

  const auto result = guard.validate(valid_request(), ready_context());

  EXPECT_TRUE(result.accepted());
  EXPECT_EQ(result.code, task_contract::ErrorCode::kOk);
}

TEST(TaskGuardTest, RejectsUnsupportedContractVersion) {
  task_guard::TaskGuard guard;
  auto request = valid_request();
  request.contract_version = 2;

  const auto result = guard.validate(request, ready_context());

  EXPECT_EQ(result.code, task_contract::ErrorCode::kInvalidContract);
}

TEST(TaskGuardTest, RejectsUnknownTarget) {
  task_guard::TaskGuard guard;
  auto request = valid_request();
  request.target = "laboratory";

  const auto result = guard.validate(request, ready_context());

  EXPECT_EQ(result.code, task_contract::ErrorCode::kUnknownTarget);
}

TEST(TaskGuardTest, RejectsContractTargetExcludedByPolicy) {
  task_guard::GuardPolicy policy;
  policy.allowed_targets = {"home"};
  task_guard::TaskGuard guard(policy);

  const auto result = guard.validate(valid_request(), ready_context());

  EXPECT_EQ(result.code, task_contract::ErrorCode::kUnknownTarget);
}

TEST(TaskGuardTest, RejectsDeadlineOutsidePolicy) {
  task_guard::TaskGuard guard;
  auto request = valid_request();
  request.deadline_s = 91;

  const auto result = guard.validate(request, ready_context());

  EXPECT_EQ(result.code, task_contract::ErrorCode::kDeadlineInvalid);
}

TEST(TaskGuardTest, RejectsTaskWhenRobotIsBusy) {
  task_guard::TaskGuard guard;
  auto context = ready_context();
  context.task_active = true;

  const auto result = guard.validate(valid_request(), context);

  EXPECT_EQ(result.code, task_contract::ErrorCode::kTaskAlreadyRunning);
}

TEST(TaskGuardTest, RejectsTaskWhenLocalizationIsUnavailable) {
  task_guard::TaskGuard guard;
  auto context = ready_context();
  context.localization_ready = false;

  const auto result = guard.validate(valid_request(), context);

  EXPECT_EQ(result.code, task_contract::ErrorCode::kRobotNotReady);
}

TEST(TaskGuardTest, RejectsTaskWhenNavigationIsUnavailable) {
  task_guard::TaskGuard guard;
  auto context = ready_context();
  context.navigation_ready = false;

  const auto result = guard.validate(valid_request(), context);

  EXPECT_EQ(result.code, task_contract::ErrorCode::kNavigationNotReady);
}

TEST(TaskGuardTest, RejectsTaskWhenDeviceHeartbeatIsUnavailable) {
  task_guard::TaskGuard guard;
  auto context = ready_context();
  context.device_ready = false;

  const auto result = guard.validate(valid_request(), context);

  EXPECT_EQ(result.code, task_contract::ErrorCode::kDeviceNotReady);
}

TEST(GuardPolicyLoaderTest, LoadsVersionControlledPolicy) {
  const auto policy = task_guard::load_policy_from_yaml(TASK_GUARD_TEST_POLICY_PATH);

  EXPECT_EQ(policy.contract_version, task_contract::kContractVersion);
  EXPECT_EQ(policy.min_deadline_s, task_contract::kMinDeadlineS);
  EXPECT_EQ(policy.max_deadline_s, task_contract::kMaxDeadlineS);
  EXPECT_EQ(policy.allowed_targets.size(), 3U);
  EXPECT_EQ(policy.allowed_targets.count("dock"), 1U);
  EXPECT_EQ(policy.allowed_targets.count("workbench"), 1U);
  EXPECT_EQ(policy.allowed_targets.count("home"), 1U);
  EXPECT_EQ(policy.max_navigation_attempts, 2U);
  EXPECT_EQ(policy.cancel_confirmation_timeout_ms, 500U);
}

TEST(GuardPolicyLoaderTest, RejectsPolicyThatRelaxesContractDeadline) {
  EXPECT_THROW(task_guard::load_policy_from_yaml(TASK_GUARD_TEST_INVALID_POLICY_PATH),
               std::runtime_error);
}

TEST(GuardPolicyLoaderTest, RejectsPolicyThatExpandsContractTargets) {
  EXPECT_THROW(task_guard::load_policy_from_yaml(TASK_GUARD_TEST_INVALID_TARGET_POLICY_PATH),
               std::runtime_error);
}

}  // namespace
