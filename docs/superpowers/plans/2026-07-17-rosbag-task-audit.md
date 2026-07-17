# Rosbag Task Audit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Record deterministic Runtime task events to an MCAP bag and fail the release gate unless persisted success and rejection timelines match the task contract.

**Architecture:** Keep persistence outside the Runtime. The existing executor publishes `/task_events`; a shell smoke records that topic with standard rosbag2/MCAP, and a small Python reader converts persisted messages to stable lines for exact assertions. Generated bags remain temporary and no custom storage layer is added.

**Tech Stack:** ROS 2 Jazzy, rosbag2, rosbag2_py, rosbag2_storage_mcap, rclpy serialization, Python 3 unittest, Bash, GitHub Actions.

## Global Constraints

- Work only on `feature/rosbag-task-audit`, based on `feature/task-event-observability`.
- Record only `/task_events`; `/diagnostics` waits for the final readiness-branch integration.
- Use MCAP through installed rosbag2 APIs; do not add a database or custom bag parser.
- Store generated bags under `${TMPDIR:-/tmp}` and retain them only after failure.
- Build and verify in `/mnt/old-linux/current-data/sheng/embodied_ws_rosbag` so
  stale test results from other feature branches cannot inflate the evidence.
- Do not commit `.mcap` files, bag metadata, credentials, or build artifacts.
- Do not add Foxglove or Nav2 work to this branch.
- Keep all commits local; do not merge or push.

---

### Task 1: Stable MCAP TaskEvent Reader

**Files:**
- Create: `scripts/audit_task_event_bag.py`
- Create: `test/test_audit_task_event_bag.py`

**Interfaces:**
- Consumes: a rosbag2 directory containing `/task_events` with type `task_contract/msg/TaskEvent`.
- Produces: `format_event(event: object) -> str` and `read_task_events(bag_path: pathlib.Path) -> list[object]`.
- CLI: `python3 scripts/audit_task_event_bag.py BAG_DIRECTORY` prints one stable line per persisted event and exits nonzero on invalid input.

- [ ] **Step 1: Write the failing formatting and path-validation tests**

Create `test/test_audit_task_event_bag.py`:

```python
from pathlib import Path
from types import SimpleNamespace
import unittest

from scripts.audit_task_event_bag import format_event
from scripts.audit_task_event_bag import read_task_events


class TaskEventBagAuditTest(unittest.TestCase):
    def test_formats_one_stable_line(self):
        event = SimpleNamespace(
            task_id="bag-success",
            state=5,
            error_code=0,
            attempt=1,
            detail="navigation\n  succeeded",
        )

        self.assertEqual(
            format_event(event),
            "task_id=bag-success state=5 error_code=0 attempt=1 "
            "detail=navigation succeeded",
        )

    def test_rejects_missing_bag_directory(self):
        with self.assertRaisesRegex(RuntimeError, "bag directory does not exist"):
            read_task_events(Path("/definitely/missing/task-event-bag"))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests and verify RED**

Run:

```bash
source /opt/ros/jazzy/setup.bash
PYTHONPATH="$PWD" python3 test/test_audit_task_event_bag.py -v
```

Expected: FAIL with `ModuleNotFoundError: No module named 'scripts.audit_task_event_bag'`.

- [ ] **Step 3: Implement the minimum rosbag2 reader**

Create executable `scripts/audit_task_event_bag.py`:

```python
#!/usr/bin/env python3

import argparse
from pathlib import Path
import sys

TASK_EVENT_TOPIC = "/task_events"


def format_event(event: object) -> str:
    detail = " ".join(event.detail.split())
    return (
        f"task_id={event.task_id} state={event.state} "
        f"error_code={event.error_code} attempt={event.attempt} detail={detail}"
    )


