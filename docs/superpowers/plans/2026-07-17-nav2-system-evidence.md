# Nav2/TurtleBot3 System Evidence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the repository's `ExecuteTask -> NavigateToPose -> Nav2 -> TurtleBot3 -> Gazebo Sim` path locally and record two successful sequential navigation tasks as reproducible system evidence.

**Architecture:** Keep the existing `runtime_simulation` package and headless smoke as the sole system harness. Install released ROS 2 Jazzy binaries, build in a large-disk workspace, fix only failures demonstrated by the real run, then update claims to match measured results.

**Tech Stack:** Ubuntu 24.04, ROS 2 Jazzy, colcon, Navigation2, TurtleBot3 Burger, Gazebo Sim, Bash, pytest, launch_testing

## Global Constraints

- Work only on local branch `feature/nav2-system-evidence`; do not push or merge.
- Do not modify partitions, old home directories, or `work.old`.
- Keep `~/.cache/vscode-cpptools` cleanup limited to rebuildable IntelliSense data; do not stop VS Code or Codex.
- Install released Jazzy binary packages; do not vendor Nav2 or TurtleBot3 source.
- Keep Fake `NavigateToPose` tests and their deterministic fault-injection role.
- Keep keepout documented as visualization-only with `enforced: false`.
- Make no unrelated refactors; only repair failures reproduced by the live system run.
- Do not claim success until `bt_navigator` is active and both outer `ExecuteTask` goals succeed.

---

### Task 1: Prepare disk space and install the released simulation stack

**Files:**
- Verify only: `/home/sheng/.cache/vscode-cpptools`
- Verify only: `/opt/ros/jazzy`

**Interfaces:**
- Consumes: Ubuntu APT repositories and the existing ROS 2 Jazzy installation.
- Produces: discoverable `nav2_bringup`, `ros_gz_sim`, `rviz2`, `turtlebot3_gazebo`, and `turtlebot3_navigation2` packages.

- [ ] **Step 1: Record the pre-cleanup disk state and active cache users**

Run:

```bash
df -h /
du -sh ~/.cache/vscode-cpptools
fuser -vm ~/.cache/vscode-cpptools || true
```

Expected: root has about 3.4 GiB free, the cache is about 20 GiB, and only `cpptools`/`cpptools-srv` may hold cache files.

- [ ] **Step 2: Stop only the C++ index child processes**

Run:

```bash
pkill -TERM -x cpptools || true
pkill -TERM -x cpptools-srv || true
```

Expected: VS Code and Codex remain running. If either index process restarts and opens the cache again, stop here rather than force deletion.

- [ ] **Step 3: Verify the cache is unused, remove it, and confirm recovered space**

Run:

```bash
test -z "$(find ~/.cache/vscode-cpptools -type f -exec fuser {} + 2>/dev/null)"
rm -rf ~/.cache/vscode-cpptools
df -h /
```

Expected: the test exits 0, the cache directory is absent, and root free space increases by roughly 20 GiB.

- [ ] **Step 4: Install the minimum reviewed Jazzy packages**

Run:

```bash
sudo apt update
sudo apt install --no-install-recommends \
  ros-jazzy-navigation2 \
  ros-jazzy-nav2-bringup \
  ros-jazzy-turtlebot3-gazebo \
  ros-jazzy-turtlebot3-navigation2
sudo apt clean
```

Expected: APT completes without held or broken packages.

- [ ] **Step 5: Verify package discovery and remaining space**

Run:

```bash
set +u
source /opt/ros/jazzy/setup.bash
set -u
bash scripts/check_nav2_sim_environment.sh
df -h /
```

Expected: `Nav2/TurtleBot3 simulation dependencies are available.` and several GiB remain free.

### Task 2: Build and test in the large-disk workspace

**Files:**
- Create: `/mnt/old-linux/current-data/sheng/embodied_ws_nav2/src/embodied-agent-runtime` (symbolic link)
- Generated: `/mnt/old-linux/current-data/sheng/embodied_ws_nav2/build`
- Generated: `/mnt/old-linux/current-data/sheng/embodied_ws_nav2/install`
- Generated: `/mnt/old-linux/current-data/sheng/embodied_ws_nav2/log`

**Interfaces:**
- Consumes: this branch's five ROS packages and Task 1's released dependencies.
- Produces: a sourced workspace containing `runtime_simulation` and all local upstream packages.

- [ ] **Step 1: Create the isolated workspace without copying source**

Run:

