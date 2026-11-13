#include "device_bridge/device_bridge_node.hpp"

#include <rclcpp/rclcpp.hpp>

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
