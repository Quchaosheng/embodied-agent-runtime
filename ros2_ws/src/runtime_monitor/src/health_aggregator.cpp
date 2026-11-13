#include "runtime_monitor/health_aggregator.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace runtime_monitor
{
HealthAggregator::HealthAggregator(
  const std::chrono::milliseconds stale_timeout, std::vector<std::string> components)
: stale_timeout_(stale_timeout), components_(std::move(components)),
  stored_(components_.size())
{
  if (stale_timeout_.count() <= 0) {
    throw std::invalid_argument("stale_timeout must be positive");
  }
  // An empty component list would make "everything is fresh" vacuously true, which
  // is the same failure the hard-coded pair had in the other direction.
  if (components_.empty()) {
    throw std::invalid_argument("at least one health component must be configured");
  }
}

const std::vector<std::string> & HealthAggregator::components() const
{
  return components_;
}

bool HealthAggregator::update(
  const diagnostic_msgs::msg::DiagnosticStatus & status, const SteadyTime received_at)
{
  const auto found = std::find(components_.begin(), components_.end(), status.name);
  if (found == components_.end()) {
    // A status for a component that is not configured is not silently adopted:
    // the configured list is what "ready" is defined against.
    return false;
  }
  auto & target = stored_[static_cast<std::size_t>(found - components_.begin())];
  target.seen = true;
  target.level = status.level <= diagnostic_msgs::msg::DiagnosticStatus::STALE ?
    status.level : diagnostic_msgs::msg::DiagnosticStatus::ERROR;
  target.message = status.message;
  target.received_at = received_at;
  return true;
}

ComponentHealth HealthAggregator::evaluate_component(
  const std::string & name, const StoredStatus & stored, const SteadyTime now) const
{
  ComponentHealth health;
  health.name = name;
  if (!stored.seen) {
    return health;
  }
  const auto raw_age = std::chrono::duration_cast<std::chrono::milliseconds>(
    now - stored.received_at);
  const auto age = std::max<std::int64_t>(raw_age.count(), 0);
  health.seen = true;
  health.age_ms = age;
  if (age > stale_timeout_.count()) {
    health.level = diagnostic_msgs::msg::DiagnosticStatus::STALE;
    health.message = "diagnostics stale";
    return health;
  }
  health.level = stored.level;
  health.message = stored.message;
  return health;
}

SystemHealth HealthAggregator::evaluate(const SteadyTime now) const
{
  SystemHealth health;
  health.components.reserve(components_.size());
  for (std::size_t index = 0; index < components_.size(); ++index) {
    health.components.push_back(
      evaluate_component(components_[index], stored_[index], now));
  }
  if (!health.components.empty()) {
    health.device_bridge = health.components[0];
  }
  if (health.components.size() > 1) {
    health.task_executor = health.components[1];
  }

  bool any_missing = false;
  bool any_error = false;
  bool any_warn = false;
  std::string first_missing;
  for (const auto & component : health.components) {
    if (!component.seen || component.level == diagnostic_msgs::msg::DiagnosticStatus::STALE) {
      any_missing = true;
      if (first_missing.empty()) {
        first_missing = component.name;
      }
    } else if (component.level == diagnostic_msgs::msg::DiagnosticStatus::ERROR) {
      any_error = true;
    } else if (component.level == diagnostic_msgs::msg::DiagnosticStatus::WARN) {
      any_warn = true;
    }
  }

  if (any_missing) {
    health.level = diagnostic_msgs::msg::DiagnosticStatus::STALE;
    health.message = "core diagnostics missing or stale: " + first_missing;
  } else if (any_error) {
    health.level = diagnostic_msgs::msg::DiagnosticStatus::ERROR;
    health.message = "core component error";
  } else if (any_warn) {
    health.level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
    health.message = "runtime degraded";
    health.ready = true;
  } else {
    health.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
    health.message = "runtime ready";
    health.ready = true;
  }
  return health;
}
}  // namespace runtime_monitor
