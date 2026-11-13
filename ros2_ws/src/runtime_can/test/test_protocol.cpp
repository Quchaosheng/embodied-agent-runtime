#include "runtime_can/protocol.hpp"

#include <gtest/gtest.h>

TEST(ProtocolTest, EncodesCommandUsingBigEndianFields)
{
  const runtime_can::Command command{0x1234, 0x56, -2};

  const auto frame = runtime_can::encode_command(command);

  // A default Command is session 1, so its id is the session-0 command id plus
  // the session nibble.
  EXPECT_EQ(frame.id, runtime_can::frame_id(1, runtime_can::kCommandFrameKind));
  EXPECT_EQ(runtime_can::session_of(frame.id), 1);
  EXPECT_EQ(frame.size, runtime_can::kPayloadSize);
  // data[0] is the session now (1 by default, which is v1's id space), and the
  // frame id carries the same session in bits 10..8.
  EXPECT_EQ(frame.data, (std::array<std::uint8_t, 8>{
    0x01, 0x12, 0x34, 0x56, 0xFF, 0xFF, 0xFF, 0xFE}));
  EXPECT_EQ(frame.id, runtime_can::frame_id(1, runtime_can::kCommandFrameId));
}

TEST(ProtocolTest, RoundTripsCommandAndResponse)
{
  runtime_can::DecodeError error;
  const runtime_can::Command command{42, 7, 123456};
  const runtime_can::Response response{42, 2, 3, {4, 5, 6}};

  const auto decoded_command = runtime_can::decode_command(
    runtime_can::encode_command(command), error);
  ASSERT_TRUE(decoded_command.has_value());
  EXPECT_EQ(error, runtime_can::DecodeError::kNone);
  EXPECT_TRUE(*decoded_command == command);

  const auto decoded_response = runtime_can::decode_response(
    runtime_can::encode_response(response), error);
  ASSERT_TRUE(decoded_response.has_value());
  EXPECT_EQ(decoded_response->command_id, response.command_id);
  EXPECT_EQ(decoded_response->result_code, response.result_code);
  EXPECT_EQ(decoded_response->device_mode, response.device_mode);
  EXPECT_EQ(decoded_response->detail, response.detail);
}

TEST(ProtocolTest, RejectsWrongIdSizeAndVersion)
{
  runtime_can::DecodeError error;
  auto frame = runtime_can::encode_command({1, 2, 3});

  frame.id = runtime_can::frame_id(1, runtime_can::kResponseFrameKind);
  EXPECT_FALSE(runtime_can::decode_command(frame, error).has_value());
  EXPECT_EQ(error, runtime_can::DecodeError::kWrongFrameId);

  frame = runtime_can::encode_command({1, 2, 3});
  frame.size = 7;
  EXPECT_FALSE(runtime_can::decode_command(frame, error).has_value());
  EXPECT_EQ(error, runtime_can::DecodeError::kWrongPayloadSize);

  // The session in the payload must match the session in the frame id, or the
  // frame cannot be attributed to a session at all.
  frame = runtime_can::encode_command({1, 2, 3});
  frame.data[0] = 5;
  EXPECT_FALSE(runtime_can::decode_command(frame, error).has_value());
  EXPECT_EQ(error, runtime_can::DecodeError::kSessionMismatch);
}

TEST(ProtocolTest, ReservesSeparateStopCommandNamespace)
{
  EXPECT_EQ(runtime_can::kStopOpcode, 0xFF);
  EXPECT_LT(runtime_can::kApplicationCommandIdMax, runtime_can::kStopCommandIdMin);
  EXPECT_EQ(runtime_can::kApplicationCommandIdMax + 1, runtime_can::kStopCommandIdMin);
}

// The execution status is a separate frame from the ACK: a device that only
// acknowledges never sends one, which is why the two paths stay distinguishable.
TEST(ProtocolTest, StatusFrameIsDisjointFromTheCommandAck)
{
  const runtime_can::StatusFrame status{
    runtime_can::kApplicationCommandIdMax, runtime_can::Completion::kExecuting, 0, {1, 2, 3}};

  const auto frame = runtime_can::encode_status(status);

  EXPECT_EQ(frame.id, runtime_can::frame_id(1, runtime_can::kStatusFrameKind));
  EXPECT_EQ(runtime_can::kind_of(frame.id), runtime_can::kStatusFrameId);
  EXPECT_NE(frame.id, runtime_can::frame_id(1, runtime_can::kResponseFrameKind));
  EXPECT_NE(frame.id, runtime_can::frame_id(1, runtime_can::kCommandFrameKind));
  EXPECT_EQ(frame.size, runtime_can::kPayloadSize);
}

