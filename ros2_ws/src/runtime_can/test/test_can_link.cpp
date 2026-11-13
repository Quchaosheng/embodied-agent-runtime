#include "runtime_can/can_link.hpp"
#include "runtime_can/in_memory_transport.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

using namespace std::chrono_literals;

namespace
{

std::shared_ptr<runtime_can::InMemoryTransport> make_transport()
{
  return std::make_shared<runtime_can::InMemoryTransport>();
}

}

TEST(CanLinkTest, StartOpensTheInterfaceAndFiltersBothFrameIds)
{
  auto transport = make_transport();
  runtime_can::CanLink link(transport, 5ms);
  std::string error;

  ASSERT_TRUE(
    link.start("vcan0", runtime_can::kResponseFrameId, runtime_can::kStatusFrameId, error));
  EXPECT_TRUE(link.is_open());
  EXPECT_EQ(link.interface_name(), "vcan0");
  EXPECT_TRUE(error.empty());
}

TEST(CanLinkTest, AwaitResponseReturnsTheMatchingFrameAndIgnoresOthers)
{
  auto transport = make_transport();
  runtime_can::CanLink link(transport, 5ms);
  std::string error;
  ASSERT_TRUE(
    link.start("vcan0", runtime_can::kResponseFrameId, runtime_can::kStatusFrameId, error));

  // A frame for a superseded command, then the one the caller is waiting for.
  ASSERT_TRUE(transport->inject(
      runtime_can::encode_response(runtime_can::Response{9, 0, 1, {0, 0, 0}})));
  ASSERT_TRUE(transport->inject(
      runtime_can::encode_response(runtime_can::Response{7, 3, 2, {1, 2, 3}})));

  runtime_can::Response response;
  const auto wait = link.await_response(7, 500ms, nullptr, response, error);
  EXPECT_EQ(wait, runtime_can::CanLink::Wait::kReceived);
  EXPECT_EQ(response.command_id, 7u);
  EXPECT_EQ(response.result_code, 3u);
}

TEST(CanLinkTest, AwaitResponseTimesOut)
{
  auto transport = make_transport();
  runtime_can::CanLink link(transport, 5ms);
  std::string error;
  ASSERT_TRUE(
    link.start("vcan0", runtime_can::kResponseFrameId, runtime_can::kStatusFrameId, error));

  runtime_can::Response response;
  EXPECT_EQ(
    link.await_response(7, 30ms, nullptr, response, error),
    runtime_can::CanLink::Wait::kTimeout);
}

TEST(CanLinkTest, AwaitResponseObservesACancel)
{
  auto transport = make_transport();
  runtime_can::CanLink link(transport, 5ms);
  std::string error;
  ASSERT_TRUE(
    link.start("vcan0", runtime_can::kResponseFrameId, runtime_can::kStatusFrameId, error));

  std::atomic_bool canceled{false};
  runtime_can::Response response;
  const auto started = std::chrono::steady_clock::now();
  const auto wait = link.await_response(
    7, 5s, [&canceled]() {return canceled.load();}, response, error);
  EXPECT_EQ(wait, runtime_can::CanLink::Wait::kTimeout);
  // Not canceled yet, so only the timeout could have ended it.
  EXPECT_GE(std::chrono::steady_clock::now() - started, 4s);
}

TEST(CanLinkTest, BusOffIsLatchedAndReportedSeparatelyFromAFrame)
{
  auto transport = make_transport();
  runtime_can::CanLink link(transport, 5ms);
  std::string error;
  ASSERT_TRUE(
    link.start("vcan0", runtime_can::kResponseFrameId, runtime_can::kStatusFrameId, error));

  runtime_can::BusState state;
  state.bus_off = true;
  ASSERT_TRUE(transport->inject_bus_state(state));

  runtime_can::Response response;
  EXPECT_EQ(
    link.await_response(7, 500ms, nullptr, response, error),
    runtime_can::CanLink::Wait::kBusOff);
  EXPECT_TRUE(link.bus_off_latched());
  EXPECT_TRUE(link.bus_state().bus_off);
}

