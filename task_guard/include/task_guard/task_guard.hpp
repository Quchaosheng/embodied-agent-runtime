#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

#include "task_contract/types.hpp"

namespace task_guard {

struct GuardPolicy {
  std::uint8_t contract_version{task_contract::kContractVersion};
  std::uint32_t min_deadline_s{task_contract::kMinDeadlineS};
  std::uint32_t max_deadline_s{task_contract::kMaxDeadlineS};
  std::unordered_set<std::string> allowed_targets{"dock", "workbench", "home"};
  std::uint32_t max_navigation_attempts{2};
  std::uint32_t cancel_confirmation_timeout_ms{500};
};

GuardPolicy load_policy_from_yaml(const std::string& path);

struct RobotContext {
  bool localization_ready{false};
  bool navigation_ready{false};
  bool task_active{false};
};

struct ValidationResult {
  task_contract::ErrorCode code{task_contract::ErrorCode::kOk};
  std::string detail;

  bool accepted() const;
};

class TaskGuard {
 public:
  explicit TaskGuard(GuardPolicy policy = {});

  ValidationResult validate(const task_contract::TaskRequest& request,
                            const RobotContext& context) const;

 private:
  GuardPolicy policy_;
};

}  // namespace task_guard
