#include "device_bridge/heartbeat_monitor.hpp"

#include <limits>
#include <stdexcept>

namespace device_bridge {

HeartbeatMonitor::HeartbeatMonitor(std::uint32_t can_id, std::chrono::milliseconds timeout)
    : can_id_(can_id), timeout_(timeout) {
  if (can_id_ > 0x7FFU) {
    throw std::invalid_argument("heartbeat CAN ID must be an 11-bit standard identifier");
  }
  if (timeout_ <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("heartbeat timeout must be positive");
  }
}

bool HeartbeatMonitor::observe(std::uint32_t can_id, const std::uint8_t* data, std::size_t size,
                               TimePoint now) {
  if (can_id != can_id_ || data == nullptr || size != 2U || data[0] != 1U ||
      (data[1] & 0xFEU) != 0U) {
    ++rejected_frames_;
    return false;
  }
  observed_ = true;
  controller_ready_ = (data[1] & 0x01U) != 0U;
  last_heartbeat_ = now;
  ++accepted_frames_;
  return true;
}

bool HeartbeatMonitor::ready(TimePoint now) const {
  return observed_ && controller_ready_ && now >= last_heartbeat_ &&
         now - last_heartbeat_ <= timeout_;
}

std::chrono::milliseconds HeartbeatMonitor::age(TimePoint now) const {
  if (!observed_) {
    return std::chrono::milliseconds::max();
  }
  if (now < last_heartbeat_) {
    return std::chrono::milliseconds::zero();
  }
  return std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_);
}

std::uint64_t HeartbeatMonitor::accepted_frames() const { return accepted_frames_; }

std::uint64_t HeartbeatMonitor::rejected_frames() const { return rejected_frames_; }

}  // namespace device_bridge
