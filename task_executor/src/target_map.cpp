#include "task_executor/target_map.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_set>

#include "task_contract/types.hpp"

namespace task_executor {
namespace {

YAML::Node require_child(const YAML::Node& parent, const char* name) {
  const auto node = parent[name];
  if (!node) {
    throw std::runtime_error(std::string("missing required target field: ") + name);
  }
  return node;
}

double finite_double(const YAML::Node& node, const char* name) {
  const auto value = node.as<double>();
  if (!std::isfinite(value)) {
    throw std::runtime_error(std::string("target field must be finite: ") + name);
  }
  return value;
}

bool is_contract_target(const std::string& target) {
  return std::find(task_contract::kContractTargets.begin(), task_contract::kContractTargets.end(),
                   target) != task_contract::kContractTargets.end();
}

}  // namespace

TargetMap load_targets_from_yaml(const std::string& path) {
  try {
    const auto root = YAML::LoadFile(path);
    if (!root.IsMap()) {
      throw std::runtime_error("target configuration root must be a map");
    }

    const auto targets_node = require_child(root, "targets");
    if (!targets_node.IsMap() || targets_node.size() == 0) {
      throw std::runtime_error("targets must be a non-empty map");
    }

    TargetMap targets;
    std::unordered_set<std::string> observed;
    for (const auto& entry : targets_node) {
      const auto target = entry.first.as<std::string>();
      if (!is_contract_target(target)) {
        throw std::runtime_error("target is outside the contract: " + target);
      }
      if (!observed.insert(target).second) {
        throw std::runtime_error("target is duplicated: " + target);
      }

      const auto pose = entry.second;
      if (!pose.IsMap() || pose.size() != 4) {
        throw std::runtime_error("target pose must contain exactly frame_id, x, y, and yaw: " +
                                 target);
      }

      NamedTarget named_target;
      named_target.frame_id = require_child(pose, "frame_id").as<std::string>();
      if (named_target.frame_id != "map") {
        throw std::runtime_error("target frame_id must be map: " + target);
      }
      named_target.x = finite_double(require_child(pose, "x"), "x");
      named_target.y = finite_double(require_child(pose, "y"), "y");
      named_target.yaw = finite_double(require_child(pose, "yaw"), "yaw");
      constexpr double kPi = 3.14159265358979323846;
      if (named_target.yaw < -kPi || named_target.yaw > kPi) {
        throw std::runtime_error("target yaw must be within [-pi, pi]: " + target);
      }
      targets.emplace(target, std::move(named_target));
    }

    for (const auto contract_target : task_contract::kContractTargets) {
      if (targets.count(std::string(contract_target)) == 0) {
        throw std::runtime_error("target configuration is missing: " +
                                 std::string(contract_target));
      }
    }
    return targets;
  } catch (const YAML::Exception& error) {
    throw std::runtime_error("failed to load target configuration '" + path + "': " + error.what());
  }
}

}  // namespace task_executor
