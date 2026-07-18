#pragma once

#include "builtin_interfaces/msg/time.hpp"
#include "robot_task_interfaces/msg/task_event.hpp"

#include <cstdint>
#include <string>

namespace task_executor
{

robot_task_interfaces::msg::TaskEvent make_task_event(
  const std::string & task_id,
  const std::string & target_id,
  std::uint8_t action_status,
  std::uint8_t outcome,
  std::uint16_t error_code,
  std::uint64_t duration_ms,
  const std::string & message,
  const builtin_interfaces::msg::Time & stamp);

}  // namespace task_executor