```bash
mkdir -p /mnt/old-linux/current-data/sheng/embodied_ws_nav2/src
ln -sfn "$PWD" /mnt/old-linux/current-data/sheng/embodied_ws_nav2/src/embodied-agent-runtime
```

Expected: `readlink -f /mnt/old-linux/current-data/sheng/embodied_ws_nav2/src/embodied-agent-runtime` prints the current repository path.

- [ ] **Step 2: Install declared dependencies**

Run:

```bash
cd /mnt/old-linux/current-data/sheng/embodied_ws_nav2
set +u
source /opt/ros/jazzy/setup.bash
set -u
rosdep install --from-paths src --ignore-src --rosdistro jazzy -r -y
```

Expected: `All required rosdeps installed successfully`.

- [ ] **Step 3: Build through the simulation package with system Python**

Run:

```bash
cd /mnt/old-linux/current-data/sheng/embodied_ws_nav2
env -u CONDA_PREFIX -u PYTHONPATH \
  colcon build --symlink-install --packages-up-to runtime_simulation \
  --cmake-args -DPython3_EXECUTABLE=/usr/bin/python3
```

Expected: all local packages through `runtime_simulation` finish successfully.

- [ ] **Step 4: Run the complete local test set**

Run:

```bash
cd /mnt/old-linux/current-data/sheng/embodied_ws_nav2
set +u
source /opt/ros/jazzy/setup.bash
source install/setup.bash
set -u
colcon test --packages-up-to runtime_simulation --event-handlers console_direct+
colcon test-result --verbose
```

Expected: zero errors and zero failures. Record the exact test total for README evidence.

### Task 3: Run and, only if necessary, repair the real headless system smoke

**Files:**
- Modify if a reproduced failure requires it: `simulation/launch/runtime_nav2_sim.launch.py`
- Modify if a reproduced failure requires it: `simulation/runtime_simulation/initial_pose.py`
- Modify if a reproduced failure requires it: `scripts/smoke_nav2_sim.sh`
- Test alongside the changed unit: `simulation/test/test_initial_pose.py`
- Test alongside the changed smoke behavior: existing smoke command and the smallest relevant package test

**Interfaces:**
- Consumes: `runtime_simulation` launch, `/bt_navigator`, `/navigate_to_pose`, and `/execute_task`.
- Produces: two successful sequential outer Action results: `nav2-smoke-dock` and `nav2-smoke-workbench`.

- [ ] **Step 1: Run the existing headless acceptance test unchanged**

Run:

```bash
cd /mnt/old-linux/current-data/sheng/embodied_ws_nav2
EMBODIED_WS="$PWD" bash src/embodied-agent-runtime/scripts/smoke_nav2_sim.sh
```

Expected: `Real Nav2/TurtleBot3 system smoke checks passed.` after both goals report `final_state: 5` and `SUCCEEDED`.

- [ ] **Step 2: If the smoke fails, preserve the log and identify the first causal error**

Run:

```bash
log="$(find /tmp -maxdepth 2 -path '*/embodied-agent-nav2-smoke-*/runtime_nav2_sim.log' -type f -printf '%T@ %p\n' | sort -nr | head -1 | cut -d' ' -f2-)"
test -n "$log"
rg -n -i 'error|failed|exception|abort|timeout|not found|cannot' "$log" | head -80
```

Expected: the earliest concrete launch, lifecycle, TF, localization, planning, or control failure is visible. Do not increase timeouts before ruling out a causal error.

- [ ] **Step 3: Add one failing check for the reproduced repository bug before changing implementation**

Choose the existing boundary that owns the failure:

```bash
# Python helper failure
cd /mnt/old-linux/current-data/sheng/embodied_ws_nav2/src/embodied-agent-runtime
/usr/bin/python3 -m pytest simulation/test/test_initial_pose.py -q

# Launch/smoke wiring failure
bash -n scripts/smoke_nav2_sim.sh
/usr/bin/python3 -m py_compile simulation/launch/runtime_nav2_sim.launch.py
```

Expected: the new focused assertion or existing syntax check fails for the reproduced cause. If the cause is solely an upstream/system configuration issue, make no repository code change and correct only that configuration.

- [ ] **Step 4: Apply the smallest root-cause fix and rerun its focused check**

Run the exact command selected in Step 3.

Expected: the focused check passes without weakening lifecycle, Action-result, or deadline assertions.

- [ ] **Step 5: Rebuild changed packages and rerun the full headless smoke**

Run:

