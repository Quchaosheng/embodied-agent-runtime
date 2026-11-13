cmake_minimum_required(VERSION 3.8)
project(robot_task_interfaces)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "action/ExecuteTask.action"
  "action/ExecuteDeviceCommand.action"
  "action/ExecuteWorkflow.action"
  "msg/DeviceState.msg"
  "msg/TaskEvent.msg"
  "srv/ResetFault.srv"
  DEPENDENCIES builtin_interfaces
)

# The error-code table is the one place a code is defined, so it lives with the
# interfaces that carry it rather than being restated in each node.
add_library(robot_task_interfaces_error_code src/error_code.cpp)
target_compile_features(robot_task_interfaces_error_code PUBLIC cxx_std_17)
target_include_directories(robot_task_interfaces_error_code PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
add_library(robot_task_interfaces::robot_task_interfaces_error_code ALIAS
  robot_task_interfaces_error_code)

# ament exports `export_X` as `<project>::X`, so this is what consumers link as
# robot_task_interfaces::error_code.
install(TARGETS robot_task_interfaces_error_code
  EXPORT export_error_code
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

# Lets the shell scripts read a code instead of restating it.
add_executable(print_error_code scripts/print_error_code.cpp)
target_link_libraries(print_error_code robot_task_interfaces_error_code)
install(TARGETS print_error_code RUNTIME DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # The project has no license headers yet.
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_error_code test/test_error_code.cpp)
  target_link_libraries(test_error_code robot_task_interfaces_error_code)
endif()

ament_export_targets(export_error_code HAS_LIBRARY_TARGET)
ament_export_dependencies(rosidl_default_runtime)
ament_package()
