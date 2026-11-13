#pragma once

// The one list of software-ECU modes. It exists as a header because the launch
// file used to restate it in a help string, and drifted: it advertised
// `duplicate`, which the node rejects, and omitted `reject`, `completion_fault`
// and `completion_never_finishes`. A launch description that names a mode the
// device refuses makes `device_mode:=that` throw at node construction.

#include <string>
#include <vector>

namespace virtual_can_device
{

struct ModeInfo
{
  const char * name;
  const char * description;
};

inline const std::vector<ModeInfo> & modes()
{
  static const std::vector<ModeInfo> all{
    {"normal", "acknowledge and complete every command"},
    {"delay_ack", "acknowledge after delay_ms"},
    {"drop_ack", "never acknowledge"},
    {"drop_stop_ack", "acknowledge commands but never a STOP"},
    {"reject", "acknowledge with a non-zero result code"},
    {"fault", "report a latched device fault"},
    {"completion", "report execution separately: accepted, executing, completed"},
    {"completion_fault", "report execution separately and end in a fault"},
    {"completion_never_finishes", "report execution separately and never complete"},
  };
  return all;
}

inline bool is_supported(const std::string & mode)
{
  for (const auto & entry : modes()) {
    if (mode == entry.name) {
      return true;
    }
  }
  return false;
}

// The help text for a launch argument, built from the list above so it cannot
// advertise a mode the node rejects.
inline std::string mode_help()
{
  std::string help = "Mode of the software ECU: ";
  for (std::size_t index = 0; index < modes().size(); ++index) {
    if (index != 0) {
      help += ", ";
    }
    help += modes()[index].name;
  }
  return help + ".";
}

}
