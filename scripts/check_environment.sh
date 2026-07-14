#!/usr/bin/env bash

set -euo pipefail

ROS_DISTRO="${ROS_DISTRO:-jazzy}"
missing=0

if [[ ! -f "/opt/ros/${ROS_DISTRO}/setup.bash" ]]; then
  echo "ROS 2 ${ROS_DISTRO} is not installed at /opt/ros/${ROS_DISTRO}."
  missing=1
else
  # shellcheck disable=SC1090
  set +u
  source "/opt/ros/${ROS_DISTRO}/setup.bash"
  set -u
fi

for tool in git g++ colcon rosdep; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "Missing required tool: ${tool}"
    missing=1
  fi
done

if [[ "${missing}" -ne 0 ]]; then
  echo "Install the missing tools, source ROS 2 ${ROS_DISTRO}, then rerun this check."
  exit 1
fi

echo "Environment check passed for ROS 2 ${ROS_DISTRO}."
