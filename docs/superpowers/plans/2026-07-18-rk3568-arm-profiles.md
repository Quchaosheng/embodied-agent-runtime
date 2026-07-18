# RK3568 ARM Profiles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the ARM64 scripts selectable for RK3568 and ROS 2 Humble without changing the default Jazzy baseline.

**Architecture:** Pass one validated `ROS_DISTRO` value through the environment-check, build, and smoke scripts. Map each supported distro to its Ubuntu baseline and construct setup/package names from that value.

**Tech Stack:** Bash, ROS 2 Jazzy/Humble, rosdep, colcon, GitHub Actions

---

### Task 1: Add Configuration Test

**Files:**
- Create: `scripts/test_arm64_configuration.sh`

- [ ] **Step 1: Write the failing test**

Run the environment checker with `RUNTIME_PLATFORM_PROFILE=rk3568` and
`ROS_DISTRO=humble`; require the report to record both values and not report an
unsupported profile or distribution. Run it again with an unknown profile and
unknown distro; require both failures.

- [ ] **Step 2: Run the test and verify RED**

Expected: FAIL because the checker currently rejects `rk3568` and has no
`ROS_DISTRO` configuration.

### Task 2: Parameterize ARM Scripts

**Files:**
- Modify: `scripts/check_arm64_environment.sh`
- Modify: `scripts/build_on_arm64.sh`
- Modify: `scripts/run_arm64_smoke.sh`

- [ ] **Step 1: Add profile/distro validation and mapping**

Accept `generic-arm64`, `rk3568`, and `x5`; accept `jazzy` and `humble`; require
Ubuntu 24.04 for Jazzy and Ubuntu 22.04 for Humble.

- [ ] **Step 2: Derive ROS setup and package names**

Use `/opt/ros/$ROS_DISTRO/setup.bash`, `--rosdistro "$ROS_DISTRO"`, and
`ros-$ROS_DISTRO-behaviortree-cpp` throughout the scripts.

- [ ] **Step 3: Run the configuration test and verify GREEN**

Expected: `PASS: ARM64 configuration behavior`; hardware-dependent checks still
fail on this x86_64 host for the expected architecture reason.

### Task 3: Document and Wire CI

**Files:**
- Modify: `README.md`
- Modify: `CONTRIBUTING.md`
- Modify: `.github/workflows/ros2-ci.yml`

- [ ] **Step 1: Document RK3568 commands**

Show the default Jazzy command and the Ubuntu 22.04/Humble command with
`RUNTIME_PLATFORM_PROFILE=rk3568 ROS_DISTRO=humble`.

- [ ] **Step 2: Run the configuration test in Ubuntu CI**

Execute `bash scripts/test_arm64_configuration.sh` after shell syntax checks.

- [ ] **Step 3: Run static and configuration verification**

Run `bash -n` for all scripts, the configuration test, `git diff --check`, and
private-path/credential scans.

- [ ] **Step 4: Commit**

```bash
git add scripts README.md CONTRIBUTING.md .github/workflows/ros2-ci.yml docs/superpowers
git commit -m "feat: add RK3568 ARM64 profile"
```

### Task 4: Publish

- [ ] **Step 1: Push and create a pull request**
- [ ] **Step 2: Wait for Windows and Ubuntu checks on push and pull_request**
- [ ] **Step 3: Merge after all checks pass and verify post-merge main CI**
