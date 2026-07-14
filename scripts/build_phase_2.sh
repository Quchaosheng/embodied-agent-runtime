#!/usr/bin/env bash

set -euo pipefail

ROS_DISTRO="${ROS_DISTRO:-jazzy}"
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workspace_root="$(cd "${project_root}/../.." && pwd)"

if [[ ! -d "${workspace_root}/src" && -d "${HOME}/embodied_ws/src" ]]; then
  workspace_root="${HOME}/embodied_ws"
fi

if [[ ! -f "/opt/ros/${ROS_DISTRO}/setup.bash" ]]; then
  echo "ROS 2 ${ROS_DISTRO} is not installed. Run scripts/check_environment.sh first."
  exit 1
fi

if [[ ! -d "${workspace_root}/src" ]]; then
  echo "Expected project path: <workspace>/src/embodied-agent-runtime"
  exit 1
fi

# shellcheck disable=SC1090
set +u
source "/opt/ros/${ROS_DISTRO}/setup.bash"
set -u
cd "${workspace_root}"

rosdep install --from-paths src --ignore-src --rosdistro "${ROS_DISTRO}" -r -y
colcon build --symlink-install --packages-up-to task_executor agent_gateway

# shellcheck disable=SC1091
set +u
source install/setup.bash
set -u
colcon test --packages-select task_guard agent_gateway task_executor
colcon test-result --verbose
