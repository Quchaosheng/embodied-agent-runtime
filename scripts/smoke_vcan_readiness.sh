#!/usr/bin/env bash

set -Eeuo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_root="$(cd "${script_dir}/.." && pwd)"
workspace_root="${EMBODIED_WS:-${source_root}}"
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((20 + $$ % 180))}"

if [[ ! -f "${workspace_root}/install/setup.bash" && \
      -f /mnt/old-linux/current-data/sheng/embodied_ws/install/setup.bash ]]; then
  workspace_root=/mnt/old-linux/current-data/sheng/embodied_ws
fi
if [[ ! -f "${workspace_root}/install/setup.bash" && \
      -f "${HOME}/embodied_ws/install/setup.bash" ]]; then
  workspace_root="${HOME}/embodied_ws"
fi

set +u
source /opt/ros/jazzy/setup.bash
source "${workspace_root}/install/setup.bash"
set -u

command -v cansend >/dev/null
ip link show vcan0 2>/dev/null | rg -q 'UP'

log_dir="${TMPDIR:-/tmp}/embodied-agent-vcan-smoke-$$"
mkdir -p "${log_dir}"
pids=()
heartbeat_pid=""

cleanup() {
  local status=$?
  trap - EXIT INT TERM
  if [[ -n "${heartbeat_pid}" ]] && kill -0 "${heartbeat_pid}" 2>/dev/null; then
    kill "${heartbeat_pid}" 2>/dev/null || true
  fi
  for pid in "${pids[@]}"; do
    if kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
    fi
  done
  if [[ "${status}" -ne 0 ]]; then
    printf 'vcan smoke failed. Logs: %s\n' "${log_dir}" >&2
  else
    rm -rf "${log_dir}"
  fi
  exit "${status}"
}
trap cleanup EXIT INT TERM

ros2 run device_bridge socketcan_heartbeat_node --ros-args \
  -p interface:=vcan0 -p heartbeat_can_id:=1441 -p heartbeat_timeout_ms:=700 \
  >"${log_dir}/device_bridge.log" 2>&1 &
pids+=("$!")
ros2 run task_executor fake_navigate_to_pose_server \
  >"${log_dir}/fake_navigation.log" 2>&1 &
pids+=("$!")
ros2 run tf2_ros static_transform_publisher \
  --frame-id map --child-frame-id base_link \
  >"${log_dir}/static_transform.log" 2>&1 &
pids+=("$!")
ros2 run task_executor execute_task_server --ros-args \
  -p require_device_ready:=true -p device_ready_timeout_ms:=1000 \
  >"${log_dir}/task_executor.log" 2>&1 &
pids+=("$!")

printf 'Waiting for Runtime and fake navigation...\n'
for _ in {1..80}; do
  if ros2 action list 2>/dev/null | rg -q '^/execute_task$' && \
     ros2 action list 2>/dev/null | rg -q '^/navigate_to_pose$'; then
    break
  fi
  sleep 0.1
done

send_task() {
  local task_id=$1
  timeout 15s ros2 action send_goal /execute_task task_contract/action/ExecuteTask \
    "{contract_version: 1, action: 1, task_id: ${task_id}, target: dock, deadline_s: 5}" 2>&1
}

printf '\n[1/3] Missing heartbeat must fail closed\n'
missing_output="$(send_task vcan-missing)"
printf '%s\n' "${missing_output}"
printf '%s\n' "${missing_output}" | rg -q 'error_code: 18'
printf '%s\n' "${missing_output}" | rg -q 'attempts: 0'

printf '\n[2/3] Valid SocketCAN heartbeat permits one task\n'
(
  for _ in {1..50}; do
    cansend vcan0 5A1#0101
    sleep 0.1
  done
) &
heartbeat_pid=$!
for _ in {1..30}; do
  ready_output="$(timeout 2s ros2 topic echo /device_ready std_msgs/msg/Bool --once 2>/dev/null || true)"
  if printf '%s\n' "${ready_output}" | rg -q 'data: true'; then
    break
  fi
done
printf '%s\n' "${ready_output}" | rg -q 'data: true'
ready_task_output="$(send_task vcan-ready)"
printf '%s\n' "${ready_task_output}"
printf '%s\n' "${ready_task_output}" | rg -q 'final_state: 5'
printf '%s\n' "${ready_task_output}" | rg -q 'Goal finished with status: SUCCEEDED'
wait "${heartbeat_pid}"
heartbeat_pid=""

printf '\n[3/3] Stale heartbeat must fail closed again\n'
sleep 1.2
stale_output="$(send_task vcan-stale)"
printf '%s\n' "${stale_output}"
printf '%s\n' "${stale_output}" | rg -q 'error_code: 18'
printf '%s\n' "${stale_output}" | rg -q 'attempts: 0'

printf '\nSocketCAN device-readiness smoke checks passed.\n'
