cmake_minimum_required(VERSION 3.8)
project(runtime_bringup)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)

install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_pytest REQUIRED)
  find_package(ament_lint_auto REQUIRED)
  # The project has no license headers yet.
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()

  # Checks the launch description, not a running stack: this host has no vcan0,
  # so an end-to-end run has to happen in the container or on the target.
  ament_add_pytest_test(test_runtime_launch test/test_runtime_launch.py TIMEOUT 60)
  set_tests_properties(test_runtime_launch PROPERTIES
    ENVIRONMENT "ROS_DOMAIN_ID=231;ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST")
endif()

ament_package()
