#include "runtime_can/protocol.hpp"

#include <cstring>

namespace runtime_can
{

namespace
{

std::uint16_t decode_u16(const std::array<std::uint8_t, kPayloadSize> & data)
{
  return static_cast<std::uint16_t>(
    (static_cast<std::uint16_t>(data[1]) << 8U) |
    static_cast<std::uint16_t>(data[2]));
}

}

bool Command::operator==(const Command & other) const
{
  return command_id == other.command_id && opcode == other.opcode &&
         argument == other.argument && session == other.session;
}

RawFrame encode_command(const Command & command)
{
  RawFrame frame;
  frame.id = frame_id(command.session, kCommandFrameKind);
  frame.size = kPayloadSize;
  frame.data[0] = command.session;
  frame.data[1] = static_cast<std::uint8_t>(command.command_id >> 8U);
  frame.data[2] = static_cast<std::uint8_t>(command.command_id);
  frame.data[3] = command.opcode;

  std::uint32_t argument_bits;
  std::memcpy(&argument_bits, &command.argument, sizeof(argument_bits));
  frame.data[4] = static_cast<std::uint8_t>(argument_bits >> 24U);
  frame.data[5] = static_cast<std::uint8_t>(argument_bits >> 16U);
  frame.data[6] = static_cast<std::uint8_t>(argument_bits >> 8U);
  frame.data[7] = static_cast<std::uint8_t>(argument_bits);
  return frame;
}

RawFrame encode_response(const Response & response)
{
  RawFrame frame;
  frame.id = frame_id(response.session, kResponseFrameKind);
  frame.size = kPayloadSize;
  frame.data[0] = response.session;
  frame.data[1] = static_cast<std::uint8_t>(response.command_id >> 8U);
  frame.data[2] = static_cast<std::uint8_t>(response.command_id);
  frame.data[3] = response.result_code;
  frame.data[4] = response.device_mode;
  frame.data[5] = response.detail[0];
  frame.data[6] = response.detail[1];
  frame.data[7] = response.detail[2];
  return frame;
}

std::optional<Command> decode_command(const RawFrame & frame, DecodeError & error)
{
  if (kind_of(frame.id) != kCommandFrameId) {
    error = DecodeError::kWrongFrameId;
    return std::nullopt;
  }
  if (frame.size != kPayloadSize) {
    error = DecodeError::kWrongPayloadSize;
    return std::nullopt;
  }
  // The session in the id and the session in the payload must agree: a frame that
  // disagrees about which session it belongs to cannot be attributed safely.
  if (frame.data[0] != session_of(frame.id)) {
    error = DecodeError::kSessionMismatch;
    return std::nullopt;
  }

  std::uint32_t argument_bits =
    (static_cast<std::uint32_t>(frame.data[4]) << 24U) |
    (static_cast<std::uint32_t>(frame.data[5]) << 16U) |
    (static_cast<std::uint32_t>(frame.data[6]) << 8U) |
    static_cast<std::uint32_t>(frame.data[7]);
  std::int32_t argument;
  std::memcpy(&argument, &argument_bits, sizeof(argument));
  error = DecodeError::kNone;
  return Command{decode_u16(frame.data), frame.data[3], argument, frame.data[0]};
}

std::optional<Response> decode_response(const RawFrame & frame, DecodeError & error)
{
  if (kind_of(frame.id) != kResponseFrameId) {
    error = DecodeError::kWrongFrameId;
    return std::nullopt;
  }
  if (frame.size != kPayloadSize) {
    error = DecodeError::kWrongPayloadSize;
    return std::nullopt;
  }
  if (frame.data[0] != session_of(frame.id)) {
    error = DecodeError::kSessionMismatch;
    return std::nullopt;
  }

  error = DecodeError::kNone;
  return Response{
    decode_u16(frame.data), frame.data[3], frame.data[4],
    {frame.data[5], frame.data[6], frame.data[7]}, frame.data[0]};
}

RawFrame encode_status(const StatusFrame & status)
{
  RawFrame frame;
  frame.id = frame_id(status.session, kStatusFrameKind);
  frame.size = kPayloadSize;
  frame.data[0] = status.session;
  frame.data[1] = static_cast<std::uint8_t>(status.command_id >> 8U);
  frame.data[2] = static_cast<std::uint8_t>(status.command_id);
  frame.data[3] = static_cast<std::uint8_t>(status.completion);
  frame.data[4] = status.fault_code;
  frame.data[5] = status.detail[0];
  frame.data[6] = status.detail[1];
  frame.data[7] = status.detail[2];
  return frame;
}

std::optional<StatusFrame> decode_status(const RawFrame & frame, DecodeError & error)
{
  if (kind_of(frame.id) != kStatusFrameId) {
    error = DecodeError::kWrongFrameId;
    return std::nullopt;
  }
  if (frame.size != kPayloadSize) {
    error = DecodeError::kWrongPayloadSize;
    return std::nullopt;
  }
  if (frame.data[0] != session_of(frame.id)) {
    error = DecodeError::kSessionMismatch;
    return std::nullopt;
  }
  // An unrecognised completion state is rejected, not guessed at: treating an
  // unknown value as completed would report motion that was never confirmed.
  if (frame.data[3] > static_cast<std::uint8_t>(Completion::kFaulted)) {
    error = DecodeError::kUnknownCompletion;
    return std::nullopt;
  }

  error = DecodeError::kNone;
  return StatusFrame{
    decode_u16(frame.data), static_cast<Completion>(frame.data[3]), frame.data[4],
    {frame.data[5], frame.data[6], frame.data[7]}, frame.data[0]};
}

RawFrame encode_session_start(const SessionStart & start)
{
  RawFrame frame;
  frame.id = frame_id(start.session, kSessionStartFrameKind);
  frame.size = 2;
  frame.data[0] = start.session;
  frame.data[1] = start.origin;
  return frame;
}

std::optional<SessionStart> decode_session_start(const RawFrame & frame, DecodeError & error)
{
  if (kind_of(frame.id) != kSessionStartFrameId) {
    error = DecodeError::kWrongFrameId;
    return std::nullopt;
  }
  if (frame.size != 2) {
    error = DecodeError::kWrongPayloadSize;
    return std::nullopt;
  }
  if (frame.data[0] != session_of(frame.id)) {
    error = DecodeError::kSessionMismatch;
    return std::nullopt;
  }
  error = DecodeError::kNone;
  return SessionStart{frame.data[0], frame.data[1]};
}

RawFrame encode_heartbeat(const Heartbeat & heartbeat)
{
  RawFrame frame;
  frame.id = frame_id(heartbeat.session, kHeartbeatFrameKind);
  frame.size = 3;
  frame.data[0] = heartbeat.session;
  frame.data[1] = heartbeat.counter;
  frame.data[2] = heartbeat.device_mode;
  return frame;
}

std::optional<Heartbeat> decode_heartbeat(const RawFrame & frame, DecodeError & error)
{
  if (kind_of(frame.id) != kHeartbeatFrameId) {
    error = DecodeError::kWrongFrameId;
    return std::nullopt;
  }
  if (frame.size != 3) {
    error = DecodeError::kWrongPayloadSize;
    return std::nullopt;
  }
  if (frame.data[0] != session_of(frame.id)) {
    error = DecodeError::kSessionMismatch;
    return std::nullopt;
  }
  error = DecodeError::kNone;
  return Heartbeat{frame.data[0], frame.data[1], frame.data[2]};
}

std::string_view to_string(Completion completion)
{
  switch (completion) {
    case Completion::kAccepted:
      return "accepted";
    case Completion::kExecuting:
      return "executing";
    case Completion::kCompleted:
      return "completed";
    case Completion::kFaulted:
      return "faulted";
  }
  return "unknown";
}

std::string_view to_string(DecodeError error)
{
  switch (error) {
    case DecodeError::kNone:
      return "none";
    case DecodeError::kWrongFrameId:
      return "wrong_frame_id";
    case DecodeError::kWrongPayloadSize:
      return "wrong_payload_size";
    case DecodeError::kUnsupportedVersion:
      return "unsupported_version";
    case DecodeError::kUnknownCompletion:
      return "unknown_completion";
    case DecodeError::kSessionMismatch:
      return "session_mismatch";
  }
  return "unknown";
}

}
