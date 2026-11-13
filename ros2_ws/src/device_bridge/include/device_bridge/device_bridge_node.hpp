#pragma once

#include "device_bridge/command_policy.hpp"
#include "device_bridge/diagnostic_state.hpp"
#include "runtime_history/can_stage_names.hpp"
#include "runtime_history/stage_event_log.hpp"

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "robot_task_interfaces/action/execute_device_command.hpp"
#include "robot_task_interfaces/msg/device_state.hpp"
#include "runtime_can/can_link.hpp"
#include "runtime_can/protocol.hpp"
#include "runtime_can/socket_can.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

class DeviceBridgeNode : public rclcpp::Node
{
public:
  using ExecuteDeviceCommand = robot_task_interfaces::action::ExecuteDeviceCommand;
  using GoalHandle = rclcpp_action::ServerGoalHandle<ExecuteDeviceCommand>;

  // The transport is a constructor argument so a test can drive frames without a
  // CAN interface; production passes nothing and gets the real socket.
  explicit DeviceBridgeNode(
    std::shared_ptr<runtime_can::Transport> transport = nullptr,
    const rclcpp::NodeOptions & options = rclcpp::NodeOptions())
  : Node("device_bridge", options)
  {
    interface_name_ = declare_parameter<std::string>("interface_name", "vcan0");
    const auto configured_max_timeout = declare_parameter<std::int64_t>(
      "max_ack_timeout_ms", 5000);
    const auto configured_poll = declare_parameter<std::int64_t>("receive_poll_ms", 25);
    const auto configured_stop_timeout = declare_parameter<std::int64_t>(
      "stop_timeout_ms", 500);
    diagnostic_period_ms_ = declare_parameter<std::int64_t>("diagnostic_period_ms", 1000);
    // A device that reports execution separately is opted into explicitly: a
    // protocol that only ACKs would otherwise be waited on forever.
    completion_states_enabled_ = declare_parameter<bool>("completion_states_enabled", false);
    const auto configured_completion_timeout = declare_parameter<std::int64_t>(
      "completion_timeout_ms", 30000);
    stage_log_ = std::make_unique<runtime_history::StageEventLog>(
      declare_parameter<std::string>("stage_event_log_path", ""));
    if (configured_max_timeout <= 0 || configured_stop_timeout <= 0 ||
      configured_stop_timeout > 60000)
    {
      throw std::invalid_argument("max_ack_timeout_ms must be positive");
    }
    if (configured_completion_timeout <= 0 || configured_completion_timeout > 600000) {
      throw std::invalid_argument("completion_timeout_ms must be in [1, 600000]");
    }
    completion_timeout_ = std::chrono::milliseconds(configured_completion_timeout);
    if (diagnostic_period_ms_ <= 0 || diagnostic_period_ms_ > 60000) {
      throw std::invalid_argument("diagnostic_period_ms must be in [1, 60000]");
    }
    max_ack_timeout_ms_ = configured_max_timeout;
    receive_poll_ms_ = std::chrono::milliseconds(std::clamp<std::int64_t>(
        configured_poll, 1, 1000));
    stop_timeout_ms_ = std::chrono::milliseconds(configured_stop_timeout);

    std::string error;
    if (transport == nullptr) {
      transport = std::make_shared<runtime_can::SocketCan>();
    }
    // A resident receive thread owns the socket: reading only inside a command's
    // wait loops left frames arriving while idle in the kernel queue, where the
    // next command read them and rejected them as unmatched.
    link_ = std::make_unique<runtime_can::CanLink>(transport, receive_poll_ms_);
    if (!link_->start(
        interface_name_, runtime_can::kResponseFrameId, runtime_can::kStatusFrameId, error))
    {
      throw std::runtime_error("failed to open " + interface_name_ + ": " + error);
    }

    diagnostics_publisher_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
      "/diagnostics", rclcpp::QoS(10).reliable());
    diagnostics_timer_ = create_wall_timer(
      std::chrono::milliseconds(diagnostic_period_ms_),
      [this]() {publish_diagnostics();});

