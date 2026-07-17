#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace device_bridge {

class HeartbeatMonitor {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  HeartbeatMonitor(std::uint32_t can_id, std::chrono::milliseconds timeout);

  bool observe(std::uint32_t can_id, const std::uint8_t* data, std::size_t size, TimePoint now);
  bool ready(TimePoint now) const;
  std::chrono::milliseconds age(TimePoint now) const;
  std::uint64_t accepted_frames() const;
  std::uint64_t rejected_frames() const;

 private:
  std::uint32_t can_id_;
  std::chrono::milliseconds timeout_;
  bool observed_{false};
  bool controller_ready_{false};
  TimePoint last_heartbeat_{};
  std::uint64_t accepted_frames_{0};
  std::uint64_t rejected_frames_{0};
};

}  // namespace device_bridge
