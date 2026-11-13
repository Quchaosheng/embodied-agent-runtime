#pragma once

#include "runtime_can/protocol.hpp"
#include "runtime_can/transport.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace runtime_can
{

// Owns the socket for the whole life of the node.
//
// Before this existed the socket was read only inside a goal's two wait loops, so
// frames arriving while idle sat in the kernel queue and were read — and rejected
// as unmatched — by the next command. A resident thread drains the bus at all
// times and routes each frame to the command that is waiting for it, which is also
// the only way a heartbeat or a bus-off can be noticed at all.
class CanLink
{
public:
  enum class Wait
  {
    kReceived,
    kTimeout,
    // The caller's cancel arrived while waiting.
    kCanceled,
    kTransportError,
    // The controller reported bus-off; the frame was never a peer's.
    kBusOff,
    kShutdown,
  };

  using CancelCheck = std::function<bool()>;

  CanLink(
    std::shared_ptr<Transport> transport, std::chrono::milliseconds poll_timeout,
    std::chrono::milliseconds reopen_interval = std::chrono::milliseconds(1000));
  ~CanLink();

  CanLink(const CanLink &) = delete;
  CanLink & operator=(const CanLink &) = delete;

  // Opens the interface and starts the receive thread. The two frame ids are the
  // response and status ids the node cares about.
  bool start(
    const std::string & interface_name, std::uint32_t response_frame_id,
    std::uint32_t status_frame_id, std::string & error);
  void stop();
  bool send(const RawFrame & frame, std::string & error);
  bool is_open() const;
  std::string interface_name() const;

  Wait await_response(
    std::uint16_t command_id, std::chrono::milliseconds timeout,
    const CancelCheck & canceled, Response & response, std::string & error);
  Wait await_status(
    std::uint16_t command_id, std::chrono::milliseconds timeout,
    const CancelCheck & canceled, StatusFrame & status, std::string & error);

  BusState bus_state() const;
  // Latched: once the controller reports bus-off the flag stays set until the
  // interface is re-opened, because a recovered link is a different observation
  // than "nothing was wrong".
  bool bus_off_latched() const;
  std::uint64_t unmatched_frames() const;
  // Counted from the controller's own RX-overflow report: the kernel discards the
  // newest frames, so an overflow is a possible lost ACK, not a cosmetic warning.
  std::uint64_t overflow_frames() const;
  std::uint64_t reconnects() const;

private:
  void receive_loop();
  void handle_frame(const RawFrame & frame);
  void handle_bus_state(const BusState & state);
  bool reopen(std::string & error);

  std::shared_ptr<Transport> transport_;
  std::chrono::milliseconds poll_timeout_;
  std::chrono::milliseconds reopen_interval_;
  std::string interface_name_;
  std::uint32_t response_frame_id_{0};
  std::uint32_t status_frame_id_{0};

  mutable std::mutex mutex_;
  std::condition_variable received_;
  std::optional<Response> last_response_;
  std::optional<StatusFrame> last_status_;
  BusState bus_state_;
  std::string transport_error_;
  bool open_{false};
  bool starting_{false};

  std::atomic_bool running_{false};
  // Set when the controller reports bus-off or the interface goes down; the
  // receive loop then drops the socket and re-opens it instead of failing every
  // command until a human restarts the node.
  std::atomic_bool bus_off_latched_{false};
  std::atomic<std::uint64_t> unmatched_frames_{0};
  std::atomic<std::uint64_t> overflow_frames_{0};
  std::atomic<std::uint64_t> reconnects_{0};
  std::thread thread_;
};

}
