#include "runtime_gateway/gateway_service.hpp"
#include "runtime_gateway/loopback_server.hpp"

#include "runtime_history/defaults.hpp"
#include "runtime_history/store.hpp"

#include <chrono>
#include <exception>
#include <csignal>
#include <memory>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"

namespace
{

volatile std::sig_atomic_t stop_requested{};

void request_stop(int) {stop_requested = 1;}

}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(
    argc, argv, rclcpp::InitOptions{}, rclcpp::SignalHandlerOptions::None);
  std::signal(SIGINT, request_stop);
  std::signal(SIGTERM, request_stop);
  try {
    auto node = std::make_shared<rclcpp::Node>("runtime_gateway");
    const auto port = node->declare_parameter<int>("port", 50051);
    const auto database_path = node->declare_parameter<std::string>(
      "database_path", runtime_history::kDefaultDatabasePath);
    if (port < 1 || port > 65535 || database_path.empty()) {
      throw std::invalid_argument("port and database_path must be valid");
    }

    runtime_history::Store store(database_path);
    runtime_gateway::GatewayService service(*node, store);
    // Retention is periodic so a dispatch never pays for the delete scan. An
    // expired identity stays replayable until this runs, which only makes the
    // ledger refuse at capacity slightly longer, never re-dispatch early.
    const auto prune_period_ms = node->declare_parameter<std::int64_t>(
      "ledger_prune_period_ms", 60000);
    if (prune_period_ms <= 0) {
      throw std::invalid_argument("ledger_prune_period_ms must be positive");
    }
    const auto retention_hours = node->declare_parameter<std::int64_t>(
      "task_run_retention_hours", 168);
    const auto keep_at_least = node->declare_parameter<std::int64_t>(
      "task_run_keep_at_least", 1000);
    if (retention_hours <= 0 || keep_at_least < 0) {
      throw std::invalid_argument(
        "task_run_retention_hours must be positive and task_run_keep_at_least non-negative");
    }
    const auto task_run_retention_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::hours(retention_hours)).count();
    const auto task_run_keep_at_least = static_cast<std::size_t>(keep_at_least);
    auto prune_timer = node->create_wall_timer(
      std::chrono::milliseconds(prune_period_ms),
      [&service, &store, &node, task_run_retention_ns, task_run_keep_at_least,
      logger = node->get_logger()]() {
        try {
          service.prune_durable();
        } catch (const std::exception & error) {
          RCLCPP_WARN(logger, "durable ledger prune failed: %s", error.what());
        }
        // task_runs had no retention at all, so the table and the sorted
        // percentile scan grew without bound. It is pruned on the same timer and
        // honours an explicit keep floor.
        try {
          const auto removed = store.prune_task_runs(
            node->now().nanoseconds() - task_run_retention_ns, task_run_keep_at_least);
          if (removed > 0) {
            RCLCPP_INFO(logger, "pruned %zu task_runs rows", removed);
          }
        } catch (const std::exception & error) {
          RCLCPP_WARN(logger, "task_runs prune failed: %s", error.what());
        }
      });
    // The monitor's readiness is defined against a component list that includes
    // this node, so it has to report itself: a gateway that is up but unable to
    // take requests used to look identical to one that was fine.
    auto diagnostics = node->create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
      "/diagnostics", rclcpp::QoS(10).reliable());
    auto publish_health = [&node, &diagnostics, &service](const std::string & message) {
        diagnostic_msgs::msg::DiagnosticStatus status;
        status.name = "runtime/gateway";
        status.hardware_id = "robot_runtime";
        status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
        status.message = message;
        diagnostic_msgs::msg::KeyValue inflight;
        inflight.key = "in_flight";
        inflight.value = std::to_string(service.active_request_count());
        status.values.push_back(inflight);
        diagnostic_msgs::msg::DiagnosticArray array;
        array.header.stamp = node->now();
        array.status.push_back(std::move(status));
        diagnostics->publish(array);
      };

    runtime_gateway::LoopbackServer server(service, port);
    server.start();
    RCLCPP_INFO(node->get_logger(), "gRPC listening on %s", server.address().c_str());

    const auto health_period_ms = node->declare_parameter<std::int64_t>(
      "diagnostic_period_ms", 1000);
    if (health_period_ms <= 0 || health_period_ms > 60000) {
      throw std::invalid_argument("diagnostic_period_ms must be in [1, 60000]");
    }
    auto health_timer = node->create_wall_timer(
      std::chrono::milliseconds(health_period_ms),
      [&publish_health, &server]() {publish_health("serving on " + server.address());});

    rclcpp::executors::MultiThreadedExecutor executor;
    executor.add_node(node);
    std::thread spin_thread([&executor]() {executor.spin();});
    while (!stop_requested) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    bool shutdown_failed = false;
    try {
      server.shutdown();
    } catch (const std::exception & error) {
      shutdown_failed = true;
      RCLCPP_ERROR(node->get_logger(), "%s", error.what());
    }
    executor.cancel();
    spin_thread.join();
    executor.remove_node(node);
    if (shutdown_failed) {
      rclcpp::shutdown();
      return 1;
    }
  } catch (const std::exception & error) {
    RCLCPP_FATAL(rclcpp::get_logger("runtime_gateway"), "%s", error.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
