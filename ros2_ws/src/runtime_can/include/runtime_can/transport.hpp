#pragma once

#include "runtime_can/protocol.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace runtime_can
{

enum class ReceiveStatus
{
  kFrame,
  kTimeout,
  kError,
  // The controller reported a state about itself — a warning, error-passive or
  // bus-off — rather than a frame from a peer. Frames on a bus and the bus
  // itself are separate failures, so they stay separate statuses.
  kBusState,
};

// The link state the controller reports in an error frame. None of these are
// per-frame evidence; they describe whether the interface can carry traffic at
// all, which is what decides between "retry" and "recover".
struct BusState
{
  bool error_warning{false};
  bool error_passive{false};
  bool bus_off{false};
  // The controller's own RX queue overflowed. The kernel then discards the
  // newest frames, so this is a possible lost ACK rather than a warning.
  bool rx_overflow{false};

  bool operator==(const BusState & other) const
  {
    return error_warning == other.error_warning && error_passive == other.error_passive &&
           bus_off == other.bus_off && rx_overflow == other.rx_overflow;
  }
  bool operator!=(const BusState & other) const {return !(*this == other);}
};

// The seam every node holding a socket goes through. `SocketCan` is the only
// implementation that touches a real interface; tests supply one that does not,
// which is what lets the bridge be exercised on a host without vcan.
class Transport
{
public:
  virtual ~Transport() = default;

  virtual bool open(const std::string & interface_name, std::string & error) = 0;
  virtual bool set_filter(std::uint32_t frame_id, std::string & error) = 0;
  // Two frame ids on one socket, for a node that reads both the command ACK and
  // the separate execution status.
  virtual bool set_filter(
    std::uint32_t first_frame_id, std::uint32_t second_frame_id, std::string & error) = 0;
  // Matches one frame kind in every session. A device listens for commands from
  // whichever session the bridge picked, so an exact id would filter them all out;
  // the mask is what makes the session nibble a don't-care.
  virtual bool set_kind_filter(
    const std::vector<std::uint32_t> & frame_kinds, std::uint32_t kind_mask,
    std::string & error) = 0;
  virtual bool send(const RawFrame & frame, std::string & error) const = 0;
  // `bus_state` is written only when the result is kBusState; a caller that does
  // not care about link state passes nullptr.
  virtual ReceiveStatus receive(
    RawFrame & frame, std::chrono::milliseconds timeout, std::string & error,
    BusState * bus_state = nullptr) const = 0;
  virtual void close() = 0;
  virtual bool is_open() const = 0;
};

}
