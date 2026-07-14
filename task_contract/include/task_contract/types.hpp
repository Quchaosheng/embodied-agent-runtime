#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace task_contract {

constexpr std::uint8_t kContractVersion = 1;
constexpr std::uint32_t kMinDeadlineS = 1;
constexpr std::uint32_t kMaxDeadlineS = 90;
inline constexpr std::array<std::string_view, 3> kContractTargets{"dock", "workbench", "home"};

enum class TaskAction : std::uint8_t {
  kNavigate = 1,
};

enum class TaskState : std::uint8_t {
  kIdle = 0,
  kValidating = 1,
  kDispatching = 2,
  kRunning = 3,
  kRecovering = 4,
  kSucceeded = 5,
  kCancelling = 6,
  kCancelled = 7,
  kSafeStop = 8,
  kFailed = 9,
};

enum class ErrorCode : std::uint8_t {
  kOk = 0,
  kInvalidJson = 10,
  kInvalidContract = 11,
  kUnsupportedAction = 12,
  kUnknownTarget = 13,
  kDeadlineInvalid = 14,
  kTaskAlreadyRunning = 15,
  kRobotNotReady = 16,
  kNavigationNotReady = 17,
  kNavRejected = 30,
  kNavAborted = 31,
  kTaskTimedOut = 32,
  kCancelUnconfirmed = 33,
  kRecoveryExhausted = 34,
};

struct TaskRequest {
  std::uint8_t contract_version{kContractVersion};
  TaskAction action{TaskAction::kNavigate};
  std::string task_id;
  std::string target;
  std::uint32_t deadline_s{0};
};

}  // namespace task_contract
