#pragma once

#include <string>
#include <unordered_map>

namespace task_executor {

struct NamedTarget {
  std::string frame_id;
  double x{0.0};
  double y{0.0};
  double yaw{0.0};
};

using TargetMap = std::unordered_map<std::string, NamedTarget>;

TargetMap load_targets_from_yaml(const std::string& path);

}  // namespace task_executor
