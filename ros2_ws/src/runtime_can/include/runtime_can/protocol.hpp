#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace runtime_can
{

constexpr std::uint32_t kCommandFrameId = 0x100;
constexpr std::uint32_t kResponseFrameId = 0x101;
// Execution status, separate from the command ACK. A device that only
// acknowledges a command never sends one, which is why the bridge keeps an
// explicit ACK-only mode instead of assuming completion.
constexpr std::uint32_t kStatusFrameId = 0x102;
constexpr std::uint8_t kProtocolVersion = 1;
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

struct Command
{
  std::uint16_t command_id{0};
  std::uint8_t opcode{0};
  std::int32_t argument{0};

  bool operator==(const Command & other) const;
};

struct Response
{
  std::uint16_t command_id{0};
  std::uint8_t result_code{0};
  std::uint8_t device_mode{0};
  std::array<std::uint8_t, 3> detail{};
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
};

enum class DecodeError
{
  kNone,
  kWrongFrameId,
  kWrongPayloadSize,
  kUnsupportedVersion,
  kUnknownCompletion
};

RawFrame encode_command(const Command & command);
RawFrame encode_response(const Response & response);
RawFrame encode_status(const StatusFrame & status);
std::optional<Command> decode_command(const RawFrame & frame, DecodeError & error);
std::optional<Response> decode_response(const RawFrame & frame, DecodeError & error);
std::optional<StatusFrame> decode_status(const RawFrame & frame, DecodeError & error);
std::string_view to_string(DecodeError error);
std::string_view to_string(Completion completion);

}
