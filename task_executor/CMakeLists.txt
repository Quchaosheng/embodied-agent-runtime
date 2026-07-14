cmake_minimum_required(VERSION 3.8)
project(task_executor)

find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(nav2_msgs REQUIRED)
find_package(task_contract REQUIRED)
find_package(task_guard REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(yaml-cpp REQUIRED)

add_library(target_map src/target_map.cpp)
target_compile_features(target_map PUBLIC cxx_std_17)
target_include_directories(target_map
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(target_map task_contract)
target_link_libraries(target_map yaml-cpp::yaml-cpp)

add_executable(fake_navigate_to_pose_server src/fake_navigate_to_pose_server.cpp)
target_compile_features(fake_navigate_to_pose_server PUBLIC cxx_std_17)
ament_target_dependencies(fake_navigate_to_pose_server
  nav2_msgs
  rclcpp
  rclcpp_action
)

add_executable(execute_task_server src/execute_task_server.cpp)
target_compile_features(execute_task_server PUBLIC cxx_std_17)
ament_target_dependencies(execute_task_server
  ament_index_cpp
  nav2_msgs
  rclcpp
  rclcpp_action
  task_contract
  task_guard
)
target_link_libraries(execute_task_server target_map)

install(TARGETS
  fake_navigate_to_pose_server
  execute_task_server
  DESTINATION lib/task_executor
)
install(TARGETS target_map
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)
install(FILES ../simulation/config/targets.yaml DESTINATION share/${PROJECT_NAME}/config)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(launch_testing_ament_cmake REQUIRED)
  ament_add_gtest(test_target_map test/test_target_map.cpp)
  target_link_libraries(test_target_map target_map)
  target_compile_definitions(test_target_map PRIVATE
    TARGET_MAP_TEST_VALID_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../simulation/config/targets.yaml"
    TARGET_MAP_TEST_MISSING_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test/data/missing_target.yaml"
    TARGET_MAP_TEST_UNKNOWN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test/data/unknown_target.yaml"
    TARGET_MAP_TEST_NONFINITE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test/data/nonfinite_target.yaml"
    TARGET_MAP_TEST_EXTRA_FIELD_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test/data/extra_field.yaml"
    TARGET_MAP_TEST_FRAME_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test/data/invalid_frame.yaml"
    TARGET_MAP_TEST_YAW_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test/data/invalid_yaw.yaml"
  )
  add_launch_test(test/test_execute_task_launch.py TIMEOUT 30)
  add_launch_test(test/test_recovery_launch.py TIMEOUT 30)
endif()

ament_package()