TEST(ProtocolTest, RoundTripsEveryCompletionState)
{
  for (const auto completion : {
    runtime_can::Completion::kAccepted, runtime_can::Completion::kExecuting,
    runtime_can::Completion::kCompleted, runtime_can::Completion::kFaulted})
  {
    runtime_can::DecodeError error;
    const runtime_can::StatusFrame status{42, completion, 7, {9, 8, 7}};
    const auto decoded = runtime_can::decode_status(
      runtime_can::encode_status(status), error);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(error, runtime_can::DecodeError::kNone);
    EXPECT_EQ(decoded->command_id, 42U);
    EXPECT_EQ(decoded->completion, completion);
    EXPECT_EQ(decoded->fault_code, 7U);
    EXPECT_EQ(decoded->detail, (std::array<std::uint8_t, 3>{9, 8, 7}));
  }
}

// An unknown completion byte is rejected rather than read as "completed": that
// would report motion the device never confirmed.
TEST(ProtocolTest, RejectsAnUnknownCompletionState)
{
  auto frame = runtime_can::encode_status(
    runtime_can::StatusFrame{42, runtime_can::Completion::kCompleted, 0, {}});
  frame.data[3] = 42;
  runtime_can::DecodeError error;
  EXPECT_FALSE(runtime_can::decode_status(frame, error).has_value());
  EXPECT_EQ(error, runtime_can::DecodeError::kUnknownCompletion);
}

TEST(ProtocolTest, RejectsAStatusFrameWithTheWrongId)
{
  auto frame = runtime_can::encode_status(
    runtime_can::StatusFrame{42, runtime_can::Completion::kAccepted, 0, {}});
  frame.id = runtime_can::kResponseFrameId;
  runtime_can::DecodeError error;
  EXPECT_FALSE(runtime_can::decode_status(frame, error).has_value());
  EXPECT_EQ(error, runtime_can::DecodeError::kWrongFrameId);
}

// Every frame id must fit the 11-bit standard-frame mask. SocketCan ANDs each id
// with CAN_SFF_MASK, so a session nibble placed above bit 10 was silently
// truncated to the frame kind alone and the device never saw the command. No unit
// test caught it because InMemoryTransport does not mask; the vcan E2E did.
TEST(ProtocolTest, EverySessionFrameIdFitsTheStandardFrameMask)
{
  constexpr std::uint32_t kCanSffMask = 0x7FF;
  for (std::uint32_t session = 0; session <= runtime_can::kMaxSession; ++session) {
    const auto typed = static_cast<std::uint8_t>(session);
    for (const auto kind : {
      runtime_can::kCommandFrameKind, runtime_can::kResponseFrameKind,
      runtime_can::kStatusFrameKind, runtime_can::kSessionStartFrameKind,
      runtime_can::kHeartbeatFrameKind})
    {
      const auto id = runtime_can::frame_id(typed, kind);
      EXPECT_LE(id, kCanSffMask)
        << "session " << session << " kind " << kind << " produced 0x" << std::hex << id;
      // Truncation is the failure mode: the masked id must still be this frame.
      EXPECT_EQ(id & kCanSffMask, id);
      EXPECT_EQ(id >> runtime_can::kSessionShift & runtime_can::kMaxSession, session);
      EXPECT_EQ(runtime_can::kind_of(id), kind << runtime_can::kFrameKindShift);
    }
  }
}

// Round-trip through the exact mask SocketCan applies, so a regression here fails
// without needing a vcan interface.
TEST(ProtocolTest, CommandsSurviveTheSocketCanMask)
{
  constexpr std::uint32_t kCanSffMask = 0x7FF;
  for (std::uint8_t session = 1; session <= runtime_can::kMaxSession; ++session) {
    const runtime_can::Command command{5, 1, 42, session};
    auto frame = runtime_can::encode_command(command);
    frame.id &= kCanSffMask;  // what the socket does on send

    runtime_can::DecodeError error;
    const auto decoded = runtime_can::decode_command(frame, error);
    ASSERT_TRUE(decoded.has_value()) << "session " << static_cast<int>(session);
    EXPECT_EQ(decoded->session, session);
    EXPECT_EQ(decoded->command_id, 5u);

    runtime_can::Response response{5, 0, 1, {0, 0, 0}, session};
    auto response_frame = runtime_can::encode_response(response);
    response_frame.id &= kCanSffMask;
    const auto decoded_response = runtime_can::decode_response(response_frame, error);
    ASSERT_TRUE(decoded_response.has_value());
    EXPECT_EQ(decoded_response->session, session);
  }
}
