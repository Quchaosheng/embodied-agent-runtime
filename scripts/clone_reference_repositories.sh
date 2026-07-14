#!/usr/bin/env bash

set -euo pipefail

reference_root="${1:-${HOME}/embodied_references}"
mkdir -p "${reference_root}"

clone_if_missing() {
  local url="$1"
  local directory="$2"

  if [[ -e "${directory}" ]]; then
    echo "Keeping existing reference: ${directory}"
    return
  fi

  git clone "${url}" "${directory}"
}

clone_if_missing https://github.com/ros-navigation/navigation2.git "${reference_root}/navigation2"
clone_if_missing https://github.com/BehaviorTree/BehaviorTree.CPP.git "${reference_root}/BehaviorTree.CPP"
clone_if_missing https://github.com/ros2/examples.git "${reference_root}/examples"
clone_if_missing https://github.com/QwenLM/Qwen-Agent.git "${reference_root}/Qwen-Agent"
clone_if_missing https://github.com/ROBOTIS-GIT/turtlebot3.git "${reference_root}/turtlebot3"
clone_if_missing https://github.com/ROBOTIS-GIT/turtlebot3_simulations.git "${reference_root}/turtlebot3_simulations"
