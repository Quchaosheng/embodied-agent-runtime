#include "runtime_can/in_memory_transport.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

TEST(InMemoryTransportTest, DeliversInjectedFramesInOrder)
{
  runtime_can::InMemoryTransport transport;
  std::string error;
  ASSERT_TRUE(transport.open("vcan0", error));

  const runtime_can::Command command{7, 1, 42};
  ASSERT_TRUE(transport.inject(runtime_can::encode_command(command)));

  runtime_can::RawFrame frame;
  ASSERT_EQ(
    transport.receive(frame, std::chrono::milliseconds(50), error),
    runtime_can::ReceiveStatus::kFrame);
  runtime_can::DecodeError decode_error;
  const auto decoded = runtime_can::decode_command(frame, decode_error);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(*decoded == command);
}

TEST(InMemoryTransportTest, ResponderAnswersASentFrame)
{
  runtime_can::InMemoryTransport transport;
  std::string error;
  ASSERT_TRUE(transport.open("vcan0", error));
  transport.set_responder(
    [&transport](const runtime_can::RawFrame & frame) {
      runtime_can::DecodeError decode_error;
      const auto command = runtime_can::decode_command(frame, decode_error);
      ASSERT_TRUE(command.has_value());
      transport.inject(runtime_can::encode_response(
          runtime_can::Response{command->command_id, 0, 1, {0, 0, 0}}));
    });

  ASSERT_TRUE(transport.send(runtime_can::encode_command({7, 1, 42}), error));
  runtime_can::RawFrame frame;
  ASSERT_EQ(
    transport.receive(frame, std::chrono::milliseconds(50), error),
    runtime_can::ReceiveStatus::kFrame);
  runtime_can::DecodeError decode_error;
  const auto response = runtime_can::decode_response(frame, decode_error);
  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response->command_id, 7);
  EXPECT_EQ(response->result_code, 0);
}

TEST(InMemoryTransportTest, ReceiveTimesOutWhenNothingIsInjected)
{
  runtime_can::InMemoryTransport transport;
  std::string error;
  ASSERT_TRUE(transport.open("vcan0", error));

  runtime_can::RawFrame frame;
  const auto started = std::chrono::steady_clock::now();
  EXPECT_EQ(
    transport.receive(frame, std::chrono::milliseconds(20), error),
    runtime_can::ReceiveStatus::kTimeout);
  EXPECT_GE(std::chrono::steady_clock::now() - started, std::chrono::milliseconds(20));
}

TEST(InMemoryTransportTest, BoundsInboundAndCountsDrops)
{
  runtime_can::InMemoryTransport transport(2);
  std::string error;
  ASSERT_TRUE(transport.open("vcan0", error));

  EXPECT_TRUE(transport.inject(runtime_can::encode_command({1, 1, 0})));
  EXPECT_TRUE(transport.inject(runtime_can::encode_command({2, 1, 0})));
  EXPECT_FALSE(transport.inject(runtime_can::encode_command({3, 1, 0})));
  EXPECT_EQ(transport.dropped_frames(), 1u);
}

TEST(InMemoryTransportTest, ReportsBusStateSeparatelyFromFrames)
{
  runtime_can::InMemoryTransport transport;
  std::string error;
  ASSERT_TRUE(transport.open("vcan0", error));
  runtime_can::BusState expected;
  expected.bus_off = true;
  ASSERT_TRUE(transport.inject_bus_state(expected));

  runtime_can::RawFrame frame;
  runtime_can::BusState actual;
  ASSERT_EQ(
    transport.receive(frame, std::chrono::milliseconds(50), error, &actual),
    runtime_can::ReceiveStatus::kBusState);
  EXPECT_TRUE(actual.bus_off);
}

TEST(InMemoryTransportTest, InjectedErrorsReachTheCaller)
{
  runtime_can::InMemoryTransport transport;
  std::string error;
  ASSERT_TRUE(transport.open("vcan0", error));
  transport.fail_send("bus is down");

  EXPECT_FALSE(transport.send(runtime_can::encode_command({1, 1, 0}), error));
  EXPECT_EQ(error, "bus is down");

  transport.fail_receive("read failed");
  runtime_can::RawFrame frame;
  EXPECT_EQ(
    transport.receive(frame, std::chrono::milliseconds(10), error),
    runtime_can::ReceiveStatus::kError);
  EXPECT_EQ(error, "read failed");
}
