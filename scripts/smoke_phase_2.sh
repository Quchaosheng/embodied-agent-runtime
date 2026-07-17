#!/usr/bin/env bash

set -Eeuo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_root="$(cd "${script_dir}/.." && pwd)"
workspace_root="${EMBODIED_WS:-${source_root}}"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((20 + $$ % 180))}"

# The normal build lives in ~/embodied_ws and links this repository from src/.
if [[ ! -f "${workspace_root}/install/setup.bash" && -f "${HOME}/embodied_ws/install/setup.bash" ]]; then
  workspace_root="${HOME}/embodied_ws"
fi

# ROS setup files use optional variables that are not nounset-safe.
set +u
source /opt/ros/jazzy/setup.bash
source "${workspace_root}/install/setup.bash"
set -u

log_dir="${TMPDIR:-/tmp}/embodied-agent-runtime-smoke-$$"
mkdir -p "${log_dir}"
nav_pid=""
executor_pid=""

cleanup() {
  local status=$?
  trap - EXIT INT TERM
  if [[ -n "${executor_pid}" ]] && kill -0 "${executor_pid}" 2>/dev/null; then
    kill "${executor_pid}" 2>/dev/null || true
    wait "${executor_pid}" 2>/dev/null || true
  fi
  if [[ -n "${nav_pid}" ]] && kill -0 "${nav_pid}" 2>/dev/null; then
    kill "${nav_pid}" 2>/dev/null || true
    wait "${nav_pid}" 2>/dev/null || true
  fi
  if [[ "${status}" -ne 0 ]]; then
    printf 'Smoke test failed. Server logs: %s\n' "${log_dir}"
  else
    rm -rf "${log_dir}"
  fi
  exit "${status}"
}
trap cleanup EXIT INT TERM

ros2 run task_executor fake_navigate_to_pose_server \
  >"${log_dir}/fake_navigate_to_pose_server.log" 2>&1 &
nav_pid=$!
ros2 run task_executor execute_task_server --ros-args \
  -p localization_check_enabled:=false \
  >"${log_dir}/execute_task_server.log" 2>&1 &
executor_pid=$!

printf 'Waiting for the two Action servers...\n'
for _ in {1..50}; do
  if ros2 action list 2>/dev/null | rg -q '^/execute_task$' && \
     ros2 action list 2>/dev/null | rg -q '^/navigate_to_pose$'; then
    break
  fi
  sleep 0.1
done

if ! ros2 action list 2>/dev/null | rg -q '^/execute_task$'; then
  printf 'execute_task Action server did not start.\n' >&2
  exit 1
fi
if ! ros2 action list 2>/dev/null | rg -q '^/navigate_to_pose$'; then
  printf 'navigate_to_pose Action server did not start.\n' >&2
  exit 1
fi

printf '\n[1/2] Valid named target: dock\n'
dock_output="$(timeout 15s ros2 action send_goal /execute_task \
  task_contract/action/ExecuteTask \
  '{contract_version: 1, action: 1, task_id: smoke-dock, target: dock, deadline_s: 30}' \
  --feedback 2>&1)"
printf '%s\n' "${dock_output}"
printf '%s\n' "${dock_output}" | rg -q 'final_state: 5'
printf '%s\n' "${dock_output}" | rg -q 'distance_remaining: 3(\.0+)?'
printf '%s\n' "${dock_output}" | rg -q 'distance_remaining: 2(\.0+)?'
printf '%s\n' "${dock_output}" | rg -q 'distance_remaining: 1(\.0+)?'
printf '%s\n' "${dock_output}" | rg -q 'Goal finished with status: SUCCEEDED'

printf '\n[2/2] Unknown target: laboratory\n'
unknown_output="$(timeout 15s ros2 action send_goal /execute_task \
  task_contract/action/ExecuteTask \
  '{contract_version: 1, action: 1, task_id: smoke-unknown, target: laboratory, deadline_s: 30}' \
  2>&1)"
printf '%s\n' "${unknown_output}"
printf '%s\n' "${unknown_output}" | rg -q 'final_state: 9'
printf '%s\n' "${unknown_output}" | rg -q 'error_code: 13'
printf '%s\n' "${unknown_output}" | rg -q 'attempts: 0'

printf '\nM2 smoke checks passed.\n'