def read_task_events(bag_path: Path) -> list[object]:
    if not bag_path.is_dir():
        raise RuntimeError(f"bag directory does not exist: {bag_path}")

    import rosbag2_py
    from rclpy.serialization import deserialize_message
    from rosidl_runtime_py.utilities import get_message

    reader = rosbag2_py.SequentialReader()
    try:
        reader.open(
            rosbag2_py.StorageOptions(uri=str(bag_path), storage_id="mcap"),
            rosbag2_py.ConverterOptions(
                input_serialization_format="cdr",
                output_serialization_format="cdr",
            ),
        )
    except Exception as error:
        raise RuntimeError(f"cannot open MCAP bag: {error}") from error

    topic_types = {
        topic.name: topic.type for topic in reader.get_all_topics_and_types()
    }
    if TASK_EVENT_TOPIC not in topic_types:
        raise RuntimeError(f"bag does not contain {TASK_EVENT_TOPIC}")

    event_type = get_message(topic_types[TASK_EVENT_TOPIC])
    events = []
    while reader.has_next():
        topic, serialized, _ = reader.read_next()
        if topic == TASK_EVENT_TOPIC:
            events.append(deserialize_message(serialized, event_type))
    if not events:
        raise RuntimeError(f"bag contains no messages on {TASK_EVENT_TOPIC}")
    return events


def main() -> int:
    parser = argparse.ArgumentParser(description="Print TaskEvent messages from an MCAP bag")
    parser.add_argument("bag", type=Path)
    arguments = parser.parse_args()
    try:
        events = read_task_events(arguments.bag)
    except RuntimeError as error:
        print(f"TaskEvent bag audit failed: {error}", file=sys.stderr)
        return 1
    for event in events:
        print(format_event(event))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

Set the executable bit:

```bash
chmod +x scripts/audit_task_event_bag.py
```

- [ ] **Step 4: Run the tests and verify GREEN**

Run:

```bash
source /opt/ros/jazzy/setup.bash
PYTHONPATH="$PWD" python3 test/test_audit_task_event_bag.py -v
python3 -m py_compile scripts/audit_task_event_bag.py
```

Expected: `Ran 2 tests` and `OK`; Python compilation exits 0.

- [ ] **Step 5: Commit the reader**

```bash
git add scripts/audit_task_event_bag.py test/test_audit_task_event_bag.py
git commit -m "feat: inspect persisted task events"
```

---

### Task 2: End-to-End MCAP Recording Smoke

**Files:**
- Create: `scripts/smoke_task_event_bag.sh`

**Interfaces:**
- Consumes: built `task_contract`, `task_guard`, and `task_executor`; `ros2 bag`; MCAP plugin; `scripts/audit_task_event_bag.py`.
- Produces: one temporary MCAP bag and exact assertions for `bag-success` and `bag-rejected`.
- CLI: `EMBODIED_WS=/path/to/ws bash scripts/smoke_task_event_bag.sh`.

- [ ] **Step 1: Create a clean large-disk workspace and build the branch**

```bash
workspace=/mnt/old-linux/current-data/sheng/embodied_ws_rosbag
mkdir -p "${workspace}/src"
ln -s \
  /home/sheng/桌面/xinxiangmu/embodied-agent-runtime-handoff/embodied-agent-runtime \
  "${workspace}/src/embodied-agent-runtime"
source /opt/ros/jazzy/setup.bash
rosdep install --from-paths "${workspace}/src" --ignore-src \
  --rosdistro jazzy -r -y
cd "${workspace}"
colcon build --symlink-install --packages-up-to task_executor agent_gateway
```

If the source symlink already exists and points to the repository, reuse it.
Expected: four packages finish successfully.

- [ ] **Step 2: Create the end-to-end smoke**

Create executable `scripts/smoke_task_event_bag.sh` with these required blocks:

```bash
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
```

Then run:

```bash
chmod +x scripts/smoke_task_event_bag.sh
bash -n scripts/smoke_task_event_bag.sh
```

Expected: shell syntax check exits 0.

- [ ] **Step 3: Run the smoke against the clean large workspace**

```bash
EMBODIED_WS=/mnt/old-linux/current-data/sheng/embodied_ws_rosbag \
  bash scripts/smoke_task_event_bag.sh
```

Expected output includes:

```text
task_id=bag-success state=5 error_code=0 attempt=1
task_id=bag-rejected state=9 error_code=13 attempt=0
TaskEvent MCAP audit smoke checks passed.
```

- [ ] **Step 4: Verify fail-closed input handling**

```bash
source /opt/ros/jazzy/setup.bash
python3 scripts/audit_task_event_bag.py /tmp/not-a-task-event-bag
```

Expected: exit 1 with `bag directory does not exist`.

- [ ] **Step 5: Commit the smoke**

```bash
git add scripts/smoke_task_event_bag.sh
git commit -m "test: persist task events in MCAP"
```

---

