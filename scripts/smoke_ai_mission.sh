#!/usr/bin/env bash

set -Eeuo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_root="$(cd "${script_dir}/.." && pwd)"
workspace_root="${EMBODIED_WS:-${source_root}}"
provider="${AI_MISSION_SMOKE_PROVIDER:-fake}"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((20 + $$ % 180))}"

if [[ ! -f "${workspace_root}/install/setup.bash" && -f "${HOME}/embodied_ws/install/setup.bash" ]]; then
  workspace_root="${HOME}/embodied_ws"
fi

set +u
source /opt/ros/jazzy/setup.bash
source "${workspace_root}/install/setup.bash"
set -u

log_dir="${TMPDIR:-/tmp}/embodied-agent-ai-mission-smoke-$$"
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
    printf 'AI mission smoke failed. Server logs: %s\n' "${log_dir}" >&2
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

printf 'Using mission provider: %s\n' "${provider}"
output="$(timeout 60s ros2 run agent_gateway run_mission \
  --provider "${provider}" --yes '先去充电桩，再去工作台' 2>&1)"
printf '%s\n' "${output}"

printf '%s\n' "${output}" | rg -q 'Mission plan: dock -> workbench'
printf '%s\n' "${output}" | rg -q 'Step result: target=dock state=SUCCEEDED goal_status=SUCCEEDED error_code=0 attempts=1'
printf '%s\n' "${output}" | rg -q 'Step result: target=workbench state=SUCCEEDED goal_status=SUCCEEDED error_code=0 attempts=1'
[[ "$(printf '%s\n' "${output}" | rg -c '^AI decision: continue$')" -eq 1 ]]
printf '%s\n' "${output}" | rg -q '^Mission result: completed$'
printf '%s\n' "${output}" | rg -q '^AI summary: .+'

printf '\nAI mission smoke checks passed.\n'
