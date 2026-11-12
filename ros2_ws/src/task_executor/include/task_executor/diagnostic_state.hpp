#pragma once

#include <diagnostic_msgs/msg/diagnostic_status.hpp>

#include <cstdint>
#include <mutex>
#include <string>

namespace task_executor
{

class TaskDiagnosticState
{
public:
  void set_bridge_ready(bool ready);
  void begin_task(const std::string & task_id);
  void mark_validating();
  void mark_running();
  void mark_waiting_ack();
  void mark_recovering();
  void record_success(const std::string & outcome);
  void record_canceled();
  void record_failure(const std::string & outcome, std::uint16_t error_code);
  // Clears the latched failure so the runtime can report ready again. The last
  // outcome and error code stay: they are what the failure was, and erasing
  // them would make a reset look like a task that never failed.
  bool clear_latched_failure();
  bool failure_latched() const;
  diagnostic_msgs::msg::DiagnosticStatus snapshot() const;

private:
  enum class TaskPhase
  {
    kIdle,
    kValidating,
    kRunning,
    kWaitingAck,
    kRecovering
  };

  mutable std::mutex mutex_;
  TaskPhase phase_{TaskPhase::kIdle};
  std::string active_task_id_;
  std::string last_outcome_{"NONE"};
  std::uint16_t last_error_code_{0};
  bool bridge_ready_{false};
  bool failure_latched_{false};
};

}
