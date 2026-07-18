cmake_minimum_required(VERSION 3.8)
project(perception_task_adapter)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(robot_task_interfaces REQUIRED)
find_package(OpenCV REQUIRED)

add_library(marker_trigger src/marker_trigger.cpp)
target_compile_features(marker_trigger PUBLIC cxx_std_17)
target_include_directories(marker_trigger PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

install(TARGETS marker_trigger
  EXPORT export_perception_task_adapter
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config/ DESTINATION share/${PROJECT_NAME}/config)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_lint_common REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()

  ament_add_gtest(test_marker_trigger test/test_marker_trigger.cpp)
  target_link_libraries(test_marker_trigger marker_trigger)
endif()

ament_export_targets(export_perception_task_adapter HAS_LIBRARY_TARGET)
ament_package()
