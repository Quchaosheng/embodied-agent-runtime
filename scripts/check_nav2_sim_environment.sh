#!/usr/bin/env bash

set -Eeuo pipefail

if [[ ! -f /opt/ros/jazzy/setup.bash ]]; then
  printf 'ROS 2 Jazzy is not installed at /opt/ros/jazzy.\n' >&2
  exit 1
fi

set +u
source /opt/ros/jazzy/setup.bash
set -u

required_packages=(
  nav2_bringup
  ros_gz_sim
  rviz2
  turtlebot3_gazebo
  turtlebot3_navigation2
)
missing_packages=()

for package in "${required_packages[@]}"; do
  if ! ros2 pkg prefix "${package}" >/dev/null 2>&1; then
    missing_packages+=("${package}")
  fi
done

if ((${#missing_packages[@]} > 0)); then
  printf 'Missing ROS packages: %s\n' "${missing_packages[*]}" >&2
  printf 'Install the reviewed Jazzy dependencies with:\n\n' >&2
  printf '  sudo apt update\n' >&2
  printf '  sudo apt install ros-jazzy-navigation2 ros-jazzy-nav2-bringup \\\n' >&2
  printf '    ros-jazzy-turtlebot3-gazebo ros-jazzy-turtlebot3-navigation2\n' >&2
  exit 1
fi

printf 'Nav2/TurtleBot3 simulation dependencies are available.\n'
printf 'For a GUI run, use a graphical session with working OpenGL support.\n'
