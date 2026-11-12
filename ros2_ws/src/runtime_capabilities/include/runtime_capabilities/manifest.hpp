#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace runtime_capabilities
{

// What a workflow may do. `requires_ready` is enforced by the behavior tree, so
// it is declared here for the reader and for a future tree that reads it; the
// current trees are fixed XML.
struct Workflow
{
  std::string id;
  bool requires_ready{true};
  std::vector<std::string> steps;
};

// A device command the bus may carry. The argument set is closed on purpose: a
// device that rejects most of the int32 range and one that accepts it look the
// same until something sends a value outside it.
struct DeviceCommand
{
  std::uint8_t opcode{0};
  std::string name;
  std::vector<std::int32_t> arguments;
  std::int64_t max_duration_ms{30000};
};

struct Limits
{
  std::int64_t max_duration_ms{30000};
  std::size_t max_steps{1};
};

// The single description of what the runtime is allowed to do.
class Manifest
{
public:
  // Throws std::runtime_error when the file is missing, unparseable, or
  // self-inconsistent. A capability set that silently loads as empty would
  // reject everything, which reads like a fault rather than a config error.
  static Manifest load(const std::string & path);

  bool has_workflow(const std::string & workflow_id) const;
  bool has_target(const std::string & target_id) const;
  // The int32 the executor sends for this target. Empty for an unknown target.
  std::optional<std::int32_t> target_argument(const std::string & target_id) const;
  // True when this opcode exists, this argument is declared for it, and the
  // requested timeout is within both the command's and the global limit.
  bool allows_command(
    std::uint8_t opcode, std::int32_t argument, std::int64_t duration_ms) const;
  bool duration_is_allowed(std::int64_t duration_ms) const;

  const std::vector<Workflow> & workflows() const {return workflows_;}
  const std::vector<std::string> & target_ids() const {return target_ids_;}
  const std::vector<DeviceCommand> & commands() const {return commands_;}
  const Limits & limits() const {return limits_;}

private:
  std::vector<Workflow> workflows_;
  std::vector<std::string> target_ids_;
  std::map<std::string, std::int32_t> target_arguments_;
  std::vector<DeviceCommand> commands_;
  Limits limits_;
};

}
