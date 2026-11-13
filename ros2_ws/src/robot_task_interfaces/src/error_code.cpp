#include "robot_task_interfaces/error_code.hpp"

namespace robot_task_interfaces
{
namespace error_code
{

namespace
{

constexpr Entry kTable[] = {
  {kExecutorBridgeRejected, Layer::kExecutor, "kExecutorBridgeRejected"},
  {kExecutorBridgeUnavailable, Layer::kExecutor, "kExecutorBridgeUnavailable"},
  {kExecutorTaskDeadline, Layer::kExecutor, "kExecutorTaskDeadline"},
  {kExecutorCancelUnconfirmed, Layer::kExecutor, "kExecutorCancelUnconfirmed"},
  {kExecutorUnexpectedChildResult, Layer::kExecutor, "kExecutorUnexpectedChildResult"},

  {kOrchestratorShutdown, Layer::kOrchestrator, "kOrchestratorShutdown"},
  {kOrchestratorPreconditionFailed, Layer::kOrchestrator, "kOrchestratorPreconditionFailed"},
  {kOrchestratorCancelUnconfirmed, Layer::kOrchestrator, "kOrchestratorCancelUnconfirmed"},
  {kOrchestratorInternal, Layer::kOrchestrator, "kOrchestratorInternal"},
  {kOrchestratorDeadline, Layer::kOrchestrator, "kOrchestratorDeadline"},

  {kDeviceInvalidGoal, Layer::kDevice, "kDeviceInvalidGoal"},
  {kDeviceTransport, Layer::kDevice, "kDeviceTransport"},
  {kDeviceAckTimeout, Layer::kDevice, "kDeviceAckTimeout"},
  {kDeviceShutdown, Layer::kDevice, "kDeviceShutdown"},
  {kDeviceCanceled, Layer::kDevice, "kDeviceCanceled"},
  {kDeviceStopTimeout, Layer::kDevice, "kDeviceStopTimeout"},
  {kDeviceStopRejected, Layer::kDevice, "kDeviceStopRejected"},
  {kDeviceCompletionTimeout, Layer::kDevice, "kDeviceCompletionTimeout"},

  {kGatewayNotAllowlisted, Layer::kGateway, "kGatewayNotAllowlisted"},
  {kGatewayConflict, Layer::kGateway, "kGatewayConflict"},
  {kGatewayRateLimited, Layer::kGateway, "kGatewayRateLimited"},
  {kGatewayCancelFailed, Layer::kGateway, "kGatewayCancelFailed"},
  {kGatewayUnavailable, Layer::kGateway, "kGatewayUnavailable"},
  {kGatewayGoalResponseTimeout, Layer::kGateway, "kGatewayGoalResponseTimeout"},
};

}

std::uint16_t device_rejected(const std::uint8_t result_code)
{
  return static_cast<std::uint16_t>(kDeviceRejectedBase + result_code);
}

Layer layer_of(const std::uint16_t code)
{
  if (code >= kDeviceRejectedBase && code <= kDeviceRejectedMax) {
    return Layer::kDevice;
  }
  if (code >= 100 && code <= 199) {
    return Layer::kExecutor;
  }
  if (code >= 200 && code <= 299) {
    return Layer::kOrchestrator;
  }
  if (code >= 300 && code <= 399) {
    return Layer::kDevice;
  }
  if (code >= 400 && code <= 599) {
    return Layer::kGateway;
  }
  // 600–999 is deliberately empty: a value there is not a defined code, and
  // calling it "the gateway" is how the old overlap became invisible.
  return Layer::kNone;
}

std::string_view to_string(const Layer layer)
{
  switch (layer) {
    case Layer::kNone:
      return "none";
    case Layer::kExecutor:
      return "executor";
    case Layer::kOrchestrator:
      return "orchestrator";
    case Layer::kDevice:
      return "device";
    case Layer::kGateway:
      return "gateway";
  }
  return "none";
}

const Entry * table(std::size_t & count)
{
  count = sizeof(kTable) / sizeof(kTable[0]);
  return kTable;
}

}
}
