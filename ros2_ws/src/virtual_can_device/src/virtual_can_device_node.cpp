#include "robot_task_interfaces/msg/device_state.hpp"
#include "runtime_can/protocol.hpp"
#include "runtime_can/socket_can.hpp"
#include "virtual_can_device/device_modes.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

#include "rclcpp/rclcpp.hpp"

class VirtualCanDeviceNode : public rclcpp::Node
{
public:
  using DeviceState = robot_task_interfaces::msg::DeviceState;

  VirtualCanDeviceNode()
  : Node("virtual_can_device")
  {
    interface_name_ = declare_parameter<std::string>("interface_name", "vcan0");
    device_id_ = declare_parameter<std::string>("device_id", "virtual_ecu_1");
    mode_ = declare_parameter<std::string>("mode", "normal");
    const auto configured_delay_ms = declare_parameter<std::int64_t>("delay_ms", 500);
    const auto configured_cache_capacity = declare_parameter<std::int64_t>(
      "replay_cache_capacity", 4096);
    // 0 disables the guard, which is what a deployment with no heartbeat source
    // wants; anything positive stops the device after that long without a beat.
    heartbeat_timeout_ms_ = declare_parameter<std::int64_t>("heartbeat_timeout_ms", 0);
    if (heartbeat_timeout_ms_ < 0 || heartbeat_timeout_ms_ > 60000) {
      throw std::invalid_argument("heartbeat_timeout_ms must be in [0, 60000]");
    }
    delay_ms_ = static_cast<int>(
      std::clamp<std::int64_t>(configured_delay_ms, 0, 60000));
    if (configured_cache_capacity < 1 || configured_cache_capacity > 65536) {
      throw std::invalid_argument("replay_cache_capacity must be in [1, 65536]");
    }
    replay_cache_capacity_ = static_cast<std::size_t>(configured_cache_capacity);
    if (!is_supported_mode(mode_)) {
      throw std::invalid_argument("unsupported virtual device mode: " + mode_);
    }

    state_publisher_ = create_publisher<DeviceState>("device_state", 10);

    std::string error;
    if (!socket_.open(interface_name_, error)) {
      throw std::runtime_error("failed to open " + interface_name_ + ": " + error);
    }
    if (!socket_.set_filter(runtime_can::kCommandFrameId, error)) {
      throw std::runtime_error("failed to set command filter: " + error);
    }

    running_ = true;
    io_thread_ = std::thread([this]() {receive_loop();});
    heartbeat_timer_ = create_wall_timer(
      std::chrono::milliseconds(50), [this]() {check_heartbeat();});
    RCLCPP_INFO(
      get_logger(), "Ready on %s mode=%s delay_ms=%d replay_cache_capacity=%zu",
      interface_name_.c_str(), mode_.c_str(), delay_ms_, replay_cache_capacity_);
  }

  ~VirtualCanDeviceNode() override
  {
    running_ = false;
    if (io_thread_.joinable()) {
      io_thread_.join();
    }
  }

private:
  struct CacheEntry
  {
    runtime_can::Command command;
    std::optional<runtime_can::Response> response;
  };

  static bool is_supported_mode(const std::string & mode)
  {
    // The list lives in virtual_can_device/device_modes.hpp so a launch file can
    // generate its help text from it instead of restating it and drifting.
    return virtual_can_device::is_supported(mode);
  }

  void receive_loop()
  {
    using namespace std::chrono_literals;
    while (running_ && rclcpp::ok()) {
      runtime_can::RawFrame frame;
      std::string error;
      const auto status = socket_.receive(frame, 100ms, error);
      if (status == runtime_can::ReceiveStatus::kTimeout) {
        continue;
      }
      if (status == runtime_can::ReceiveStatus::kError) {
        RCLCPP_ERROR(get_logger(), "CAN receive failed: %s", error.c_str());
        continue;
      }

      // The frame kind decides what arrived, so a session-start or a heartbeat is
      // never mistaken for a command.
      if (runtime_can::kind_of(frame.id) == runtime_can::kSessionStartFrameId) {
        runtime_can::DecodeError start_error;
        const auto start = runtime_can::decode_session_start(frame, start_error);
        if (start) {
          start_session(*start);
        } else {
          RCLCPP_WARN(
            get_logger(), "Ignoring invalid session frame: %s",
            runtime_can::to_string(start_error).data());
        }
        continue;
      }
      if (runtime_can::kind_of(frame.id) == runtime_can::kHeartbeatFrameId) {
        runtime_can::DecodeError heartbeat_error;
        const auto heartbeat = runtime_can::decode_heartbeat(frame, heartbeat_error);
        if (heartbeat) {
          note_heartbeat(*heartbeat);
        } else {
          RCLCPP_WARN(
            get_logger(), "Ignoring invalid heartbeat frame: %s",
            runtime_can::to_string(heartbeat_error).data());
        }
        continue;
      }

      runtime_can::DecodeError decode_error;
      const auto command = runtime_can::decode_command(frame, decode_error);
      if (!command) {
        RCLCPP_WARN(
          get_logger(), "Ignoring invalid command frame: %s",
          runtime_can::to_string(decode_error).data());
        continue;
      }
      process_command(*command);
    }
  }

