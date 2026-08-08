#include "task_executor/target_validator.hpp"

#include <stdexcept>

namespace task_executor
{

TargetValidator::TargetValidator(const std::vector<std::string> & targets)
: targets_(targets.begin(), targets.end())
{
  targets_.erase("");
  if (targets_.empty()) {
    throw std::invalid_argument(
      "target allowlist must contain at least one non-empty target id");
  }
}

bool TargetValidator::is_known(std::string_view target_id) const
{
  return targets_.find(std::string(target_id)) != targets_.end();
}

std::size_t TargetValidator::size() const
{
  return targets_.size();
}

}
