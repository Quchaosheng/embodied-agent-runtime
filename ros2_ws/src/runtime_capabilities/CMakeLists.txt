cmake_minimum_required(VERSION 3.8)
project(runtime_capabilities)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(yaml-cpp REQUIRED)

add_library(runtime_capabilities_manifest src/manifest.cpp)
target_compile_features(runtime_capabilities_manifest PUBLIC cxx_std_17)
target_include_directories(runtime_capabilities_manifest PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(runtime_capabilities_manifest PUBLIC yaml-cpp)

install(TARGETS runtime_capabilities_manifest
  EXPORT export_runtime_capabilities
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config/ DESTINATION share/${PROJECT_NAME}/config)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_lint_auto REQUIRED)
  # The project has no license headers yet.
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()

  ament_add_gtest(test_manifest test/test_manifest.cpp)
  target_link_libraries(test_manifest runtime_capabilities_manifest)
  # The shipped manifest is a test fixture: a node reading it and a test
  # reading it must see the same file, or the tests pass on a different world.
  target_compile_definitions(test_manifest PRIVATE
    CAPABILITIES_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/config/capabilities.yaml")
endif()

ament_export_targets(export_runtime_capabilities HAS_LIBRARY_TARGET)
ament_export_dependencies(yaml-cpp)
ament_package()
