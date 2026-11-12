#include "runtime_capabilities/manifest.hpp"

#include <yaml-cpp/yaml.h>

#include <limits>
#include <stdexcept>
#include <string>

namespace runtime_capabilities
{
namespace
{

[[noreturn]] void fail(const std::string & detail)
{
  throw std::runtime_error("capability manifest: " + detail);
}

std::int64_t positive_int(const YAML::Node & node, const std::string & what)
{
  if (!node || !node.IsScalar()) {
    fail(what + " must be a scalar");
  }
  std::int64_t value{};
  try {
    value = node.as<std::int64_t>();
  } catch (const YAML::Exception & error) {
    fail(what + " is not an integer: " + error.what());
  }
  if (value <= 0) {
    fail(what + " must be positive");
  }
  return value;
}

}  // namespace

Manifest Manifest::load(const std::string & path)
{
  YAML::Node root;
  try {
    root = YAML::LoadFile(path);
  } catch (const YAML::Exception & error) {
    fail("cannot read " + path + ": " + error.what());
  }

  const auto version = root["version"];
  if (!version || version.as<std::int64_t>() != 1) {
    fail("unsupported or missing version (expected 1)");
  }

  Manifest manifest;

  const auto workflows = root["workflows"];
  if (!workflows || !workflows.IsMap() || workflows.size() == 0) {
    fail("workflows must be a non-empty map");
  }
  for (const auto & entry : workflows) {
    Workflow workflow;
    workflow.id = entry.first.as<std::string>();
    if (workflow.id.empty()) {
      fail("a workflow id is empty");
    }
    const auto body = entry.second;
    workflow.requires_ready = body["requires_ready"] ?
      body["requires_ready"].as<bool>() : true;
    const auto steps = body["steps"];
    if (!steps || !steps.IsSequence() || steps.size() == 0) {
      fail("workflow " + workflow.id + " has no steps");
    }
    for (const auto & step : steps) {
      workflow.steps.push_back(step.as<std::string>());
    }
    manifest.workflows_.push_back(std::move(workflow));
  }

  const auto targets = root["targets"];
  if (!targets || !targets.IsMap() || targets.size() == 0) {
    fail("targets must be a non-empty map");
  }
  for (const auto & entry : targets) {
    const auto id = entry.first.as<std::string>();
    if (id.empty()) {
      fail("a target id is empty");
    }
    const auto argument = entry.second["argument"];
    if (!argument) {
      fail("target " + id + " has no argument");
    }
    const auto value = argument.as<std::int64_t>();
    if (value < std::numeric_limits<std::int32_t>::min() ||
      value > std::numeric_limits<std::int32_t>::max())
    {
      fail("target " + id + " argument exceeds int32");
    }
    manifest.target_ids_.push_back(id);
    manifest.target_arguments_.emplace(id, static_cast<std::int32_t>(value));
  }

  const auto commands = root["device_commands"];
  if (!commands || !commands.IsSequence() || commands.size() == 0) {
    fail("device_commands must be a non-empty sequence");
  }
  for (const auto & entry : commands) {
    DeviceCommand command;
    const auto opcode = positive_int(entry["opcode"], "device_commands.opcode");
    if (opcode > 255) {
      fail("device_commands.opcode exceeds 255");
    }
    command.opcode = static_cast<std::uint8_t>(opcode);
    command.name = entry["name"] ? entry["name"].as<std::string>() : std::string{};
    const auto arguments = entry["arguments"];
    if (!arguments || !arguments.IsSequence() || arguments.size() == 0) {
      fail("device command opcode " + std::to_string(opcode) + " declares no arguments");
    }
    for (const auto & argument : arguments) {
      const auto value = argument.as<std::int64_t>();
      if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max())
      {
        fail("a device command argument exceeds int32");
      }
      command.arguments.push_back(static_cast<std::int32_t>(value));
    }
    command.max_duration_ms = entry["max_duration_ms"] ?
      positive_int(entry["max_duration_ms"], "device_commands.max_duration_ms") :
      manifest.limits_.max_duration_ms;
    manifest.commands_.push_back(std::move(command));
  }

  const auto limits = root["limits"];
  if (limits) {
    if (limits["max_duration_ms"]) {
      manifest.limits_.max_duration_ms =
        positive_int(limits["max_duration_ms"], "limits.max_duration_ms");
    }
    if (limits["max_steps"]) {
      manifest.limits_.max_steps =
        static_cast<std::size_t>(positive_int(limits["max_steps"], "limits.max_steps"));
    }
  }
  // A command may not claim a longer timeout than the global bound; otherwise
  // the entry points would enforce two different ceilings.
  for (const auto & command : manifest.commands_) {
    if (command.max_duration_ms > manifest.limits_.max_duration_ms) {
      fail(
        "device command opcode " + std::to_string(command.opcode) +
        " allows longer than limits.max_duration_ms");
    }
  }

  return manifest;
}

bool Manifest::has_workflow(const std::string & workflow_id) const
{
  for (const auto & workflow : workflows_) {
    if (workflow.id == workflow_id) {
      return true;
    }
  }
  return false;
}

bool Manifest::has_target(const std::string & target_id) const
{
  return target_arguments_.find(target_id) != target_arguments_.end();
}

std::optional<std::int32_t> Manifest::target_argument(const std::string & target_id) const
{
  const auto found = target_arguments_.find(target_id);
  if (found == target_arguments_.end()) {
    return std::nullopt;
  }
  return found->second;
}

bool Manifest::duration_is_allowed(const std::int64_t duration_ms) const
{
  return duration_ms > 0 && duration_ms <= limits_.max_duration_ms;
}

bool Manifest::allows_command(
  const std::uint8_t opcode, const std::int32_t argument, const std::int64_t duration_ms) const
{
  if (!duration_is_allowed(duration_ms)) {
    return false;
  }
  for (const auto & command : commands_) {
    if (command.opcode != opcode) {
      continue;
    }
    if (duration_ms > command.max_duration_ms) {
      return false;
    }
    for (const auto argument_value : command.arguments) {
      if (argument_value == argument) {
        return true;
      }
    }
    return false;
  }
  return false;
}

}
