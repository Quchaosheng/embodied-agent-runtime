cmake_minimum_required(VERSION 3.8)
project(task_guard)

find_package(ament_cmake REQUIRED)
find_package(task_contract REQUIRED)
find_package(yaml-cpp REQUIRED)

add_library(${PROJECT_NAME}
  src/task_guard.cpp
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
target_include_directories(${PROJECT_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(${PROJECT_NAME} task_contract)
target_link_libraries(${PROJECT_NAME} yaml-cpp::yaml-cpp)

install(
  TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config/ DESTINATION share/${PROJECT_NAME}/config)

ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(task_contract yaml-cpp)
ament_export_include_directories(include)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_task_guard test/test_task_guard.cpp)
  target_link_libraries(test_task_guard ${PROJECT_NAME})
  ament_target_dependencies(test_task_guard task_contract)
  target_compile_definitions(test_task_guard PRIVATE
    TASK_GUARD_TEST_POLICY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/config/task_policy.yaml"
    TASK_GUARD_TEST_INVALID_POLICY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test/data/invalid_task_policy.yaml"
    TASK_GUARD_TEST_INVALID_TARGET_POLICY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test/data/invalid_target_policy.yaml"
  )
endif()

ament_package()