    action_server_ = rclcpp_action::create_server<ExecuteDeviceCommand>(
      this,
      "execute_device_command",
      std::bind(&DeviceBridgeNode::handle_goal, this, std::placeholders::_1,
        std::placeholders::_2),
      std::bind(&DeviceBridgeNode::handle_cancel, this, std::placeholders::_1),
      std::bind(&DeviceBridgeNode::handle_accepted, this, std::placeholders::_1));

    RCLCPP_INFO(
      get_logger(),
      "Device Bridge ready on %s max_ack_timeout_ms=%ld stop_timeout_ms=%ld "
      "receive_poll_ms=%ld diagnostic_period_ms=%ld",
      interface_name_.c_str(), max_ack_timeout_ms_, stop_timeout_ms_.count(),
      receive_poll_ms_.count(), diagnostic_period_ms_);
  }

  // A cancel that arrives from outside the goal: SIGINT/SIGTERM has to stop the
  // device, not merely stop the process with the command still in flight.
  void request_shutdown()
  {
    shutting_down_ = true;
  }

  // True once the worker stopped on its own, which is what lets a test assert
  // that shutdown drained the command instead of aborting it.
  bool shutdown_complete() const
  {
    return worker_finished_;
  }

  ~DeviceBridgeNode() override
  {
    shutting_down_ = true;
    running_ = false;
    std::thread worker;
    {
      std::lock_guard<std::mutex> lock(workers_mutex_);
      worker.swap(worker_);
    }
    if (worker.joinable()) {
      worker.join();
    }
    if (link_ != nullptr) {
      link_->stop();
    }
  }

