#include "runtime_can/protocol.hpp"

#include <gtest/gtest.h>

TEST(ProtocolTest, EncodesCommandUsingBigEndianFields)
{
  const runtime_can::Command command{0x1234, 0x56, -2};

  const auto frame = runtime_can::encode_command(command);

  EXPECT_EQ(frame.id, runtime_can::kCommandFrameId);
  EXPECT_EQ(frame.size, runtime_can::kPayloadSize);
  EXPECT_EQ(frame.data, (std::array<std::uint8_t, 8>{
    0x01, 0x12, 0x34, 0x56, 0xFF, 0xFF, 0xFF, 0xFE}));
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

  frame.id = runtime_can::kResponseFrameId;
  EXPECT_FALSE(runtime_can::decode_command(frame, error).has_value());
  EXPECT_EQ(error, runtime_can::DecodeError::kWrongFrameId);

  frame = runtime_can::encode_command({1, 2, 3});
  frame.size = 7;
  EXPECT_FALSE(runtime_can::decode_command(frame, error).has_value());
  EXPECT_EQ(error, runtime_can::DecodeError::kWrongPayloadSize);

  frame = runtime_can::encode_command({1, 2, 3});
  frame.data[0] = 99;
  EXPECT_FALSE(runtime_can::decode_command(frame, error).has_value());
  EXPECT_EQ(error, runtime_can::DecodeError::kUnsupportedVersion);
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

  EXPECT_EQ(frame.id, runtime_can::kStatusFrameId);
  EXPECT_NE(frame.id, runtime_can::kResponseFrameId);
  EXPECT_NE(frame.id, runtime_can::kCommandFrameId);
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