  void process_command(const runtime_can::Command & command)
  {
    // STOP is never served from the cache and never cached. A command identity is
    // (session, command_id) now, but a STOP must execute unconditionally even so:
    // replaying a STOP because "we already saw that id" would report a stop the
    // device did not perform.
    if (command.opcode == runtime_can::kStopOpcode) {
      handle_stop(command);
      return;
    }
    const runtime_can::CommandKey key{command.session, command.command_id};
    const auto cached = cache_.find(key);
    if (cached != cache_.end()) {
      if (!(cached->second.command == command)) {
        runtime_can::Response conflict{
          command.command_id, 3, DeviceState::FAULT, {0, 0, 0}, command.session};
        send_response(conflict);
        publish_state(command.command_id, DeviceState::FAULT, 3);
        return;
      }
      if (cached->second.response) {
        send_response(*cached->second.response);
      }
      return;
    }

    if (mode_ == "drop_ack" || mode_ == "drop_stop_ack") {
      cache_command(command, std::nullopt);
      publish_state(command.command_id, DeviceState::BUSY, 0);
      RCLCPP_WARN(get_logger(), "Dropping ACK command_id=%u", command.command_id);
      return;
    }

    runtime_can::Response response;
    response.command_id = command.command_id;
    if (mode_ == "reject") {
      response.result_code = 1;
      response.device_mode = DeviceState::NORMAL;
    } else if (mode_ == "fault") {
      response.result_code = 2;
      response.device_mode = DeviceState::FAULT;
    } else {
      response.result_code = 0;
      response.device_mode = DeviceState::NORMAL;
    }

    cache_command(command, response);
    if (mode_ == "delay_ack") {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
    }
    send_response(response);
    publish_state(
      command.command_id, response.device_mode,
      response.result_code == 2 ? 1 : 0);
    if (response.result_code == 0) {
      publish_completion(command.command_id);
    }
  }

  // The ACK above only says the command arrived. These modes then report what
  // the device does with it, on the separate status frame, so a test can tell
  // "acknowledged" from "finished" instead of conflating them.
  void publish_completion(const std::uint16_t command_id)
  {
    using runtime_can::Completion;
    if (mode_ == "completion_never_finishes") {
      send_status(command_id, Completion::kAccepted, 0);
      send_status(command_id, Completion::kExecuting, 0);
      return;
    }
    if (mode_ == "completion_fault") {
      send_status(command_id, Completion::kAccepted, 0);
      send_status(command_id, Completion::kFaulted, 1);
      return;
    }
    if (mode_ != "completion") {
      return;
    }
    send_status(command_id, Completion::kAccepted, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(std::max(delay_ms_, 0)));
    send_status(command_id, Completion::kExecuting, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(std::max(delay_ms_, 0)));
    send_status(command_id, Completion::kCompleted, 0);
  }

  void send_status(
    const std::uint16_t command_id, const runtime_can::Completion completion,
    const std::uint8_t fault_code)
  {
    runtime_can::StatusFrame status;
    status.command_id = command_id;
    status.completion = completion;
    status.fault_code = fault_code;
    std::string error;
    if (!socket_.send(runtime_can::encode_status(status), error)) {
      RCLCPP_ERROR(get_logger(), "CAN status send failed: %s", error.c_str());
      return;
    }
    RCLCPP_INFO(
      get_logger(), "Status command_id=%u state=%s fault_code=%u",
      command_id, runtime_can::to_string(completion).data(), fault_code);
  }

  // A STOP executes every time it arrives: it is never fetched from the cache and
  // never stored in it, so a repeated id cannot turn into a reported stop that
  // did not happen.
  void handle_stop(const runtime_can::Command & command)
  {
    if (mode_ == "drop_stop_ack") {
      publish_state(command.command_id, DeviceState::BUSY, 0);
      RCLCPP_WARN(get_logger(), "Dropping STOP ACK command_id=%u", command.command_id);
      return;
    }

    runtime_can::Response stop_response;
    stop_response.command_id = command.command_id;
    stop_response.result_code = 0;
    stop_response.device_mode = DeviceState::STOPPED;
    stop_response.session = command.session;
    send_response(stop_response);
    publish_state(command.command_id, DeviceState::STOPPED, 0);
    RCLCPP_INFO(
      get_logger(), "STOP acknowledged command_id=%u original_command_id=%d session=%u",
      command.command_id, command.argument, command.session);
  }

