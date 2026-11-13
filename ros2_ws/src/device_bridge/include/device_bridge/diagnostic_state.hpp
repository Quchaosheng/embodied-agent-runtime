#pragma once

#include <diagnostic_msgs/msg/diagnostic_status.hpp>

#include <cstdint>
#include <mutex>
#include <string>

namespace device_bridge
{

class DeviceDiagnosticState
{
public:
  void begin_command(std::uint16_t command_id);
  void mark_sending();
  void mark_waiting_ack();
  void record_retry();
  void mark_stopping();
  void record_success();
  void record_canceled();
  void record_ack_timeout(std::uint16_t error_code);
  void record_device_fault(std::uint16_t error_code);
  void record_stop_failure(std::uint16_t error_code);
  void record_transport_error(std::uint16_t error_code);
  // Bus-off arrives from the controller, not from a command, so it is its own
  // latch rather than another transport error: only re-opening the interface
  // clears it.
  void record_bus_off();
  diagnostic_msgs::msg::DiagnosticStatus snapshot(bool socket_ready) const;
  // The link counters have no home in the phase machine, so they are passed in
  // rather than duplicated in another member.
  diagnostic_msgs::msg::DiagnosticStatus snapshot(
    bool socket_ready, std::uint64_t unmatched_frames, std::uint64_t overflow_frames,
    std::uint64_t reconnects) const;

private:
  enum class DevicePhase
  {
    kIdle,
    kSending,
    kWaitingAck,
    kRetrying,
    kStopping
  };

  mutable std::mutex mutex_;
  DevicePhase phase_{DevicePhase::kIdle};
  std::uint16_t active_command_id_{0};
  std::uint16_t last_error_code_{0};
  std::uint64_t retry_count_{0};
  std::uint64_t ack_timeout_count_{0};
  std::uint64_t device_fault_count_{0};
  std::uint64_t stop_failure_count_{0};
  std::uint64_t bus_off_count_{0};
  bool error_latched_{false};
};

}
