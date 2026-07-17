#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "device_bridge/heartbeat_monitor.hpp"
#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "diagnostic_msgs/msg/key_value.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/bool.hpp"

using namespace std::chrono_literals;

class SocketCanHeartbeatNode : public rclcpp::Node {
 public:
  SocketCanHeartbeatNode()
      : Node("socketcan_heartbeat_node"),
        interface_(declare_parameter<std::string>("interface", "vcan0")),
        heartbeat_can_id_(declare_parameter<int>("heartbeat_can_id", 0x321)),
        heartbeat_timeout_ms_(declare_parameter<int>("heartbeat_timeout_ms", 500)),
        monitor_(checked_can_id(heartbeat_can_id_), checked_timeout(heartbeat_timeout_ms_)) {
    if (interface_.empty() || interface_.size() >= IFNAMSIZ) {
      throw std::invalid_argument("SocketCAN interface name is invalid");
    }
    ready_publisher_ = create_publisher<std_msgs::msg::Bool>("device_ready", 10);
    diagnostics_publisher_ =
        create_publisher<diagnostic_msgs::msg::DiagnosticArray>("/diagnostics", 10);
    poll_timer_ = create_wall_timer(20ms, [this]() { poll(); });
    diagnostics_timer_ = create_wall_timer(1s, [this]() { publish_diagnostics(); });
  }

  ~SocketCanHeartbeatNode() override { close_socket(); }

 private:
  static std::uint32_t checked_can_id(int can_id) {
    if (can_id < 0 || can_id > 0x7FF) {
      throw std::invalid_argument("heartbeat_can_id must be within [0, 0x7ff]");
    }
    return static_cast<std::uint32_t>(can_id);
  }

  static std::chrono::milliseconds checked_timeout(int timeout_ms) {
    if (timeout_ms <= 0) {
      throw std::invalid_argument("heartbeat_timeout_ms must be positive");
    }
    return std::chrono::milliseconds(timeout_ms);
  }

  void close_socket() {
    if (socket_fd_ >= 0) {
      close(socket_fd_);
      socket_fd_ = -1;
    }
  }

  bool open_socket() {
    const auto now = device_bridge::HeartbeatMonitor::Clock::now();
    if (now - last_open_attempt_ < 1s) {
      return false;
    }
    last_open_attempt_ = now;

    const int socket_fd = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
    if (socket_fd < 0) {
      socket_error_ = std::strerror(errno);
      return false;
    }
    const unsigned int interface_index = if_nametoindex(interface_.c_str());
    if (interface_index == 0U) {
      socket_error_ = "interface not found";
      close(socket_fd);
      return false;
    }

    can_filter filter{};
    filter.can_id = heartbeat_can_id_;
    filter.can_mask = CAN_SFF_MASK;
    if (setsockopt(socket_fd, SOL_CAN_RAW, CAN_RAW_FILTER, &filter, sizeof(filter)) < 0) {
      socket_error_ = std::strerror(errno);
      close(socket_fd);
      return false;
    }

    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = static_cast<int>(interface_index);
    if (bind(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
      socket_error_ = std::strerror(errno);
      close(socket_fd);
      return false;
    }
    socket_fd_ = socket_fd;
    socket_error_.clear();
    return true;
  }

  void poll() {
    if (socket_fd_ < 0 && !open_socket()) {
      publish_ready(false);
      return;
    }

    can_frame frame{};
    while (true) {
      const auto received = read(socket_fd_, &frame, sizeof(frame));
      if (received == static_cast<ssize_t>(sizeof(frame))) {
        const bool valid_type =
            (frame.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) == 0U;
        if (valid_type) {
          monitor_.observe(frame.can_id & CAN_SFF_MASK, frame.data, frame.can_dlc,
                           device_bridge::HeartbeatMonitor::Clock::now());
        }
        continue;
      }
      if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        break;
      }
      socket_error_ = received == 0 ? "SocketCAN interface closed" : std::strerror(errno);
      close_socket();
      break;
    }
    publish_ready(socket_fd_ >= 0 && monitor_.ready(device_bridge::HeartbeatMonitor::Clock::now()));
  }

  void publish_ready(bool ready) {
    std_msgs::msg::Bool message;
    message.data = ready;
    ready_publisher_->publish(message);
    last_published_ready_ = ready;
  }

  static diagnostic_msgs::msg::KeyValue value(const std::string& key, const std::string& content) {
    diagnostic_msgs::msg::KeyValue result;
    result.key = key;
    result.value = content;
    return result;
  }

  std::string can_id_text() const {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << heartbeat_can_id_;
    return stream.str();
  }

  void publish_diagnostics() {
    const auto now = device_bridge::HeartbeatMonitor::Clock::now();
    diagnostic_msgs::msg::DiagnosticStatus status;
    status.name = get_fully_qualified_name() + std::string("/heartbeat");
    status.hardware_id = interface_;
    if (socket_fd_ < 0) {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::ERROR;
      status.message = "SocketCAN interface unavailable: " + socket_error_;
    } else if (!last_published_ready_) {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::ERROR;
      status.message = "controller heartbeat missing, stale, or not ready";
    } else {
      status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
      status.message = "controller heartbeat ready";
    }
    const auto age = monitor_.age(now);
    status.values.push_back(value("interface", interface_));
    status.values.push_back(value("heartbeat_can_id", can_id_text()));
    status.values.push_back(value("device_ready", last_published_ready_ ? "true" : "false"));
    status.values.push_back(value("heartbeat_timeout_ms", std::to_string(heartbeat_timeout_ms_)));
    status.values.push_back(value("heartbeat_age_ms",
                                  age == std::chrono::milliseconds::max() ? "never"
                                                                          : std::to_string(age.count())));
    status.values.push_back(value("accepted_frames", std::to_string(monitor_.accepted_frames())));
    status.values.push_back(value("rejected_frames", std::to_string(monitor_.rejected_frames())));

    diagnostic_msgs::msg::DiagnosticArray message;
    message.header.stamp = this->now();
    message.status.push_back(std::move(status));
    diagnostics_publisher_->publish(message);
  }

  std::string interface_;
  std::uint32_t heartbeat_can_id_;
  int heartbeat_timeout_ms_;
  device_bridge::HeartbeatMonitor monitor_;
  int socket_fd_{-1};
  std::string socket_error_{"not opened"};
  bool last_published_ready_{false};
  device_bridge::HeartbeatMonitor::TimePoint last_open_attempt_{};
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr ready_publisher_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diagnostics_publisher_;
  rclcpp::TimerBase::SharedPtr poll_timer_;
  rclcpp::TimerBase::SharedPtr diagnostics_timer_;
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<SocketCanHeartbeatNode>());
  rclcpp::shutdown();
  return 0;
}