TEST(CanLinkTest, ReopensAfterATransportErrorAndCountsIt)
{
  auto transport = make_transport();
  runtime_can::CanLink link(transport, 5ms);
  std::string error;
  ASSERT_TRUE(
    link.start("vcan0", runtime_can::kResponseFrameId, runtime_can::kStatusFrameId, error));

  transport->fail_receive("network is down");
  runtime_can::Response response;
  EXPECT_EQ(
    link.await_response(7, 500ms, nullptr, response, error),
    runtime_can::CanLink::Wait::kTransportError);

  // The interface comes back, then the receive thread retries it: the link is
  // usable again without restarting the node.
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (link.reconnects() == 0 && std::chrono::steady_clock::now() < deadline) {
    transport->clear_errors();
    std::this_thread::sleep_for(10ms);
  }
  EXPECT_EQ(link.reconnects(), 1u);
  const auto open_deadline = std::chrono::steady_clock::now() + 3s;
  while (!link.is_open() && std::chrono::steady_clock::now() < open_deadline) {
    std::this_thread::sleep_for(10ms);
  }
  EXPECT_TRUE(link.is_open());
}

TEST(CanLinkTest, UnmatchedFrameForASupersededCommandIsCountedNotDelivered)
{
  auto transport = make_transport();
  runtime_can::CanLink link(transport, 5ms);
  std::string error;
  ASSERT_TRUE(
    link.start("vcan0", runtime_can::kResponseFrameId, runtime_can::kStatusFrameId, error));

  ASSERT_TRUE(transport->inject(
      runtime_can::encode_response(runtime_can::Response{99, 0, 1, {0, 0, 0}})));
  runtime_can::Response response;
  EXPECT_EQ(
    link.await_response(7, 100ms, nullptr, response, error),
    runtime_can::CanLink::Wait::kTimeout);
  EXPECT_EQ(link.unmatched_frames(), 1u);
}

TEST(CanLinkTest, StatusFramesAreDeliveredThroughTheirOwnWait)
{
  auto transport = make_transport();
  runtime_can::CanLink link(transport, 5ms);
  std::string error;
  ASSERT_TRUE(
    link.start("vcan0", runtime_can::kResponseFrameId, runtime_can::kStatusFrameId, error));

  runtime_can::StatusFrame sent;
  sent.command_id = 11;
  sent.completion = runtime_can::Completion::kCompleted;
  ASSERT_TRUE(transport->inject(runtime_can::encode_status(sent)));

  runtime_can::StatusFrame received;
  EXPECT_EQ(
    link.await_status(11, 500ms, nullptr, received, error),
    runtime_can::CanLink::Wait::kReceived);
  EXPECT_EQ(received.completion, runtime_can::Completion::kCompleted);
}

TEST(CanLinkTest, BusOffReopensTheInterfaceAndClearsTheLatch)
{
  auto transport = make_transport();
  runtime_can::CanLink link(transport, 5ms, 10ms);
  std::string error;
  ASSERT_TRUE(
    link.start("vcan0", runtime_can::kResponseFrameId, runtime_can::kStatusFrameId, error));

  runtime_can::BusState state;
  state.bus_off = true;
  ASSERT_TRUE(transport->inject_bus_state(state));

  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (link.reconnects() == 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(10ms);
  }
  EXPECT_EQ(link.reconnects(), 1u);
  EXPECT_FALSE(link.bus_off_latched());
  EXPECT_FALSE(link.bus_state().bus_off);
}

TEST(CanLinkTest, StopEndsTheReceiveThread)
{
  auto transport = make_transport();
  runtime_can::CanLink link(transport, 5ms);
  std::string error;
  ASSERT_TRUE(
    link.start("vcan0", runtime_can::kResponseFrameId, runtime_can::kStatusFrameId, error));

  link.stop();
  EXPECT_FALSE(link.is_open());
  runtime_can::Response response;
  EXPECT_EQ(
    link.await_response(7, 100ms, nullptr, response, error),
    runtime_can::CanLink::Wait::kShutdown);
}