  void cache_command(
    const runtime_can::Command & command,
    const std::optional<runtime_can::Response> & response)
  {
    // A STOP is never cached, so its identity cannot be replayed at all.
    if (command.opcode == runtime_can::kStopOpcode) {
      return;
    }
    const runtime_can::CommandKey key{command.session, command.command_id};
    if (cache_.size() >= replay_cache_capacity_) {
      cache_.erase(cache_order_.front());
      cache_order_.pop_front();
    }
    cache_.emplace(key, CacheEntry{command, response});
    cache_order_.push_back(key);
  }

  // The only mechanism that lets the device stop on its own: without it, a wedge
  // in the runtime is indistinguishable from a busy one and the device keeps
  // executing. A missed beat is counted; reaching the bound stops the device.
  void note_heartbeat(const runtime_can::Heartbeat & heartbeat)
  {
    if (heartbeat.session != session_) {
      // A heartbeat from a session we have not started is a runtime that has not
      // announced itself yet; it is not evidence of liveness for this session.
      RCLCPP_WARN(
        get_logger(), "Ignoring heartbeat for session %u (current %u)",
        heartbeat.session, session_);
      return;
    }
    last_heartbeat_ns_ = std::chrono::steady_clock::now();
    missed_heartbeats_ = 0;
    heartbeat_count_ = heartbeat.counter;
  }

  // Called on the io thread: a device that has heard nothing for
  // heartbeat_timeout_ms stops on its own and reports why.
  void check_heartbeat()
  {
    if (heartbeat_timeout_ms_ <= 0 || last_heartbeat_ns_.time_since_epoch().count() == 0) {
      return;
    }
    const auto silent = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - last_heartbeat_ns_);
    if (silent <= std::chrono::milliseconds(heartbeat_timeout_ms_)) {
      missed_heartbeats_ = 0;
      return;
    }
    if (heartbeat_latched_) {
      return;
    }
    heartbeat_latched_ = true;
    ++missed_heartbeats_;
    publish_state(0, DeviceState::STOPPED, kHeartbeatLostFaultCode);
    RCLCPP_ERROR(
      get_logger(),
      "No heartbeat for %ld ms (session %u); device stopped on its own",
      static_cast<long>(silent.count()), session_);
  }

  // Drops every entry from a session other than the announced one. The bridge
  // sends this when its process starts, which is what makes a restarted bridge
  // unable to receive a stale ACK for an id it happens to reuse.
  void start_session(const runtime_can::SessionStart & start)
  {
    std::size_t dropped = 0;
    for (auto it = cache_.begin(); it != cache_.end(); ) {
      if (it->first.session != start.session) {
        it = cache_.erase(it);
        ++dropped;
      } else {
        ++it;
      }
    }
    cache_order_.clear();
    for (const auto & entry : cache_) {
      cache_order_.push_back(entry.first);
    }
    session_ = start.session;
    publish_state(0, DeviceState::NORMAL, 0);
    RCLCPP_INFO(
      get_logger(), "Session %u started; dropped %zu cached commands from other sessions",
      start.session, dropped);
  }

  void send_response(const runtime_can::Response & response)
  {
    std::string error;
    if (!socket_.send(runtime_can::encode_response(response), error)) {
      RCLCPP_ERROR(get_logger(), "CAN response send failed: %s", error.c_str());
      return;
    }
    ++response_count_;
    RCLCPP_INFO(
      get_logger(), "Response command_id=%u result=%u count=%u",
      response.command_id, response.result_code, response_count_);
  }

  void publish_state(std::uint16_t command_id, std::uint8_t mode, std::uint16_t fault_code)
  {
    DeviceState state;
    state.stamp = get_clock()->now();
    state.device_id = device_id_;
    state.mode = mode;
    state.fault_code = fault_code;
    state.last_command_id = command_id;
    state_publisher_->publish(state);
  }

  std::string interface_name_;
  std::string device_id_;
  std::string mode_;
  int delay_ms_{0};
  std::size_t replay_cache_capacity_{4096};
  std::atomic_bool running_{false};
  std::uint32_t response_count_{0};
  runtime_can::SocketCan socket_;
  rclcpp::Publisher<DeviceState>::SharedPtr state_publisher_;
  std::thread io_thread_;
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  std::deque<runtime_can::CommandKey> cache_order_;
  std::uint8_t session_{1};
  std::int64_t heartbeat_timeout_ms_{0};
  static constexpr std::uint16_t kHeartbeatLostFaultCode = 0x7E01;
  bool heartbeat_latched_{false};
  std::uint32_t missed_heartbeats_{0};
  std::uint8_t heartbeat_count_{0};
  std::chrono::steady_clock::time_point last_heartbeat_ns_{};
  std::map<runtime_can::CommandKey, CacheEntry> cache_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    rclcpp::spin(std::make_shared<VirtualCanDeviceNode>());
  } catch (const std::exception & error) {
    RCLCPP_FATAL(rclcpp::get_logger("virtual_can_device"), "%s", error.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