### Task 3: Release Gate, CI, and Artifact Hygiene

**Files:**
- Modify: `.gitignore`
- Modify: `task_executor/package.xml:17-23`
- Modify: `scripts/verify_release.sh:18-90`
- Modify: `.github/workflows/ros2-ci.yml:32-90`

**Interfaces:**
- Consumes: Task 1 unit test and Task 2 smoke.
- Produces: rosdep metadata for rosbag2/MCAP and identical local/CI audit commands.

- [ ] **Step 1: Add narrow bag ignore rules**

Append to `.gitignore`:

```gitignore
*.mcap
task-event-bag-*/
```

Do not ignore `metadata.yaml` globally.

- [ ] **Step 2: Declare test-only rosbag dependencies**

Add before the existing `rclpy` test dependency in `task_executor/package.xml`:

```xml
  <test_depend>rosbag2_py</test_depend>
  <test_depend>rosbag2_storage_mcap</test_depend>
  <test_depend>rosidl_runtime_py</test_depend>
```

Run:

```bash
xmllint --noout task_executor/package.xml
rosdep check --from-paths task_executor --ignore-src --rosdistro jazzy
```

Expected: XML validation exits 0 and rosdep reports all dependencies satisfied.

- [ ] **Step 3: Add the reader and smoke to the local release gate**

Add these files to `required_files` in `scripts/verify_release.sh`:

```bash
  scripts/audit_task_event_bag.py
  scripts/smoke_task_event_bag.sh
  test/test_audit_task_event_bag.py
```

After `source install/setup.bash`, add:

```bash
PYTHONPATH="${project_root}" \
  python3 "${project_root}/test/test_audit_task_event_bag.py" -v
```

After the AI smoke, add:

```bash
ROS_DOMAIN_ID=163 EMBODIED_WS="${workspace_root}" \
  bash "${project_root}/scripts/smoke_task_event_bag.sh"
```

- [ ] **Step 4: Run the release gate before changing CI**

```bash
EMBODIED_WS=/mnt/old-linux/current-data/sheng/embodied_ws_rosbag \
  bash scripts/verify_release.sh
```

Expected: unit test, existing tests, intent evaluation, Runtime smoke, AI smoke,
and TaskEvent MCAP smoke all exit 0.

- [ ] **Step 5: Reuse the same smoke in GitHub Actions**

Extend `.github/workflows/ros2-ci.yml` process smoke block:

```yaml
          PYTHONPATH="${GITHUB_WORKSPACE}" \
            python3 test/test_audit_task_event_bag.py -v
          bash scripts/smoke_phase_2.sh
          bash scripts/smoke_ai_gateway.sh
          ROS_DOMAIN_ID=163 bash scripts/smoke_task_event_bag.sh
```

No credential or network-model step is added.

- [ ] **Step 6: Commit release integration**

```bash
git add .gitignore task_executor/package.xml scripts/verify_release.sh \
  .github/workflows/ros2-ci.yml
git commit -m "ci: verify persisted task event evidence"
```

---

### Task 4: README, Learning, and Project Evidence

**Files:**
- Modify: `README.md`
- Modify: `CHANGELOG.md`
- Modify: `THIRD_PARTY_NOTICES.md`
- Modify: `docs/project-talking-points.zh-CN.md`
- Modify: `docs/release-checklist.zh-CN.md`
- Create: `docs/learning-session-20-rosbag-task-audit.zh-CN.md`
- Create: `/home/sheng/桌面/具身智能项目技术复习学习/学习记录/20-rosbag任务审计.md` as a symlink

**Interfaces:**
- Consumes: verified commands and observed output from Tasks 1-3.
- Produces: accurate first-page evidence and one Chinese lesson with project answers.

- [ ] **Step 1: Update README without inflating the colcon test count**

Document these exact facts:

```text
- TaskEvent is persisted with standard rosbag2 MCAP, not a custom database.
- The smoke records one success and one Guard rejection, reads the bag back,
  and asserts exact state order, error code, and attempt count.
- Run: EMBODIED_WS=~/embodied_ws bash scripts/smoke_task_event_bag.sh
- Generated bags are temporary and are not committed.
- The existing colcon test count remains separate from the new unittest and smoke.
```

Update the M5 roadmap row from “rosbag remains” to “TaskEvent MCAP audit
complete; diagnostics/Foxglove integration remains.”

