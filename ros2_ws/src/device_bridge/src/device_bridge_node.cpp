#include "device_bridge/device_bridge_node.hpp"

#include "runtime_can/can_link.hpp"
#include "runtime_can/protocol.hpp"
#include "runtime_can/socket_can.hpp"

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    rclcpp::spin(std::make_shared<DeviceBridgeNode>());
  } catch (const std::exception & error) {
    RCLCPP_FATAL(rclcpp::get_logger("device_bridge"), "%s", error.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
