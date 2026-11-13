#pragma once

// One error-code table for the whole runtime, partitioned by the layer that owns
// the meaning. Every constant is distinct and the ranges cannot overlap, so a
// caller that receives `SAFE_STOP` + a code can tell which layer produced it
// without decoding a message string.
//
//   100–199   entry / executor     task_executor, and the adapters above it
//   200–299   task_orchestrator
//   300–399   device_bridge        bridge-local failures
//   400–599   gateway transport    the gRPC surface
//   1000–1255 device rejection     1000 + the device's own 8-bit result_code
//
// The ranges previously overlapped. device_bridge used 205/206 for "STOP refused"
// and "completion timeout" while task_orchestrator used the same two numbers for
// "shutting down" and "precondition failed". The device-rejection range was
// `300 + result_code`, i.e. 300–555, which swallowed the gateway's entire 400/500
// vocabulary once a device reported a large code. Child codes propagate upward
// unchanged, so every one of those collisions reached the caller.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace robot_task_interfaces
{
namespace error_code
{

// --- 1xx: entry and executor -------------------------------------------------
constexpr std::uint16_t kExecutorBridgeRejected = 110;
constexpr std::uint16_t kExecutorBridgeUnavailable = 111;
constexpr std::uint16_t kExecutorTaskDeadline = 112;
constexpr std::uint16_t kExecutorCancelUnconfirmed = 113;
constexpr std::uint16_t kExecutorUnexpectedChildResult = 114;

// --- 2xx: task_orchestrator --------------------------------------------------
constexpr std::uint16_t kOrchestratorShutdown = 205;
constexpr std::uint16_t kOrchestratorPreconditionFailed = 206;
constexpr std::uint16_t kOrchestratorCancelUnconfirmed = 207;
constexpr std::uint16_t kOrchestratorInternal = 208;
constexpr std::uint16_t kOrchestratorDeadline = 209;

// --- 3xx: device_bridge, bridge-local ----------------------------------------
constexpr std::uint16_t kDeviceInvalidGoal = 300;
constexpr std::uint16_t kDeviceTransport = 301;
constexpr std::uint16_t kDeviceAckTimeout = 302;
constexpr std::uint16_t kDeviceShutdown = 303;
constexpr std::uint16_t kDeviceCanceled = 304;
constexpr std::uint16_t kDeviceStopTimeout = 305;
constexpr std::uint16_t kDeviceStopRejected = 306;
constexpr std::uint16_t kDeviceCompletionTimeout = 307;

// --- 4xx–5xx: gateway transport ----------------------------------------------
// These keep the values the gateway already published. Reusing one value for two
// meanings was the smaller sin than renumbering a field clients read: "orchestrator
// unavailable" and "gateway is shutting down" both mean "the runtime cannot take
// this request now", and the message string distinguishes them.
constexpr std::uint16_t kGatewayNotAllowlisted = 400;
constexpr std::uint16_t kGatewayConflict = 409;
constexpr std::uint16_t kGatewayRateLimited = 429;
constexpr std::uint16_t kGatewayCancelFailed = 500;
constexpr std::uint16_t kGatewayUnavailable = 503;
constexpr std::uint16_t kGatewayGoalResponseTimeout = 504;

// --- 1xxx: the device's own rejection ----------------------------------------
// `kDeviceRejectedBase + result_code`. The device's result code is 8 bits, so the
// range is 1000–1255: above every layer range above, which is what keeps a device
// code from ever being read as a gateway code.
constexpr std::uint16_t kDeviceRejectedBase = 1000;
constexpr std::uint16_t kDeviceResultCodeMax = 255;
constexpr std::uint16_t kDeviceRejectedMax = kDeviceRejectedBase + kDeviceResultCodeMax;

std::uint16_t device_rejected(std::uint8_t result_code);

// Which layer a code belongs to. A caller that receives a code uses this to know
// whose vocabulary to interpret it in; nothing has to guess from the number.
enum class Layer
{
  kNone,
  kExecutor,
  kOrchestrator,
  kDevice,
  kGateway,
};

Layer layer_of(std::uint16_t code);
std::string_view to_string(Layer layer);

// Every named constant, for the uniqueness test. The device-rejection range is a
// formula rather than a list, so kDeviceRejectedBase/kDeviceRejectedMax describe
// it and the test checks it against the ranges above.
struct Entry
{
  std::uint16_t code;
  Layer layer;
  std::string_view name;
};

const Entry * table(std::size_t & count);

}
}
