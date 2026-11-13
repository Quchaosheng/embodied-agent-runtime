#include "runtime_can/in_memory_transport.hpp"

#include <utility>

namespace runtime_can
{

InMemoryTransport::InMemoryTransport(std::size_t inbound_capacity)
: inbound_capacity_(inbound_capacity == 0 ? 1 : inbound_capacity)
{
}

void InMemoryTransport::set_responder(Responder responder)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  responder_ = std::move(responder);
}

bool InMemoryTransport::inject(const RawFrame & frame)
{
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (inbound_.size() >= inbound_capacity_) {
      ++dropped_;
      return false;
    }
    inbound_.push_back(Inbound{false, frame, BusState{}});
  }
  frame_available_.notify_all();
  return true;
}

bool InMemoryTransport::inject_bus_state(const BusState & state)
{
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (inbound_.size() >= inbound_capacity_) {
      ++dropped_;
      return false;
    }
    inbound_.push_back(Inbound{true, RawFrame{}, state});
  }
  frame_available_.notify_all();
  return true;
}

bool InMemoryTransport::fail_send(const std::string & error)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  send_error_ = error;
  return true;
}

bool InMemoryTransport::fail_receive(const std::string & error)
{
  const std::lock_guard<std::mutex> lock(mutex_);
  receive_error_ = error;
  return true;
}

void InMemoryTransport::clear_errors()
{
  const std::lock_guard<std::mutex> lock(mutex_);
  send_error_.clear();
  receive_error_.clear();
}

std::vector<RawFrame> InMemoryTransport::sent_frames() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return sent_;
}

std::size_t InMemoryTransport::dropped_frames() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return dropped_;
}

bool InMemoryTransport::is_open() const
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return open_;
}

bool InMemoryTransport::open(const std::string & interface_name, std::string & error)
{
  error.clear();
  const std::lock_guard<std::mutex> lock(mutex_);
  open_ = true;
  interface_name_ = interface_name;
  return true;
}

bool InMemoryTransport::set_filter(std::uint32_t, std::string & error)
{
  error.clear();
  return true;
}

bool InMemoryTransport::set_filter(std::uint32_t, std::uint32_t, std::string & error)
{
  error.clear();
  return true;
}

bool InMemoryTransport::set_kind_filter(
  const std::vector<std::uint32_t> &, std::uint32_t, std::string & error)
{
  // Filtering is the socket's job; a fake delivers what a test injects.
  error.clear();
  return true;
}

bool InMemoryTransport::send(const RawFrame & frame, std::string & error) const
{
  Responder responder;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) {
      error = "CAN socket is not open";
      return false;
    }
    if (!send_error_.empty()) {
      error = send_error_;
      return false;
    }
    sent_.push_back(frame);
    // Copied out so the responder runs without the lock: it typically injects a
    // response, which re-takes the same mutex.
    responder = responder_;
  }
  if (responder) {
    responder(frame);
  }
  return true;
}

ReceiveStatus InMemoryTransport::receive(
  RawFrame & frame, std::chrono::milliseconds timeout, std::string & error,
  BusState * bus_state) const
{
  std::unique_lock<std::mutex> lock(mutex_);
  if (!open_) {
    error = "CAN socket is not open";
    return ReceiveStatus::kError;
  }
  if (!receive_error_.empty()) {
    error = receive_error_;
    return ReceiveStatus::kError;
  }
  if (inbound_.empty()) {
    frame_available_.wait_for(lock, timeout, [this]() {return !inbound_.empty();});
    if (inbound_.empty()) {
      // The injected error can arrive while waiting, so it is re-checked here.
      if (!receive_error_.empty()) {
        error = receive_error_;
        return ReceiveStatus::kError;
      }
      return ReceiveStatus::kTimeout;
    }
  }

  auto entry = inbound_.front();
  inbound_.pop_front();
  lock.unlock();
  if (entry.is_bus_state) {
    if (bus_state == nullptr) {
      error = "unsupported error CAN frame";
      return ReceiveStatus::kError;
    }
    *bus_state = entry.bus_state;
    return ReceiveStatus::kBusState;
  }
  frame = entry.frame;
  return ReceiveStatus::kFrame;
}

void InMemoryTransport::close()
{
  const std::lock_guard<std::mutex> lock(mutex_);
  open_ = false;
}

}
