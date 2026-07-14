cmake_minimum_required(VERSION 3.8)
project(task_contract)

find_package(ament_cmake REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "action/ExecuteTask.action"
  "msg/TaskEvent.msg"
  DEPENDENCIES builtin_interfaces
)

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAME})
install(DIRECTORY schema/ DESTINATION share/${PROJECT_NAME}/schema)

ament_export_dependencies(rosidl_default_runtime)
ament_export_include_directories(include/${PROJECT_NAME})
ament_package()
