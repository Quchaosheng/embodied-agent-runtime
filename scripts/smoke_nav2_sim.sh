#!/usr/bin/env bash

set -Eeuo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_root="$(cd "${script_dir}/.." && pwd)"
workspace_root="${EMBODIED_WS:-${source_root}}"
smoke_mode="${NAV2_SMOKE_MODE:-direct}"
mission_provider="${AI_MISSION_SMOKE_PROVIDER:-fake}"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((20 + $$ % 180))}"

if ! bash "${script_dir}/validate_nav2_smoke_mode.sh" "${smoke_mode}"; then
  printf 'Unsupported NAV2_SMOKE_MODE: %s\n' "${smoke_mode}" >&2
  exit 1
fi

if [[ ! -f "${workspace_root}/install/setup.bash" && -f "${HOME}/embodied_ws/install/setup.bash" ]]; then
  workspace_root="${HOME}/embodied_ws"
fi

bash "${script_dir}/check_nav2_sim_environment.sh"

set +u
source /opt/ros/jazzy/setup.bash
source "${workspace_root}/install/setup.bash"
set -u

if ! ros2 pkg prefix runtime_simulation >/dev/null 2>&1; then
  printf 'runtime_simulation is not built. Build the workspace first.\n' >&2
  exit 1
fi

ros2 run runtime_simulation validate_map_targets

log_dir="${TMPDIR:-/tmp}/embodied-agent-nav2-smoke-$$"
mkdir -p "${log_dir}"
launch_pid=""

cleanup() {
  local status=$?
  trap - EXIT INT TERM
  if [[ -n "${launch_pid}" ]] && kill -0 "${launch_pid}" 2>/dev/null; then
    kill -- "-${launch_pid}" 2>/dev/null || true
    wait "${launch_pid}" 2>/dev/null || true
  fi
  if [[ "${status}" -ne 0 ]]; then
    printf 'Nav2 smoke failed. Launch log: %s/runtime_nav2_sim.log\n' "${log_dir}" >&2
  else
    rm -rf "${log_dir}"
  fi
  exit "${status}"
}
trap cleanup EXIT INT TERM

setsid ros2 launch runtime_simulation runtime_nav2_sim.launch.py \
  headless:=true use_rviz:=false \
  >"${log_dir}/runtime_nav2_sim.log" 2>&1 &
launch_pid=$!

printf 'Waiting for Gazebo, AMCL, Nav2, and the Runtime Action server...\n'
for _ in {1..180}; do
  if ros2 action list 2>/dev/null | rg -q '^/execute_task$' && \
     ros2 action list 2>/dev/null | rg -q '^/navigate_to_pose$' && \
     ros2 lifecycle get /bt_navigator 2>/dev/null | \
       bash "${script_dir}/is_lifecycle_active.sh"; then
    break
  fi
  sleep 1
done

if ! ros2 lifecycle get /bt_navigator 2>/dev/null | \
  bash "${script_dir}/is_lifecycle_active.sh"; then
  printf 'Nav2 bt_navigator did not become active.\n' >&2
  exit 1
fi
if ! ros2 action list 2>/dev/null | rg -q '^/execute_task$'; then
  printf 'execute_task Action server did not start.\n' >&2
  exit 1
fi

if [[ "${smoke_mode}" == "mission" ]]; then
  printf '\nRun a bounded AI mission through the real Nav2 stack\n'
  mission_output="$(timeout 240s ros2 run agent_gateway run_mission \
    --provider "${mission_provider}" --yes \
    '先去充电桩，再去工作台' 2>&1)"
  printf '%s\n' "${mission_output}"
  rg -q 'Mission plan: dock -> workbench' <<<"${mission_output}"
  rg -q 'Step result: target=dock state=SUCCEEDED goal_status=SUCCEEDED error_code=0 attempts=1' <<<"${mission_output}"
  rg -q 'Step result: target=workbench state=SUCCEEDED goal_status=SUCCEEDED error_code=0 attempts=1' <<<"${mission_output}"
  [[ "$(rg -c '^AI decision: continue$' <<<"${mission_output}")" -eq 1 ]]
  rg -q '^Mission result: completed$' <<<"${mission_output}"
  rg -q '^AI summary: .+' <<<"${mission_output}"
  printf '\nAI mission/Nav2 system smoke checks passed.\n'
else
  printf '\n[1/2] Navigate from home to dock through the real Nav2 stack\n'
  dock_output="$(timeout 120s ros2 action send_goal /execute_task \
    task_contract/action/ExecuteTask \
    '{contract_version: 1, action: 1, task_id: nav2-smoke-dock, target: dock, deadline_s: 90}' \
    --feedback 2>&1)"
  printf '%s\n' "${dock_output}"
  printf '%s\n' "${dock_output}" | rg -q 'final_state: 5'
  printf '%s\n' "${dock_output}" | rg -q 'Goal finished with status: SUCCEEDED'

  printf '\n[2/2] Navigate from dock to workbench as a second sequential task\n'
  workbench_output="$(timeout 120s ros2 action send_goal /execute_task \
    task_contract/action/ExecuteTask \
    '{contract_version: 1, action: 1, task_id: nav2-smoke-workbench, target: workbench, deadline_s: 90}' \
    --feedback 2>&1)"
  printf '%s\n' "${workbench_output}"
  printf '%s\n' "${workbench_output}" | rg -q 'final_state: 5'
  printf '%s\n' "${workbench_output}" | rg -q 'Goal finished with status: SUCCEEDED'

  printf '\nReal Nav2/TurtleBot3 system smoke checks passed.\n'
fi
