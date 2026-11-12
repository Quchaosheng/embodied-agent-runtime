#pragma once

#include "robot_task_interfaces/msg/task_event.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <mutex>
#include <string>

namespace task_executor
{

// Append the terminal TaskEvent to a JSONL capture so a consumer in another
// repository can bind it to its own run without a live DDS subscription.
//
// The field names are the contract: workbench-mobile-home-robot's
// `integrations/ros2_runtime/task_events.py` reads exactly these keys. Keep the
// two in step; `test_task_event_capture` asserts this writer's output.
class TaskEventCapture
{
public:
  explicit TaskEventCapture(const std::string & path)
  {
    if (path.empty()) {
      return;
    }
    fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  }

  ~TaskEventCapture()
  {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  TaskEventCapture(const TaskEventCapture &) = delete;
  TaskEventCapture & operator=(const TaskEventCapture &) = delete;

  bool enabled() const {return fd_ >= 0;}

  // Best-effort: capture must never change a task's terminal outcome, so a write
  // failure is reported to the caller and the task still finishes.
  bool append(const robot_task_interfaces::msg::TaskEvent & event)
  {
    if (fd_ < 0) {
      return false;
    }
    const std::string line = to_jsonl(event);
    const std::lock_guard<std::mutex> lock(mutex_);
    return write_all(line.data(), line.size());
  }

  static std::string to_jsonl(const robot_task_interfaces::msg::TaskEvent & event);

private:
  bool write_all(const char * data, std::size_t size)
  {
    std::size_t written = 0;
    while (written < size) {
      const auto result = ::write(fd_, data + written, size - written);
      if (result <= 0) {
        // A short write would leave a half line in the capture, which the
        // consumer must refuse rather than repair; report and stop.
        return false;
      }
      written += static_cast<std::size_t>(result);
    }
    return true;
  }

  int fd_{-1};
  std::mutex mutex_;
};

}
