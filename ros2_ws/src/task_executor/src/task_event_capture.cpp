#include "task_executor/task_event_capture.hpp"

#include <cstdio>
#include <sstream>

namespace task_executor
{

namespace
{

// Escape a string for a JSON string literal. Identifiers arrive from the network,
// so an unescaped quote would produce a line the consumer refuses.
std::string escape(const std::string & value)
{
  std::string result;
  result.reserve(value.size());
  for (const char character : value) {
    switch (character) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (static_cast<unsigned char>(character) < 0x20U) {
          char buffer[7];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", character & 0xFF);
          result += buffer;
        } else {
          result += character;
        }
    }
  }
  return result;
}

std::string quoted(const std::string & value)
{
  return "\"" + escape(value) + "\"";
}

}

std::string TaskEventCapture::to_jsonl(const robot_task_interfaces::msg::TaskEvent & event)
{
  std::ostringstream line;
  line << "{\"stamp\":{\"sec\":" << event.stamp.sec
       << ",\"nanosec\":" << event.stamp.nanosec << "}"
       << ",\"task_id\":" << quoted(event.task_id)
       << ",\"target_id\":" << quoted(event.target_id)
       << ",\"request_id\":" << quoted(event.request_id)
       << ",\"workflow_id\":" << quoted(event.workflow_id)
       << ",\"goal_id\":" << quoted(event.goal_id)
       << ",\"action_status\":" << static_cast<unsigned>(event.action_status)
       << ",\"outcome\":" << static_cast<unsigned>(event.outcome)
       << ",\"error_code\":" << event.error_code
       << ",\"duration_ms\":" << event.duration_ms
       << ",\"message\":" << quoted(event.message)
       << "}\n";
  return line.str();
}

}
