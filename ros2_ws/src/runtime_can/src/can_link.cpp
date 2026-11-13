#include "runtime_can/can_link.hpp"

#include <algorithm>
#include <utility>

namespace runtime_can
{

CanLink::CanLink(
  std::shared_ptr<Transport> transport, std::chrono::milliseconds poll_timeout,
  std::chrono::milliseconds reopen_interval)
: transport_(std::move(transport)),
  poll_timeout_(poll_timeout),
  reopen_interval_(reopen_interval)
{
}

CanLink::~CanLink()
{
  stop();
}

bool CanLink::start(
  const std::string & interface_name, std::uint32_t response_frame_id,
  std::uint32_t status_frame_id, std::string & error)
{
  stop();
  interface_name_ = interface_name;
  response_frame_id_ = response_frame_id;
  status_frame_id_ = status_frame_id;

  if (!transport_->open(interface_name, error)) {
    return false;
  }
  if (!transport_->set_filter(response_frame_id, status_frame_id, error)) {
    transport_->close();
    return false;
  }
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    open_ = true;
    transport_error_.clear();
  }
  bus_off_latched_ = false;
  running_ = true;
  thread_ = std::thread([this]() {receive_loop();});
  return true;
}

void CanLink::stop()
{
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    open_ = false;
    last_response_.reset();
    last_status_.reset();
  }
  received_.notify_all();
  transport_->close();
}

bool CanLink::send(const RawFrame & frame, std::string & error)
{
  if (!transport_->is_open()) {
    error = "CAN socket is not open";
    return false;
  }
  return transport_->send(frame, error);
}

bool CanLink::is_open() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return open_;
}

std::string CanLink::interface_name() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return interface_name_;
}

BusState CanLink::bus_state() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return bus_state_;
}

bool CanLink::bus_off_latched() const
{
  return bus_off_latched_;
}

std::uint64_t CanLink::unmatched_frames() const
{
  return unmatched_frames_;
}

std::uint64_t CanLink::overflow_frames() const
{
  return overflow_frames_;
}

std::uint64_t CanLink::reconnects() const
{
  return reconnects_;
}

bool CanLink::reopen(std::string & error)
{
  transport_->close();
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    open_ = false;
  }
  if (!transport_->open(interface_name_, error)) {
    return false;
  }
  if (!transport_->set_filter(response_frame_id_, status_frame_id_, error)) {
    transport_->close();
    return false;
  }
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    open_ = true;
    transport_error_.clear();
    bus_state_ = BusState{};
  }
  bus_off_latched_ = false;
  return true;
}

void CanLink::handle_bus_state(const BusState & state)
{
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    bus_state_ = state;
  }
  // Bus-off is latched because recovery is an explicit, observable event: the
  // socket is re-opened below and the latch clears only then.
  if (state.bus_off) {
    bus_off_latched_ = true;
  }
  if (state.rx_overflow) {
    // The kernel drops the newest frames on overflow, so this counter is the only
    // evidence that an ACK may have been lost before it was ever read.
    ++overflow_frames_;
  }
  received_.notify_all();
}

void CanLink::handle_frame(const RawFrame & frame)
{
  bool matched = false;
  // Compared against the ids this link was started with, not the global v1
  // constants: with sessions the actual id depends on the session.
  if (frame.id == response_frame_id_) {
    DecodeError decode_error;
    const auto response = decode_response(frame, decode_error);
    if (response) {
      {
        const std::lock_guard<std::mutex> lock(mutex_);
        last_response_ = *response;
      }
      matched = true;
      received_.notify_all();
    }
  } else if (frame.id == status_frame_id_) {
    DecodeError decode_error;
    const auto status = decode_status(frame, decode_error);
    if (status) {
      {
        const std::lock_guard<std::mutex> lock(mutex_);
        last_status_ = *status;
      }
      matched = true;
      received_.notify_all();
    }
  }
  if (!matched) {
    // Counted rather than logged: while idle this is the normal way a late frame
    // for a superseded command leaves the system, and one line per frame in the
    // receive path is its own defect.
    ++unmatched_frames_;
  }
}

void CanLink::receive_loop()
{
  while (running_) {
    RawFrame frame;
    std::string error;
    BusState state;
    const auto status = transport_->receive(frame, poll_timeout_, error, &state);
    if (!running_) {
      break;
    }
    switch (status) {
      case ReceiveStatus::kFrame:
        handle_frame(frame);
        break;
      case ReceiveStatus::kBusState:
        handle_bus_state(state);
        // A controller that is off-bus cannot carry the next command, and the
        // kernel does not restart it for a CAN_RAW socket on its own. Recovery is
        // re-opening the interface, which is also what a downed link needs.
        if (state.bus_off) {
          std::this_thread::sleep_for(reopen_interval_);
          if (!running_) {
            break;
          }
          std::string reopen_error;
          if (reopen(reopen_error)) {
            ++reconnects_;
            received_.notify_all();
          }
        }
        break;
      case ReceiveStatus::kTimeout:
        break;
      case ReceiveStatus::kError:
        {
          const std::lock_guard<std::mutex> lock(mutex_);
          transport_error_ = error;
          open_ = false;
        }
        received_.notify_all();
        // An interface that went down is retried here; without this the node kept
        // a dead socket until it was restarted.
        std::this_thread::sleep_for(reopen_interval_);
        if (!running_) {
          break;
        }
        std::string reopen_error;
        if (reopen(reopen_error)) {
          ++reconnects_;
          received_.notify_all();
        }
        break;
    }
  }
}

CanLink::Wait CanLink::await_response(
  std::uint16_t command_id, std::chrono::milliseconds timeout,
  const CancelCheck & canceled, Response & response, std::string & error)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::unique_lock<std::mutex> lock(mutex_);
  while (true) {
    if (canceled && canceled()) {
      return Wait::kCanceled;
    }
    if (!transport_error_.empty()) {
      error = transport_error_;
      return Wait::kTransportError;
    }
    if (last_response_) {
      if (last_response_->command_id == command_id) {
        response = *last_response_;
        last_response_.reset();
        return Wait::kReceived;
      }
      // Belongs to another transaction; dropped so it cannot settle this one.
      last_response_.reset();
      ++unmatched_frames_;
    }
    if (bus_off_latched_) {
      return Wait::kBusOff;
    }
    if (!running_) {
      error = "bridge is shutting down";
      return Wait::kShutdown;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      return Wait::kTimeout;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    // A cancel is only observed through the poll interval, so it is bounded by
    // that interval rather than by the full command timeout.
    received_.wait_for(lock, std::min(remaining, poll_timeout_));
  }
}

CanLink::Wait CanLink::await_status(
  std::uint16_t command_id, std::chrono::milliseconds timeout,
  const CancelCheck & canceled, StatusFrame & status, std::string & error)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::unique_lock<std::mutex> lock(mutex_);
  while (true) {
    if (canceled && canceled()) {
      return Wait::kCanceled;
    }
    if (!transport_error_.empty()) {
      error = transport_error_;
      return Wait::kTransportError;
    }
    if (last_status_) {
      if (last_status_->command_id == command_id) {
        status = *last_status_;
        last_status_.reset();
        return Wait::kReceived;
      }
      last_status_.reset();
      ++unmatched_frames_;
    }
    if (bus_off_latched_) {
      return Wait::kBusOff;
    }
    if (!running_) {
      error = "bridge is shutting down";
      return Wait::kShutdown;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      return Wait::kTimeout;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    received_.wait_for(lock, std::min(remaining, poll_timeout_));
  }
}

}
