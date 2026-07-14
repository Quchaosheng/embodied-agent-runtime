#include "task_guard/task_guard.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace task_guard {
namespace {

YAML::Node require_child(const YAML::Node& parent, const char* name) {
  const auto node = parent[name];
  if (!node) {
    throw std::runtime_error(std::string("missing required policy field: ") + name);
  }
  return node;
}

std::uint32_t parse_uint32(const YAML::Node& node, const char* name) {
  const auto value = node.as<std::uint64_t>();
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error(std::string("policy field is too large: ") + name);
  }
  return static_cast<std::uint32_t>(value);
}

}  // namespace

bool ValidationResult::accepted() const { return code == task_contract::ErrorCode::kOk; }

GuardPolicy load_policy_from_yaml(const std::string& path) {
  try {
    const auto root = YAML::LoadFile(path);
    if (!root.IsMap()) {
      throw std::runtime_error("task policy root must be a map");
    }

    GuardPolicy policy;

    const auto contract_version =
        parse_uint32(require_child(root, "contract_version"), "contract_version");
    if (contract_version != task_contract::kContractVersion) {
      throw std::runtime_error("task policy contract_version is unsupported");
    }
    policy.contract_version = static_cast<std::uint8_t>(contract_version);

    const auto deadline = require_child(root, "deadline");
    if (!deadline.IsMap()) {
      throw std::runtime_error("task policy deadline must be a map");
    }
    policy.min_deadline_s = parse_uint32(require_child(deadline, "min_s"), "deadline.min_s");
    policy.max_deadline_s = parse_uint32(require_child(deadline, "max_s"), "deadline.max_s");
    if (policy.min_deadline_s < task_contract::kMinDeadlineS ||
        policy.max_deadline_s > task_contract::kMaxDeadlineS ||
        policy.min_deadline_s > policy.max_deadline_s) {
      throw std::runtime_error("task policy deadline is outside contract bounds");
    }

    const auto allowed_targets = require_child(root, "allowed_targets");
    if (!allowed_targets.IsSequence() || allowed_targets.size() == 0) {
      throw std::runtime_error("task policy allowed_targets must be a non-empty sequence");
    }
    policy.allowed_targets.clear();
    for (const auto& target_node : allowed_targets) {
      const auto target = target_node.as<std::string>();
      if (target.empty()) {
        throw std::runtime_error("task policy target must not be empty");
      }
      if (std::find(task_contract::kContractTargets.begin(), task_contract::kContractTargets.end(),
                    target) == task_contract::kContractTargets.end()) {
        throw std::runtime_error("task policy target is outside the contract: " + target);
      }
      if (!policy.allowed_targets.insert(target).second) {
        throw std::runtime_error("task policy contains a duplicate target: " + target);
      }
    }

    const auto recovery = require_child(root, "recovery");
    if (!recovery.IsMap()) {
      throw std::runtime_error("task policy recovery must be a map");
    }
    policy.max_navigation_attempts = parse_uint32(
        require_child(recovery, "max_navigation_attempts"), "recovery.max_navigation_attempts");
    policy.cancel_confirmation_timeout_ms =
        parse_uint32(require_child(recovery, "cancel_confirmation_timeout_ms"),
                     "recovery.cancel_confirmation_timeout_ms");
    if (policy.max_navigation_attempts == 0 || policy.cancel_confirmation_timeout_ms == 0) {
      throw std::runtime_error("task policy recovery values must be greater than zero");
    }

    return policy;
  } catch (const YAML::Exception& error) {
    throw std::runtime_error("failed to load task policy '" + path + "': " + error.what());
  }
}

TaskGuard::TaskGuard(GuardPolicy policy) : policy_(std::move(policy)) {}

ValidationResult TaskGuard::validate(const task_contract::TaskRequest& request,
                                     const RobotContext& context) const {
  if (request.contract_version != policy_.contract_version) {
    return {task_contract::ErrorCode::kInvalidContract, "unsupported contract version"};
  }

  if (request.action != task_contract::TaskAction::kNavigate) {
    return {task_contract::ErrorCode::kUnsupportedAction, "action is not permitted"};
  }

  if (policy_.allowed_targets.count(request.target) == 0) {
    return {task_contract::ErrorCode::kUnknownTarget, "target is not permitted"};
  }

  if (request.deadline_s < policy_.min_deadline_s || request.deadline_s > policy_.max_deadline_s) {
    return {task_contract::ErrorCode::kDeadlineInvalid, "deadline is outside policy bounds"};
  }

  if (context.task_active) {
    return {task_contract::ErrorCode::kTaskAlreadyRunning, "a task is already active"};
  }

  if (!context.localization_ready) {
    return {task_contract::ErrorCode::kRobotNotReady, "robot localization is not ready"};
  }

  if (!context.navigation_ready) {
    return {task_contract::ErrorCode::kNavigationNotReady, "navigation is not ready"};
  }

  return {};
}

}  // namespace task_guard
