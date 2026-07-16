cmake_minimum_required(VERSION 3.8)
project(device_bridge)

find_package(ament_cmake REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

add_library(heartbeat_monitor src/heartbeat_monitor.cpp)
target_compile_features(heartbeat_monitor PUBLIC cxx_std_17)
target_include_directories(heartbeat_monitor
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

add_executable(socketcan_heartbeat_node src/socketcan_heartbeat_node.cpp)
target_compile_features(socketcan_heartbeat_node PUBLIC cxx_std_17)
ament_target_dependencies(socketcan_heartbeat_node diagnostic_msgs rclcpp std_msgs)
target_link_libraries(socketcan_heartbeat_node heartbeat_monitor)

install(TARGETS heartbeat_monitor socketcan_heartbeat_node
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY launch/ DESTINATION share/${PROJECT_NAME}/launch)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_heartbeat_monitor test/test_heartbeat_monitor.cpp)
  target_link_libraries(test_heartbeat_monitor heartbeat_monitor)
endif()

ament_package()
