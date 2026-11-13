#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_status.hpp"

namespace runtime_monitor
{
inline constexpr char kDeviceBridgeName[] = "runtime/device_bridge";
inline constexpr char kTaskExecutorName[] = "runtime/task_executor";
inline constexpr char kTaskOrchestratorName[] = "runtime/task_orchestrator";
inline constexpr char kRuntimeGatewayName[] = "runtime/gateway";
inline constexpr char kRuntimeHistoryName[] = "runtime/history";
inline constexpr char kSystemName[] = "runtime/system";

// What the runtime requires before it reports ready. The two names it used to
// hard-code are only part of it: task_orchestrator, runtime_gateway and
// runtime_history can be dead or wedged while the system still said "ready",
// because the aggregator dropped their diagnostics instead of reporting them.
inline const std::vector<std::string> & default_components()
{
  static const std::vector<std::string> names{
    kDeviceBridgeName, kTaskExecutorName, kTaskOrchestratorName, kRuntimeGatewayName,
    kRuntimeHistoryName};
  return names;
}

using SteadyTime = std::chrono::steady_clock::time_point;

struct ComponentHealth
{
  std::string name;
  bool seen{false};
  std::uint8_t level{diagnostic_msgs::msg::DiagnosticStatus::STALE};
  std::string message{"not received"};
  std::int64_t age_ms{-1};
};

struct SystemHealth
{
  bool ready{false};
  std::uint8_t level{diagnostic_msgs::msg::DiagnosticStatus::STALE};
  std::string message{"waiting for core diagnostics"};
  // One entry per configured component, in configuration order. A component that
  // was configured but never published is present and not seen, rather than
  // silently absent: "missing" is a reportable state, not an omission.
  std::vector<ComponentHealth> components;
  // The two long-standing fields, kept so existing consumers and their key names
  // keep working. They mirror the first two configured entries.
  ComponentHealth device_bridge;
  ComponentHealth task_executor;
};

class HealthAggregator
{
public:
  explicit HealthAggregator(
    std::chrono::milliseconds stale_timeout,
    std::vector<std::string> components = default_components());
  bool update(const diagnostic_msgs::msg::DiagnosticStatus & status, SteadyTime received_at);
  SystemHealth evaluate(SteadyTime now) const;
  const std::vector<std::string> & components() const;

private:
  struct StoredStatus
  {
    bool seen{false};
    std::uint8_t level{diagnostic_msgs::msg::DiagnosticStatus::STALE};
    std::string message{"not received"};
    SteadyTime received_at{};
  };

  ComponentHealth evaluate_component(
    const std::string & name, const StoredStatus & stored, SteadyTime now) const;

  std::chrono::milliseconds stale_timeout_;
  std::vector<std::string> components_;
  std::vector<StoredStatus> stored_;
};
}  // namespace runtime_monitor
