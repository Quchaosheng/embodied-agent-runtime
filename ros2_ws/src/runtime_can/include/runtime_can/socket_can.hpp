#pragma once

#include "runtime_can/protocol.hpp"
#include "runtime_can/transport.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace runtime_can
{

// The real interface. Behaviour and error strings are unchanged from the
// pre-interface version, so a node that injects nothing behaves as it did.
class SocketCan : public Transport
{
public:
  SocketCan() = default;
  ~SocketCan() override;

  SocketCan(const SocketCan &) = delete;
  SocketCan & operator=(const SocketCan &) = delete;

  bool open(const std::string & interface_name, std::string & error) override;
  bool set_filter(std::uint32_t frame_id, std::string & error) override;
  bool set_filter(
    std::uint32_t first_frame_id, std::uint32_t second_frame_id, std::string & error) override;
  bool set_kind_filter(
    const std::vector<std::uint32_t> & frame_kinds, std::uint32_t kind_mask,
    std::string & error) override;
  bool send(const RawFrame & frame, std::string & error) const override;
  // An error frame is decoded into `bus_state` and reported as kBusState, so
  // bus-off becomes a state the node acts on instead of a transport error it
  // cannot tell apart from a dropped frame.
  ReceiveStatus receive(
    RawFrame & frame, std::chrono::milliseconds timeout, std::string & error,
    BusState * bus_state = nullptr) const override;
  void close() override;
  bool is_open() const override;

private:
  int socket_fd_{-1};
};

}