```bash
cd /mnt/old-linux/current-data/sheng/embodied_ws_nav2
env -u CONDA_PREFIX -u PYTHONPATH \
  colcon build --symlink-install --packages-up-to runtime_simulation \
  --cmake-args -DPython3_EXECUTABLE=/usr/bin/python3
set +u
source /opt/ros/jazzy/setup.bash
source install/setup.bash
set -u
EMBODIED_WS="$PWD" bash src/embodied-agent-runtime/scripts/smoke_nav2_sim.sh
```

Expected: both outer tasks report `final_state: 5`, both ROS Actions report `SUCCEEDED`, and the final success line is printed.

- [ ] **Step 6: Commit only an evidence-driven code fix, if one was needed**

Run:

```bash
git add simulation scripts/smoke_nav2_sim.sh
git diff --cached --check
git commit -m "fix: run Nav2 TurtleBot3 system smoke"
```

Expected: one focused local commit. Skip this commit when no repository fix was required.

### Task 4: Record honest evidence for reviewers and learners

**Files:**
- Modify: `README.md`
- Modify: `docs/project-roadmap.md`
- Modify: `docs/release-checklist.zh-CN.md`
- Modify: `docs/project-talking-points.zh-CN.md`
- Modify: `docs/learning-session-17-nav2-turtlebot3-simulation.zh-CN.md`
- Modify: `simulation/README.md`

**Interfaces:**
- Consumes: exact Task 2 test totals and Task 3 smoke results.
- Produces: consistent public claims, reproduction commands, limitations, and project answers.

- [ ] **Step 1: Replace only pending-system-evidence statements with measured results**

Record these facts in all listed documents:

```text
Verified locally on 2026-07-17 with ROS 2 Jazzy.
bt_navigator reached active.
home -> dock succeeded through ExecuteTask and real Nav2.
dock -> workbench succeeded through ExecuteTask and real Nav2.
Keepout remains visualization-only (enforced: false).
```

Use the exact test total printed by `colcon test-result`; do not copy an older total.

- [ ] **Step 2: Add the reproducible headless command and evidence boundary**

Use this command wherever reproduction is documented:

```bash
cd /mnt/old-linux/current-data/sheng/embodied_ws_nav2
EMBODIED_WS="$PWD" bash src/embodied-agent-runtime/scripts/smoke_nav2_sim.sh
```

Expected: README and the learning lesson distinguish real local evidence from Fake Action tests, OpenAI model evidence, real hardware, and enforced keepout.

- [ ] **Step 3: Check documentation consistency and release wording**

Run:

```bash
rg -n 'pending dependencies|待安装依赖|系统验证待执行|完整系统 smoke 仍待|本机安装完整' \
  README.md simulation/README.md docs
rg -n 'enforced: false|visualization-only|仅.*可视化' \
  README.md simulation/README.md docs/learning-session-17-nav2-turtlebot3-simulation.zh-CN.md
git diff --check
```

Expected: no stale pending Nav2 claim remains in current-status sections, while keepout limitations remain explicit.

- [ ] **Step 4: Run release verification and the real smoke once more**

Run:

```bash
bash scripts/verify_release.sh
cd /mnt/old-linux/current-data/sheng/embodied_ws_nav2
EMBODIED_WS="$PWD" bash src/embodied-agent-runtime/scripts/smoke_nav2_sim.sh
```

Expected: release verification passes and the two real navigation tasks succeed again.

- [ ] **Step 5: Commit documentation evidence locally**

Run:

```bash
git add README.md simulation/README.md \
  docs/project-roadmap.md \
  docs/release-checklist.zh-CN.md \
  docs/project-talking-points.zh-CN.md \
  docs/learning-session-17-nav2-turtlebot3-simulation.zh-CN.md
git diff --cached --check
git commit -m "docs: record live Nav2 system evidence"
```

Expected: a local documentation commit with no push or merge.

### Task 5: Final branch verification

**Files:**
- Verify: all files changed since `feature/nav2-turtlebot3-simulation`

**Interfaces:**
- Consumes: Tasks 1-4.
- Produces: a clean, locally committed feature branch ready for later unified code review.

- [ ] **Step 1: Review the complete branch delta**

Run:

```bash
git diff --check feature/nav2-turtlebot3-simulation...HEAD
git diff --stat feature/nav2-turtlebot3-simulation...HEAD
git status --short --branch
```

Expected: no whitespace errors and no uncommitted files.

- [ ] **Step 2: Record final verification facts**

Run:

```bash
git log --oneline --decorate feature/nav2-turtlebot3-simulation..HEAD
df -h / /mnt/old-linux
```

Expected: design, plan, any evidence-driven fix, and evidence documentation are local commits; no branch was pushed or merged.
