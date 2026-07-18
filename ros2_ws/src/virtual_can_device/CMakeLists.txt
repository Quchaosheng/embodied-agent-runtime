cmake_minimum_required(VERSION 3.8)
project(virtual_can_device)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(robot_task_interfaces REQUIRED)
find_package(runtime_can REQUIRED)

add_executable(virtual_can_device_node src/virtual_can_device_node.cpp)
target_compile_features(virtual_can_device_node PUBLIC cxx_std_17)
target_link_libraries(virtual_can_device_node runtime_can::runtime_can)
ament_target_dependencies(virtual_can_device_node
  rclcpp
  robot_task_interfaces)

install(TARGETS virtual_can_device_node
  DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config/ DESTINATION share/${PROJECT_NAME}/config)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # comment the line when a copyright and license is added to all source files
  set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # comment the line when this package is in a git repo and when
  # a copyright and license is added to all source files
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
