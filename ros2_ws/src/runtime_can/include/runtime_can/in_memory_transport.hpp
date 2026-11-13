#pragma once

#include "runtime_can/protocol.hpp"
#include "runtime_can/transport.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace runtime_can
{

// A transport with no interface behind it. Frames sent are handed to an optional
// scripted responder; frames that responder (or a test) injects are delivered to
// receive(). Everything is bounded, because a fake that grows without limit turns
// a missing read into a memory leak instead of a visible backlog.
class InMemoryTransport : public Transport
{
public:
  // Called on the sending thread with the frame that was just sent.
  using Responder = std::function<void(const RawFrame &)>;

  explicit InMemoryTransport(std::size_t inbound_capacity = 256);

  void set_responder(Responder responder);
  // Queue a frame for the next receive(). Returns false when the bound is full,
  // which is the same condition a real socket reports by dropping the newest.
  bool inject(const RawFrame & frame);
  bool inject_bus_state(const BusState & state);
  bool fail_send(const std::string & error);
  bool fail_receive(const std::string & error);
  // Clears the injected send/receive errors, which is how a test says "the
  // interface came back" without constructing a new transport.
  void clear_errors();
  std::vector<RawFrame> sent_frames() const;
  std::size_t dropped_frames() const;
  bool is_open() const override;

  bool open(const std::string & interface_name, std::string & error) override;
  bool set_filter(std::uint32_t frame_id, std::string & error) override;
  bool set_filter(
    std::uint32_t first_frame_id, std::uint32_t second_frame_id, std::string & error) override;
  bool set_kind_filter(
    const std::vector<std::uint32_t> & frame_kinds, std::uint32_t kind_mask,
    std::string & error) override;
  bool send(const RawFrame & frame, std::string & error) const override;
  ReceiveStatus receive(
    RawFrame & frame, std::chrono::milliseconds timeout, std::string & error,
    BusState * bus_state = nullptr) const override;
  void close() override;

private:
  struct Inbound
  {
    bool is_bus_state{false};
    RawFrame frame;
    BusState bus_state;
  };

  std::size_t inbound_capacity_;
  mutable std::mutex mutex_;
  mutable std::condition_variable frame_available_;
  bool open_{false};
  std::string interface_name_;
  mutable std::deque<Inbound> inbound_;
  mutable std::vector<RawFrame> sent_;
  mutable std::size_t dropped_{0};
  std::string send_error_;
  std::string receive_error_;
  Responder responder_;
  // receive() is called from a worker thread that must not run the responder
  // under the lock while send() holds it, so sends unlock before responding.
  mutable std::mutex send_mutex_;
};

}
