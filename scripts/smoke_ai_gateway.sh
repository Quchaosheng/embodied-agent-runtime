#!/usr/bin/env bash

set -Eeuo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_root="$(cd "${script_dir}/.." && pwd)"
workspace_root="${EMBODIED_WS:-${source_root}}"
provider="${AI_SMOKE_PROVIDER:-fake}"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((20 + $$ % 180))}"

if [[ ! -f "${workspace_root}/install/setup.bash" && -f "${HOME}/embodied_ws/install/setup.bash" ]]; then
  workspace_root="${HOME}/embodied_ws"
fi

set +u
source /opt/ros/jazzy/setup.bash
source "${workspace_root}/install/setup.bash"
set -u

log_dir="${TMPDIR:-/tmp}/embodied-agent-ai-smoke-$$"
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
    printf 'AI smoke test failed. Server logs: %s\n' "${log_dir}"
  else
    rm -rf "${log_dir}"
  fi
  exit "${status}"
}
trap cleanup EXIT INT TERM

ros2 run task_executor fake_navigate_to_pose_server \
  >"${log_dir}/fake_navigate_to_pose_server.log" 2>&1 &
nav_pid=$!
ros2 run task_executor execute_task_server \
  >"${log_dir}/execute_task_server.log" 2>&1 &
executor_pid=$!

printf 'Waiting for ExecuteTask...\n'
for _ in {1..50}; do
  if ros2 action list 2>/dev/null | rg -q '^/execute_task$'; then
    break
  fi
  sleep 0.1
done

if ! ros2 action list 2>/dev/null | rg -q '^/execute_task$'; then
  printf 'execute_task Action server did not start.\n' >&2
  exit 1
fi

printf 'Using provider: %s\n' "${provider}"
output="$(timeout 60s ros2 run agent_gateway ask \
  --provider "${provider}" '电量低了，回充电桩' 2>&1)"
printf '%s\n' "${output}"

printf '%s\n' "${output}" | rg -q 'AI selected: action=navigate target=dock deadline_s=[1-9][0-9]?'
printf '%s\n' "${output}" | rg -q 'state=RUNNING attempt=1 distance_remaining=3\.00'
printf '%s\n' "${output}" | rg -q 'state=RUNNING attempt=1 distance_remaining=2\.00'
printf '%s\n' "${output}" | rg -q 'state=RUNNING attempt=1 distance_remaining=1\.00'
printf '%s\n' "${output}" | rg -q 'Result: task_id=[^ ]+ state=SUCCEEDED goal_status=SUCCEEDED error_code=0 attempts=1'

printf '\nAI Gateway smoke checks passed.\n'
