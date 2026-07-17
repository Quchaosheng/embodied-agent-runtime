#!/usr/bin/env bash

set -Eeuo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_dir}/.." && pwd)"
workspace_root="${EMBODIED_WS:-${HOME}/embodied_ws}"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((20 + $$ % 180))}"

set +u
source /opt/ros/jazzy/setup.bash
source "${workspace_root}/install/setup.bash"
set -u

ros2 pkg prefix rosbag2_storage_mcap >/dev/null

log_dir="${TMPDIR:-/tmp}/task-event-bag-$$"
bag_dir="${log_dir}/recording"
mkdir -p "${log_dir}"
pids=()
recorder_pid=""

cleanup() {
  local status=$?
  trap - EXIT INT TERM
  if [[ -n "${recorder_pid}" ]] && kill -0 "${recorder_pid}" 2>/dev/null; then
    kill -TERM "${recorder_pid}" 2>/dev/null || true
    wait "${recorder_pid}" 2>/dev/null || true
  fi
  for pid in "${pids[@]}"; do
    if kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
    fi
  done
  if [[ "${status}" -eq 0 ]]; then
    rm -rf "${log_dir}"
  else
    printf 'TaskEvent bag smoke failed. Evidence retained at %s\n' "${log_dir}" >&2
  fi
  exit "${status}"
}
trap cleanup EXIT INT TERM

ros2 run task_executor fake_navigate_to_pose_server \
  >"${log_dir}/fake_navigation.log" 2>&1 &
pids+=("$!")
ros2 run task_executor execute_task_server \
  >"${log_dir}/task_executor.log" 2>&1 &
pids+=("$!")

for _ in {1..80}; do
  if ros2 action list 2>/dev/null | rg -q '^/execute_task$' && \
     ros2 action list 2>/dev/null | rg -q '^/navigate_to_pose$'; then
    break
  fi
  sleep 0.1
done
ros2 action list | rg -q '^/execute_task$'
ros2 action list | rg -q '^/navigate_to_pose$'

ros2 bag record --storage mcap --output "${bag_dir}" /task_events \
  >"${log_dir}/rosbag.log" 2>&1 &
recorder_pid=$!
for _ in {1..80}; do
  if ros2 topic info /task_events 2>/dev/null | rg -q 'Subscription count: [1-9]'; then
    break
  fi
  sleep 0.1
done
ros2 topic info /task_events | rg -q 'Subscription count: [1-9]'

send_task() {
  local task_id=$1
  local target=$2
  timeout 15s ros2 action send_goal /execute_task task_contract/action/ExecuteTask \
    "{contract_version: 1, action: 1, task_id: ${task_id}, target: ${target}, deadline_s: 5}" 2>&1
}

success_output="$(send_task bag-success dock)"
printf '%s\n' "${success_output}"
printf '%s\n' "${success_output}" | rg -q 'Goal finished with status: SUCCEEDED'

rejected_output="$(send_task bag-rejected laboratory)"
printf '%s\n' "${rejected_output}"
printf '%s\n' "${rejected_output}" | rg -q 'error_code: 13'
printf '%s\n' "${rejected_output}" | rg -q 'attempts: 0'

kill -TERM "${recorder_pid}"
wait "${recorder_pid}"
recorder_pid=""

audit_output="$(python3 "${project_root}/scripts/audit_task_event_bag.py" "${bag_dir}")"
printf '%s\n' "${audit_output}"

timeline() {
  local task_id=$1
  printf '%s\n' "${audit_output}" | awk -v id="task_id=${task_id}" \
    '$1 == id {split($2, value, "="); print value[2]}' | paste -sd, -
}

[[ "$(timeline bag-success)" == "1,2,3,5" ]]
[[ "$(timeline bag-rejected)" == "1,9" ]]
printf '%s\n' "${audit_output}" | \
  rg -q '^task_id=bag-success state=5 error_code=0 attempt=1 '
printf '%s\n' "${audit_output}" | \
  rg -q '^task_id=bag-rejected state=9 error_code=13 attempt=0 '

printf '\nTaskEvent MCAP audit smoke checks passed.\n'
