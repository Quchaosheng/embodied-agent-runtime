#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace runtime_can
{

// v2 adds a session number to the frame id, so a command identity is
// (session, command_id) rather than command_id alone. That is what removes the
// replay-cache collision: a device cannot serve an id from a previous process
// because the session differs.
//
//   bits 15..12 session, 0-15  (the bridge picks one at startup)
//   bits 11..8  frame kind     0 command, 1 response, 2 status,
//                              3 session-start, 4 heartbeat
//   bits  7..0  unused, zero
//
// A v1 peer used 0x100-0x102, so v2 ids sit in the same 11-bit SFF space and do
// not collide with them; the session nibble is what a v1 peer ignores.
constexpr std::uint32_t kSessionShift = 12;
constexpr std::uint8_t kMaxSession = 15;
constexpr std::uint32_t kFrameKindShift = 8;
constexpr std::uint32_t kFrameKindMask = 0x0F00;
constexpr std::uint32_t kCommandFrameKind = 0;
constexpr std::uint32_t kResponseFrameKind = 1;
constexpr std::uint32_t kStatusFrameKind = 2;
constexpr std::uint32_t kSessionStartFrameKind = 3;
constexpr std::uint32_t kHeartbeatFrameKind = 4;

// The frame ids session 0 produces, kept as named constants because tests and
// callers refer to them directly.
constexpr std::uint32_t kCommandFrameId =
  (kCommandFrameKind << kFrameKindShift);
constexpr std::uint32_t kResponseFrameId =
  (kResponseFrameKind << kFrameKindShift);
constexpr std::uint32_t kStatusFrameId =
  (kStatusFrameKind << kFrameKindShift);
constexpr std::uint32_t kSessionStartFrameId =
  (kSessionStartFrameKind << kFrameKindShift);
constexpr std::uint32_t kHeartbeatFrameId =
  (kHeartbeatFrameKind << kFrameKindShift);

// The frame id for one session and one *raw* kind (0-4). The shift belongs here,
// so a caller passes kResponseFrameKind and not the already-shifted
// kResponseFrameId: mixing the two silently produced kind 0.
constexpr std::uint32_t frame_id(const std::uint8_t session, const std::uint32_t kind)
{
  return (static_cast<std::uint32_t>(session & kMaxSession) << kSessionShift) |
         ((kind << kFrameKindShift) & kFrameKindMask);
}

constexpr std::uint8_t session_of(const std::uint32_t id)
{
  return static_cast<std::uint8_t>((id >> kSessionShift) & kMaxSession);
}

constexpr std::uint32_t kind_of(const std::uint32_t id)
{
  return id & kFrameKindMask;
}
// Execution status, separate from the command ACK. A device that only
// acknowledges a command never sends one, which is why the bridge keeps an
// explicit ACK-only mode instead of assuming completion.
constexpr std::uint8_t kProtocolVersion = 2;
constexpr std::uint8_t kPayloadSize = 8;
constexpr std::uint8_t kStopOpcode = 0xFF;
constexpr std::uint16_t kApplicationCommandIdMax = 0x7FFF;
// STOP ACKs use a disjoint command-id range and cannot match an application ACK.
constexpr std::uint16_t kStopCommandIdMin = 0x8000;

struct RawFrame
{
  std::uint32_t id{0};
  std::uint8_t size{0};
  std::array<std::uint8_t, kPayloadSize> data{};
};

// Every frame carries the session it belongs to, so the receiver can tell a
// frame from the current process apart from one left over from a previous one
// without relying on the id alone.
struct Sessioned
{
  std::uint8_t session{1};

  bool operator==(const Sessioned & other) const {return session == other.session;}
  bool operator!=(const Sessioned & other) const {return !(*this == other);}
};

struct Command
{
  std::uint16_t command_id{0};
  std::uint8_t opcode{0};
  std::int32_t argument{0};
  // Default 1 so a caller that has not been taught about sessions still
  // produces a frame a v1 device understands.
  std::uint8_t session{1};

  bool operator==(const Command & other) const;
};

struct Response
{
  std::uint16_t command_id{0};
  std::uint8_t result_code{0};
  std::uint8_t device_mode{0};
  std::array<std::uint8_t, 3> detail{};
  std::uint8_t session{1};
};

// What the device reports about the command it acknowledged. kAccepted and
// kExecuting are movement not yet finished; only kCompleted means the device
// says the command reached its end state. None of these are physical-motion
// evidence, only protocol states.
enum class Completion : std::uint8_t
{
  kAccepted = 0,
  kExecuting = 1,
  kCompleted = 2,
  kFaulted = 3,
};

// A status frame carries the same command identity as the ACK so a late status
// for a superseded command cannot be attributed to the current one.
struct StatusFrame
{
  std::uint16_t command_id{0};
  Completion completion{Completion::kAccepted};
  // Device-specific; the bridge only reports it, it never interprets it.
  std::uint8_t fault_code{0};
  std::array<std::uint8_t, 3> detail{};
  std::uint8_t session{1};
};

// Announced by the bridge when a process starts. A device that sees a new
// session drops its replay cache, which is what makes a bridge restart unable to
// replay a stale ACK. It is not a command and never enters the cache.
struct SessionStart
{
  std::uint8_t session{1};
  // Free-form device/session identity from the bridge, 0 when unused.
  std::uint8_t origin{0};
};

// Carries liveness in both directions. A device stops on its own after
// max_missed consecutive missed beats, which is the only mechanism that lets it
// tell "the runtime died" from "the runtime is busy".
struct Heartbeat
{
  std::uint8_t session{1};
  std::uint8_t counter{0};
  std::uint8_t device_mode{0};
};

// A session-scoped cache key: the command identity is the pair, not the id.
struct CommandKey
{
  std::uint8_t session{1};
  std::uint16_t command_id{0};

  bool operator==(const CommandKey & other) const
  {
    return session == other.session && command_id == other.command_id;
  }
  bool operator!=(const CommandKey & other) const {return !(*this == other);}
  bool operator<(const CommandKey & other) const
  {
    return session != other.session ? session < other.session : command_id < other.command_id;
  }
};

enum class DecodeError
{
  kNone,
  kWrongFrameId,
  kWrongPayloadSize,
  kUnsupportedVersion,
  kUnknownCompletion,
  // The session in the frame id and the one in the payload disagree.
  kSessionMismatch
};

RawFrame encode_command(const Command & command);
RawFrame encode_response(const Response & response);
RawFrame encode_status(const StatusFrame & status);
RawFrame encode_session_start(const SessionStart & start);
RawFrame encode_heartbeat(const Heartbeat & heartbeat);
std::optional<Command> decode_command(const RawFrame & frame, DecodeError & error);
std::optional<Response> decode_response(const RawFrame & frame, DecodeError & error);
std::optional<StatusFrame> decode_status(const RawFrame & frame, DecodeError & error);
std::optional<SessionStart> decode_session_start(const RawFrame & frame, DecodeError & error);
std::optional<Heartbeat> decode_heartbeat(const RawFrame & frame, DecodeError & error);
std::string_view to_string(DecodeError error);
std::string_view to_string(Completion completion);

}