private:
  enum class WaitResult
  {
    kResponse,
    kTimeout,
    kCanceled,
    kTransportError,
    kShutdown
  };

  static constexpr std::uint8_t kMaxRetries = 1;

  rclcpp_action::GoalResponse handle_goal(
    const rclcpp_action::GoalUUID &,
    std::shared_ptr<const ExecuteDeviceCommand::Goal> goal)
  {
    std::chrono::milliseconds ack_timeout;
    if (!device_bridge::application_command_id_in_range(goal->command_id)) {
      // IDs at or above kStopCommandIdMin are STOP's, so an ordinary command there
      // could share an identity with a STOP ACK.
      RCLCPP_WARN(
        get_logger(),
        "Rejecting command_id=%u: application command IDs are [1, %u]; "
        "IDs at or above %u are reserved for STOP",
        goal->command_id, runtime_can::kApplicationCommandIdMax,
        runtime_can::kStopCommandIdMin);
      return rclcpp_action::GoalResponse::REJECT;
    }
    if (!device_bridge::duration_to_milliseconds(
        goal->ack_timeout, max_ack_timeout_ms_, ack_timeout))
    {
      RCLCPP_WARN(
        get_logger(), "Rejecting command_id=%u: invalid ack_timeout", goal->command_id);
      return rclcpp_action::GoalResponse::REJECT;
    }
    if (!link_->is_open()) {
      RCLCPP_WARN(
        get_logger(), "Rejecting command_id=%u: CAN socket is not ready",
        goal->command_id);
      return rclcpp_action::GoalResponse::REJECT;
    }

    bool expected = false;
    // A single active wire transaction keeps response matching unambiguous;
    // concurrent goals could otherwise consume each other's command ACKs.
    if (!active_goal_.compare_exchange_strong(expected, true)) {
      RCLCPP_WARN(get_logger(), "Rejecting command_id=%u: bridge is busy", goal->command_id);
      return rclcpp_action::GoalResponse::REJECT;
    }

    RCLCPP_INFO(
      get_logger(), "Accepting command_id=%u opcode=%u argument=%d ack_timeout_ms=%ld",
      goal->command_id, goal->opcode, goal->argument, ack_timeout.count());
    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  rclcpp_action::CancelResponse handle_cancel(const std::shared_ptr<GoalHandle> goal_handle)
  {
    RCLCPP_INFO(
      get_logger(), "Accepting cancel request for command_id=%u",
      goal_handle->get_goal()->command_id);
    return rclcpp_action::CancelResponse::ACCEPT;
  }

  void handle_accepted(const std::shared_ptr<GoalHandle> goal_handle)
  {
    join_completed_worker();
    worker_finished_ = false;
    diagnostic_state_.begin_command(
      static_cast<std::uint16_t>(goal_handle->get_goal()->command_id));
    std::lock_guard<std::mutex> lock(workers_mutex_);
    worker_ = std::thread([this, goal_handle]() {execute(goal_handle);});
  }

  void join_completed_worker()
  {
    std::thread worker;
    {
      const std::lock_guard<std::mutex> lock(workers_mutex_);
      worker.swap(worker_);
    }
    if (worker.joinable()) {
      worker.join();
    }
  }

  // Waiting moved into CanLink: the receive thread owns the socket, so this only
  // asks for the next frame matching this command. The cancel check is passed in
  // as a callback because the wait must poll it, not be woken by it.
  WaitResult receive_response(
    const std::shared_ptr<GoalHandle> & goal_handle,
    const std::uint16_t expected_command_id,
    const std::chrono::milliseconds timeout,
    const bool observe_cancel,
    runtime_can::Response & response,
    std::string & error)
  {
    const runtime_can::CanLink::CancelCheck canceled = observe_cancel ?
      runtime_can::CanLink::CancelCheck(
        [this, &goal_handle]() {
          return goal_handle->is_canceling() || shutting_down_.load();
        }) :
      runtime_can::CanLink::CancelCheck([this]() {return shutting_down_.load();});
    switch (link_->await_response(expected_command_id, timeout, canceled, response, error)) {
      case runtime_can::CanLink::Wait::kReceived:
        return WaitResult::kResponse;
      case runtime_can::CanLink::Wait::kTimeout:
        return WaitResult::kTimeout;
      case runtime_can::CanLink::Wait::kCanceled:
        return WaitResult::kCanceled;
      case runtime_can::CanLink::Wait::kBusOff:
        // A controller that is off the bus cannot carry the STOP either, so this
        // is reported as a transport failure with the real cause in the string.
        diagnostic_state_.record_bus_off();
        publish_diagnostics();
        error = "CAN controller is bus-off";
        return WaitResult::kTransportError;
      case runtime_can::CanLink::Wait::kTransportError:
        return WaitResult::kTransportError;
      case runtime_can::CanLink::Wait::kShutdown:
        return WaitResult::kShutdown;
    }
    error = "unreachable wait state";
    return WaitResult::kShutdown;
  }

  // Trace identity arrives in the goal because this node cannot see ExecuteTask.
  void emit_stage(
    const std::shared_ptr<const ExecuteDeviceCommand::Goal> & goal,
    const std::string & stage,
    const std::string & extra_json = "{}")
  {
    if (stage_log_ == nullptr || !stage_log_->enabled() || goal->trace_id.empty()) {
      return;
    }
    runtime_history::StageEvent event;
    event.trace_id = goal->trace_id;
    event.source_node = "device_bridge";
    event.stage = stage;
    event.event_name = stage;
    event.event_type = "can_bridge";
    event.extra_json = extra_json;
    stage_log_->emit(event);
  }

  void execute(const std::shared_ptr<GoalHandle> & goal_handle)
  {
    struct ActiveGuard
    {
      std::atomic_bool & active;
      std::atomic_bool & finished;
      ~ActiveGuard()
      {
        active = false;
        finished = true;
      }
    } active_guard{active_goal_, worker_finished_};

    const auto goal = goal_handle->get_goal();
    std::chrono::milliseconds ack_timeout;
    if (!device_bridge::duration_to_milliseconds(
        goal->ack_timeout, max_ack_timeout_ms_, ack_timeout))
    {
      auto result = std::make_shared<ExecuteDeviceCommand::Result>();
      result->outcome = ExecuteDeviceCommand::Result::DEVICE_FAULT;
      result->error_code = device_bridge::kErrorInvalidGoal;
      result->message = "ack_timeout became invalid before execution";
      goal_handle->abort(result);
      return;
    }

    const auto command = runtime_can::Command{
      static_cast<std::uint16_t>(goal->command_id), goal->opcode, goal->argument};
    bool command_sent = false;
    for (std::uint8_t attempt = 0; attempt <= kMaxRetries; ++attempt) {
      // Checked on every attempt, not only before the first: a cancel accepted
      // between an ACK timeout and the retry used to re-send the very command
      // the caller had already asked to stop.
      if (goal_handle->is_canceling() || shutting_down_) {
        if (!command_sent) {
          finish_canceled(goal_handle, "command canceled before CAN send");
        } else {
          // The command is already on the wire, so cancel means STOP, not
          // "pretend it never happened". A shutdown is not a caller's cancel, so
          // it is reported as a safe stop even though the device is stopped.
          stop_and_finish(
            goal_handle, command.command_id, !shutting_down_.load(), 0, "");
        }
        return;
      }

      publish_feedback(goal_handle, ExecuteDeviceCommand::Feedback::SENDING, attempt);
      diagnostic_state_.mark_sending();
      std::string send_error;
      // retry_count distinguishes attempts of one command inside the trace.
      const std::string attempt_extra =
        "{\"command_id\":" + std::to_string(command.command_id) +
        ",\"retry_count\":" + std::to_string(attempt) + "}";
      emit_stage(goal, runtime_history::can_stage::kEncodeStart, attempt_extra);
      const auto frame = runtime_can::encode_command(command);
      emit_stage(goal, runtime_history::can_stage::kEncodeEnd, attempt_extra);
      if (!link_->send(frame, send_error)) {
        diagnostic_state_.record_transport_error(device_bridge::kErrorTransport);
        publish_diagnostics();
        finish_aborted(
          goal_handle, ExecuteDeviceCommand::Result::SAFE_STOP,
          device_bridge::kErrorTransport, "CAN send failed: " + send_error);
        return;
      }
      command_sent = true;
      publish_feedback(goal_handle, ExecuteDeviceCommand::Feedback::WAITING_ACK, attempt);
      diagnostic_state_.mark_waiting_ack();
      emit_stage(goal, runtime_history::can_stage::kFrameSent, attempt_extra);
      emit_stage(goal, runtime_history::can_stage::kAckWaitStart, attempt_extra);

      runtime_can::Response response;
      std::string wait_error;
      const auto wait_result = receive_response(
        goal_handle, command.command_id, ack_timeout, true, response, wait_error);
      if (wait_result == WaitResult::kResponse) {
        if (goal_handle->is_canceling()) {
          stop_and_finish(goal_handle, command.command_id, true, 0, "");
          return;
        }
        if (response.result_code == 0) {
          diagnostic_state_.record_success();
          publish_diagnostics();
          emit_stage(goal, runtime_history::can_stage::kAckReceived, attempt_extra);
          // The ACK only says the command arrived. What the result means is
          // decided by whether this protocol reports execution separately.
          finish_acknowledged(goal_handle, command.command_id, completion_timeout_);
          return;
        }
        const auto error_code = device_bridge::device_error_code(response.result_code);
        diagnostic_state_.record_device_fault(error_code);
        publish_diagnostics();
        finish_aborted(
          goal_handle, ExecuteDeviceCommand::Result::DEVICE_FAULT,
          error_code,
          "device returned result_code=" + std::to_string(response.result_code));
        return;
      }
      if (wait_result == WaitResult::kCanceled) {
        if (!command_sent) {
          finish_canceled(goal_handle, "command canceled before CAN send");
        } else {
          stop_and_finish(
            goal_handle, command.command_id, !shutting_down_.load(), 0, "");
        }
        return;
      }
      if (wait_result == WaitResult::kTransportError) {
        diagnostic_state_.record_transport_error(device_bridge::kErrorTransport);
        publish_diagnostics();
        stop_and_finish(
          goal_handle, command.command_id, false, device_bridge::kErrorTransport,
          "CAN receive failed: " + wait_error + "; attempting device STOP");
        return;
      }
      if (wait_result == WaitResult::kShutdown) {
        stop_and_finish(
          goal_handle, command.command_id, false, device_bridge::kErrorShutdown,
          wait_error + "; attempting device STOP");
        return;
      }

      // An ACK timeout is evidence in its own right: without it the expiry of one
      // attempt is only a gap between two timestamps, which is indistinguishable
      // from a stalled process or a truncated log.
      emit_stage(goal, runtime_history::can_stage::kAckTimeout, attempt_extra);

      if (attempt < kMaxRetries) {
        {
          const std::string retry_extra =
            "{\"command_id\":" + std::to_string(command.command_id) +
            ",\"retry_count\":" + std::to_string(attempt + 1) + "}";
          emit_stage(goal, runtime_history::can_stage::kRetryScheduled, retry_extra);
        }
        publish_feedback(goal_handle, ExecuteDeviceCommand::Feedback::RETRYING, attempt + 1);
        diagnostic_state_.record_retry();
        publish_diagnostics();
        RCLCPP_WARN(
          get_logger(), "ACK timeout command_id=%u; retrying with same command_id",
          command.command_id);
        continue;
      }

      publish_feedback(goal_handle, ExecuteDeviceCommand::Feedback::STOPPING, attempt);
      diagnostic_state_.record_ack_timeout(device_bridge::kErrorAckTimeout);
      publish_diagnostics();
      emit_stage(goal, runtime_history::can_stage::kRetryExhausted, attempt_extra);
      stop_and_finish(
        goal_handle, command.command_id, false, device_bridge::kErrorAckTimeout,
        "command ACK timeout; attempting device STOP");
      return;
    }
  }

  // Called once the device ACKed the command. A protocol that reports execution
  // separately is waited on; a protocol that only acknowledges keeps the old
  // meaning, but the result says so explicitly instead of implying motion.
  void finish_acknowledged(
    const std::shared_ptr<GoalHandle> & goal_handle,
    const std::uint16_t command_id,
    const std::chrono::milliseconds completion_timeout)
  {
    if (device_bridge::classify_status(std::nullopt, completion_states_enabled_) ==
      device_bridge::Acknowledgement::kAckOnlyCompleted)
    {
      finish_completed(
        goal_handle, ExecuteDeviceCommand::Feedback::COMPLETION_ACK_ONLY,
        "device acknowledged command; this protocol has no completion state");
      return;
    }

    const runtime_can::CanLink::CancelCheck canceled =
      [this, &goal_handle]() {
        return goal_handle->is_canceling() || shutting_down_.load();
      };
    const auto deadline = std::chrono::steady_clock::now() + completion_timeout;
    while (true) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        break;
      }
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
      runtime_can::StatusFrame decoded;
      std::string wait_error;
      const auto wait = link_->await_status(command_id, remaining, canceled, decoded, wait_error);
      if (wait == runtime_can::CanLink::Wait::kCanceled) {
        stop_and_finish(
          goal_handle, command_id, !shutting_down_.load(), 0, "");
        return;
      }
      if (wait == runtime_can::CanLink::Wait::kBusOff) {
        diagnostic_state_.record_bus_off();
        publish_diagnostics();
        finish_safe_stop(
          goal_handle, device_bridge::kErrorTransport,
          "CAN controller went bus-off while waiting for completion");
        return;
      }
      if (wait == runtime_can::CanLink::Wait::kTransportError) {
        stop_and_finish(
          goal_handle, command_id, false, device_bridge::kErrorTransport,
          "CAN receive failed while waiting for completion: " + wait_error +
          "; attempting device STOP");
        return;
      }
      if (wait == runtime_can::CanLink::Wait::kShutdown) {
        stop_and_finish(
          goal_handle, command_id, false, device_bridge::kErrorShutdown,
          "bridge shutdown while waiting for completion; attempting device STOP");
        return;
      }
      if (wait == runtime_can::CanLink::Wait::kTimeout) {
        continue;
      }

      switch (decoded.completion) {
        case runtime_can::Completion::kAccepted:
          publish_feedback(
            goal_handle, ExecuteDeviceCommand::Feedback::WAITING_ACK, 0,
            ExecuteDeviceCommand::Feedback::COMPLETION_ACCEPTED);
          break;
        case runtime_can::Completion::kExecuting:
          publish_feedback(
            goal_handle, ExecuteDeviceCommand::Feedback::EXECUTING, 0,
            ExecuteDeviceCommand::Feedback::COMPLETION_EXECUTING);
          break;
        case runtime_can::Completion::kCompleted:
          finish_completed(
            goal_handle, ExecuteDeviceCommand::Feedback::COMPLETION_COMPLETED,
            "device reported command completed");
          return;
        case runtime_can::Completion::kFaulted: {
            const auto error_code = device_bridge::device_error_code(decoded.fault_code);
            diagnostic_state_.record_device_fault(error_code);
            publish_diagnostics();
            finish_aborted(
              goal_handle, ExecuteDeviceCommand::Result::DEVICE_FAULT, error_code,
              "device reported fault code=" + std::to_string(decoded.fault_code));
            return;
          }
      }
    }
    // "Executing" without a bound is not a state machine, so an unfinished
    // command is a reported gap, not a completed one.
    stop_and_finish(
      goal_handle, command_id, false, device_bridge::kErrorCompletionTimeout,
      "device did not report completion within " +
      std::to_string(completion_timeout.count()) + " ms; attempting device STOP");
  }

  void finish_completed(
    const std::shared_ptr<GoalHandle> & goal_handle, const std::uint8_t completion,
    const std::string & message)
  {
    // The completion state rides the last feedback frame, because Result keeps
    // its existing shape for clients that already read it.
    publish_feedback(goal_handle, ExecuteDeviceCommand::Feedback::SENDING, 0, completion);
    auto result = std::make_shared<ExecuteDeviceCommand::Result>();
    result->outcome = ExecuteDeviceCommand::Result::COMPLETED;
    result->error_code = 0;
    result->message = message;
    goal_handle->succeed(result);
  }

  void stop_and_finish(
    const std::shared_ptr<GoalHandle> & goal_handle,
    const std::uint16_t original_command_id,
    const bool cancellation_requested,
    const std::uint16_t safe_stop_error_code,
    const std::string & safe_stop_message)
  {
    using DeviceState = robot_task_interfaces::msg::DeviceState;
    diagnostic_state_.mark_stopping();

    const auto stop_command_id = allocate_stop_command_id();
    const runtime_can::Command stop_command{
      stop_command_id, runtime_can::kStopOpcode,
      static_cast<std::int32_t>(original_command_id)};
    std::string send_error;
    if (!link_->send(runtime_can::encode_command(stop_command), send_error)) {
      diagnostic_state_.record_transport_error(device_bridge::kErrorTransport);
      publish_diagnostics();
      finish_safe_stop(
        goal_handle, device_bridge::kErrorTransport,
        "STOP send failed: " + send_error);
      return;
    }
    publish_feedback(goal_handle, ExecuteDeviceCommand::Feedback::STOPPING, 0);

    RCLCPP_WARN(
      get_logger(), "STOP sent stop_command_id=%u original_command_id=%u",
      stop_command_id, original_command_id);
    const std::string stop_extra =
      "{\"stop_command_id\":" + std::to_string(stop_command_id) +
      ",\"original_command_id\":" + std::to_string(original_command_id) + "}";
    // The two STOP boundaries the cancel path is measured between.
    emit_stage(goal_handle->get_goal(), "can_stop_sent", stop_extra);
    runtime_can::Response stop_response;
    std::string wait_error;
    const auto wait_result = receive_response(
      goal_handle, stop_command_id, stop_timeout_ms_, false, stop_response, wait_error);
    if (wait_result == WaitResult::kResponse && stop_response.result_code == 0 &&
      stop_response.device_mode == DeviceState::STOPPED)
    {
      emit_stage(goal_handle->get_goal(), "can_stop_acked", stop_extra);
      const auto stop_message = "device STOP acknowledged stop_command_id=" +
        std::to_string(stop_command_id);
      if (cancellation_requested) {
        finish_canceled(goal_handle, stop_message);
      } else {
        finish_safe_stop(
          goal_handle, safe_stop_error_code, safe_stop_message + "; " + stop_message);
      }
      return;
    }
    if (wait_result == WaitResult::kTimeout) {
      diagnostic_state_.record_stop_failure(device_bridge::kErrorStopTimeout);
      publish_diagnostics();
      finish_safe_stop(
        goal_handle, device_bridge::kErrorStopTimeout,
        "device STOP ACK timeout stop_command_id=" + std::to_string(stop_command_id));
      return;
    }
    if (wait_result == WaitResult::kTransportError || wait_result == WaitResult::kShutdown) {
      diagnostic_state_.record_transport_error(device_bridge::kErrorTransport);
      publish_diagnostics();
      finish_safe_stop(
        goal_handle, device_bridge::kErrorTransport,
        "device STOP receive failed: " + wait_error);
      return;
    }
    diagnostic_state_.record_stop_failure(device_bridge::kErrorStopRejected);
    publish_diagnostics();
    finish_safe_stop(
      goal_handle, device_bridge::kErrorStopRejected,
      "device STOP response rejected stop_command_id=" + std::to_string(stop_command_id));
  }

  std::uint16_t allocate_stop_command_id()
  {
    const auto command_id = next_stop_command_id_;
    if (next_stop_command_id_ == std::numeric_limits<std::uint16_t>::max()) {
      next_stop_command_id_ = runtime_can::kStopCommandIdMin;
    } else {
      ++next_stop_command_id_;
    }
    return command_id;
  }

  void publish_feedback(
    const std::shared_ptr<GoalHandle> & goal_handle,
    const std::uint8_t state,
    const std::uint8_t retry_count,
    const std::uint8_t completion = ExecuteDeviceCommand::Feedback::COMPLETION_UNKNOWN)
  {
    auto feedback = std::make_shared<ExecuteDeviceCommand::Feedback>();
    feedback->state = state;
    feedback->retry_count = retry_count;
    feedback->completion = completion;
    goal_handle->publish_feedback(feedback);
  }

  void publish_diagnostics()
  {
    try {
      diagnostic_msgs::msg::DiagnosticArray message;
      message.header.stamp = now();
      message.status.push_back(
        diagnostic_state_.snapshot(
          link_->is_open(), link_->unmatched_frames(), link_->overflow_frames(),
          link_->reconnects()));
      diagnostics_publisher_->publish(message);
    } catch (const std::exception & error) {
      RCLCPP_ERROR(get_logger(), "Failed to publish diagnostics: %s", error.what());
    } catch (...) {
      RCLCPP_ERROR(get_logger(), "Failed to publish diagnostics: unknown exception");
    }
  }

  void finish_canceled(const std::shared_ptr<GoalHandle> & goal_handle, const std::string & message)
  {
    diagnostic_state_.record_canceled();
    publish_diagnostics();
    auto result = std::make_shared<ExecuteDeviceCommand::Result>();
    result->outcome = ExecuteDeviceCommand::Result::CANCELED;
    result->error_code = 0;
    result->message = message;
    goal_handle->canceled(result);
  }

  void finish_safe_stop(
    const std::shared_ptr<GoalHandle> & goal_handle,
    const std::uint16_t error_code,
    const std::string & message)
  {
    finish_aborted(
      goal_handle, ExecuteDeviceCommand::Result::SAFE_STOP,
      error_code, message);
  }

  void finish_aborted(
    const std::shared_ptr<GoalHandle> & goal_handle,
    const std::uint8_t outcome,
    const std::uint16_t error_code,
    const std::string & message)
  {
    auto result = std::make_shared<ExecuteDeviceCommand::Result>();
    result->outcome = outcome;
    result->error_code = error_code;
    result->message = message;
    goal_handle->abort(result);
  }

  std::string interface_name_;
  std::int64_t max_ack_timeout_ms_{5000};
  std::int64_t diagnostic_period_ms_{1000};
  std::chrono::milliseconds receive_poll_ms_{25};
  std::chrono::milliseconds stop_timeout_ms_{500};
  std::chrono::milliseconds completion_timeout_{30000};
  bool completion_states_enabled_{false};
  std::uint16_t next_stop_command_id_{runtime_can::kStopCommandIdMin};
  std::atomic_bool running_{true};
  std::atomic_bool shutting_down_{false};
  std::atomic_bool worker_finished_{false};
  std::atomic_bool active_goal_{false};
  std::unique_ptr<runtime_can::CanLink> link_;
  std::unique_ptr<runtime_history::StageEventLog> stage_log_;
  device_bridge::DeviceDiagnosticState diagnostic_state_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diagnostics_publisher_;
  rclcpp::TimerBase::SharedPtr diagnostics_timer_;
  rclcpp_action::Server<ExecuteDeviceCommand>::SharedPtr action_server_;
  std::mutex workers_mutex_;
  std::thread worker_;
};