- [ ] **Step 2: Add lesson 20**

Create `docs/learning-session-20-rosbag-task-audit.zh-CN.md` with these sections
and complete answers:

```markdown
# 第二十课：rosbag2 MCAP 任务持久审计

## 为什么 transient-local 不够

transient-local 只在 publisher 存活时保留最近历史；进程重启后历史消失。
rosbag2 把 DDS 消息持久到文件，可以跨进程回放和检查。

## 为什么选 MCAP

MCAP 是 rosbag2 的现成存储插件，支持多 Topic、时间戳和索引。
项目只通过公开 rosbag2 API 读取，不解析 MCAP 二进制格式。

## 持久证据

bag-success: VALIDATING -> DISPATCHING -> RUNNING -> SUCCEEDED
bag-rejected: VALIDATING -> FAILED(error 13, attempt 0)

## 复现命令

EMBODIED_WS=/mnt/old-linux/current-data/sheng/embodied_ws_rosbag \
  bash scripts/smoke_task_event_bag.sh

## 边界

bag 证明 Runtime 发布过什么软件事件，不能单独证明物理机器人已停止。
bag 可能包含地图、位置或用户输入，离开本机前必须进行数据分类和脱敏。

## 技术复习问答

### TaskEvent 和 rosbag2 各解决什么？

TaskEvent 定义任务状态契约；rosbag2 负责持久和回放该契约的消息。

### 为什么不把 bag 提交到 Git？

bag 是生成的二进制运行证据，体积会持续增长，还可能包含环境数据。
仓库只保存生成和验证证据的可重复脚本。
```

- [ ] **Step 3: Update release and dependency records**

Add to `CHANGELOG.md`: TaskEvent MCAP persistence and deterministic audit
smoke. Add rosbag2/MCAP to the implemented test/development dependency table in
`THIRD_PARTY_NOTICES.md`. Add checklist items requiring the bag smoke to pass
and forbidding `.mcap` files in Git status.

Add three project answers:

1. Why transient-local is not durable history.
2. Why the project uses rosbag2 APIs instead of parsing MCAP bytes.
3. What a bag proves and what it cannot prove about physical stop.

- [ ] **Step 4: Create the desktop lesson link**

```bash
ln -s \
  /home/sheng/桌面/xinxiangmu/embodied-agent-runtime-handoff/embodied-agent-runtime/docs/learning-session-20-rosbag-task-audit.zh-CN.md \
  /home/sheng/桌面/具身智能项目技术复习学习/学习记录/20-rosbag任务审计.md
```

If the destination exists, verify it points to the repository lesson rather
than overwriting it.

- [ ] **Step 5: Run complete verification**

```bash
bash -n scripts/smoke_task_event_bag.sh scripts/verify_release.sh
python3 -m py_compile scripts/audit_task_event_bag.py
xmllint --noout task_executor/package.xml
git diff --check
EMBODIED_WS=/mnt/old-linux/current-data/sheng/embodied_ws_rosbag \
  bash scripts/verify_release.sh
git status --short
```

Expected: all commands exit 0; release output includes the TaskEvent MCAP smoke
success line; Git status contains only intentional source and documentation
changes and no `.mcap` file.

- [ ] **Step 6: Commit documentation**

```bash
git add README.md CHANGELOG.md THIRD_PARTY_NOTICES.md \
  docs/project-talking-points.zh-CN.md \
  docs/release-checklist.zh-CN.md \
  docs/learning-session-20-rosbag-task-audit.zh-CN.md
git commit -m "docs: teach persisted task event auditing"
```

---

### Task 5: Final Branch Evidence

**Files:**
- Verify only; no planned source changes.

**Interfaces:**
- Produces: a clean, local feature branch ready for later integration review.

- [ ] **Step 1: Verify branch history and cleanliness**

```bash
git status --short --branch
git log --oneline --decorate -6
git diff feature/task-event-observability..HEAD --check
```

Expected: clean worktree and six local commits after
`feature/task-event-observability`: design, plan, reader, smoke, release
integration, and documentation.

- [ ] **Step 2: Record the handoff facts**

Report:

```text
branch: feature/rosbag-task-audit
push: not performed
merge: not performed
bag artifact: not committed
verification: release gate and MCAP smoke passed
deferred: diagnostics recording and Foxglove layout after final integration
```
