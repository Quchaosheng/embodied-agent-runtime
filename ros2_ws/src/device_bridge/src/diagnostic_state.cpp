#include "device_bridge/diagnostic_state.hpp"

#include <diagnostic_msgs/msg/key_value.hpp>

#include <string>

namespace
{

diagnostic_msgs::msg::KeyValue key_value(const std::string & key, const std::string & value)
{
  diagnostic_msgs::msg::KeyValue result;
  result.key = key;
  result.value = value;
  return result;
}

}

namespace device_bridge
{

void DeviceDiagnosticState::begin_command(const std::uint16_t command_id)
{
  std::lock_guard<std::mutex> lock(mutex_);
  active_command_id_ = command_id;
  phase_ = DevicePhase::kSending;
}

void DeviceDiagnosticState::mark_sending()
{
  std::lock_guard<std::mutex> lock(mutex_);
  phase_ = DevicePhase::kSending;
}

void DeviceDiagnosticState::mark_waiting_ack()
{
  std::lock_guard<std::mutex> lock(mutex_);
  phase_ = DevicePhase::kWaitingAck;
}

void DeviceDiagnosticState::record_retry()
{
  std::lock_guard<std::mutex> lock(mutex_);
  ++retry_count_;
  phase_ = DevicePhase::kRetrying;
}

void DeviceDiagnosticState::mark_stopping()
{
  std::lock_guard<std::mutex> lock(mutex_);
  phase_ = DevicePhase::kStopping;
}

void DeviceDiagnosticState::record_success()
{
  std::lock_guard<std::mutex> lock(mutex_);
  phase_ = DevicePhase::kIdle;
  active_command_id_ = 0;
  last_error_code_ = 0;
  error_latched_ = false;
}

void DeviceDiagnosticState::record_canceled()
{
  record_success();
}

void DeviceDiagnosticState::record_ack_timeout(const std::uint16_t error_code)
{
  std::lock_guard<std::mutex> lock(mutex_);
  ++ack_timeout_count_;
  phase_ = DevicePhase::kIdle;
  active_command_id_ = 0;
  last_error_code_ = error_code;
  error_latched_ = true;
}

void DeviceDiagnosticState::record_device_fault(const std::uint16_t error_code)
{
  std::lock_guard<std::mutex> lock(mutex_);
  ++device_fault_count_;
  phase_ = DevicePhase::kIdle;
  active_command_id_ = 0;
  last_error_code_ = error_code;
  error_latched_ = true;
}

void DeviceDiagnosticState::record_bus_off()
{
  std::lock_guard<std::mutex> lock(mutex_);
  ++bus_off_count_;
  phase_ = DevicePhase::kIdle;
  active_command_id_ = 0;
  error_latched_ = true;
}

void DeviceDiagnosticState::record_stop_failure(const std::uint16_t error_code)
{
  std::lock_guard<std::mutex> lock(mutex_);
  ++stop_failure_count_;
  phase_ = DevicePhase::kIdle;
  active_command_id_ = 0;
  last_error_code_ = error_code;
  error_latched_ = true;
}

void DeviceDiagnosticState::record_transport_error(const std::uint16_t error_code)
{
  std::lock_guard<std::mutex> lock(mutex_);
  phase_ = DevicePhase::kIdle;
  active_command_id_ = 0;
  last_error_code_ = error_code;
  error_latched_ = true;
}

diagnostic_msgs::msg::DiagnosticStatus DeviceDiagnosticState::snapshot(
  const bool socket_ready) const
{
  DevicePhase phase;
  std::uint16_t active_command_id;
  std::uint16_t last_error_code;
  std::uint64_t retry_count;
  std::uint64_t ack_timeout_count;
  std::uint64_t device_fault_count;
  std::uint64_t stop_failure_count;
  std::uint64_t bus_off_count;
  bool error_latched;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    phase = phase_;
    active_command_id = active_command_id_;
    last_error_code = last_error_code_;
    retry_count = retry_count_;
    ack_timeout_count = ack_timeout_count_;
    device_fault_count = device_fault_count_;
    stop_failure_count = stop_failure_count_;
    bus_off_count = bus_off_count_;
    error_latched = error_latched_;
  }

  const char * phase_name = "IDLE";
  switch (phase) {
    case DevicePhase::kIdle:
      phase_name = "IDLE";
      break;
    case DevicePhase::kSending:
      phase_name = "SENDING";
      break;
    case DevicePhase::kWaitingAck:
      phase_name = "WAITING_ACK";
      break;
    case DevicePhase::kRetrying:
      phase_name = "RETRYING";
      break;
    case DevicePhase::kStopping:
      phase_name = "STOPPING";
      break;
  }

  diagnostic_msgs::msg::DiagnosticStatus status;
  status.name = "runtime/device_bridge";
  status.hardware_id = "socketcan_bridge";
  if (!socket_ready) {
    status.level = diagnostic_msgs::msg::DiagnosticStatus::ERROR;
    status.message = "socket unavailable";
  } else if (error_latched) {
    status.level = diagnostic_msgs::msg::DiagnosticStatus::ERROR;
    status.message = "device bridge error";
  } else if (phase == DevicePhase::kRetrying) {
    status.level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
    status.message = "retrying command";
  } else {
    status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
    status.message = "ready";
  }
  status.values = {
    key_value("ready", socket_ready ? "true" : "false"),
    key_value("state", phase_name),
    key_value("active_command_id", std::to_string(active_command_id)),
    key_value("retry_count", std::to_string(retry_count)),
    key_value("ack_timeout_count", std::to_string(ack_timeout_count)),
    key_value("device_fault_count", std::to_string(device_fault_count)),
    key_value("stop_failure_count", std::to_string(stop_failure_count)),
    key_value("last_error_code", std::to_string(last_error_code)),
    key_value("bus_off_count", std::to_string(bus_off_count))};
  return status;
}

// Appends the link counters instead of replacing the base keys, so a consumer
// that reads the original eight keys keeps working.
diagnostic_msgs::msg::DiagnosticStatus DeviceDiagnosticState::snapshot(
  const bool socket_ready, const std::uint64_t unmatched_frames,
  const std::uint64_t overflow_frames, const std::uint64_t reconnects) const
{
  auto status = snapshot(socket_ready);
  status.values.push_back(key_value("unmatched_frames", std::to_string(unmatched_frames)));
  status.values.push_back(key_value("overflow_frames", std::to_string(overflow_frames)));
  status.values.push_back(key_value("reconnects", std::to_string(reconnects)));
  return status;
}

}
