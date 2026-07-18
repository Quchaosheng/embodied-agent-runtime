# Windows WSL2 Tools Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide a tested, non-destructive PowerShell entry point for checking and running the ROS 2 Jazzy workspace through WSL2 from Windows.

**Architecture:** A single PowerShell script selects Check or BuildTest mode, translates the repository path with `wslpath`, and executes a strict Bash command inside a named distribution. DryRun exposes stable behavior for a Windows runner without requiring nested virtualization.

**Tech Stack:** Windows PowerShell 5.1+, PowerShell 7, WSL2, Bash, ROS 2 Jazzy, colcon, GitHub Actions

---

### Task 1: Define DryRun and Parameter Behavior

**Files:**
- Create: `scripts/test_windows_wsl.ps1`
- Create: `scripts/windows_wsl.ps1`

- [ ] **Step 1: Write the failing PowerShell behavior test**

Create a child-process test that runs `scripts/windows_wsl.ps1` and asserts:

- Check DryRun exits zero and reports `mode=Check`, the distribution, Jazzy,
  and `invoke_wsl=false`.
- BuildTest DryRun exits zero and reports `mode=BuildTest`, isolated `.colcon`
  paths, and `invoke_wsl=false`.
- an unsupported mode exits nonzero.

- [ ] **Step 2: Run the test and verify RED**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test_windows_wsl.ps1
```

Expected: FAIL because `scripts/windows_wsl.ps1` does not exist.

- [ ] **Step 3: Implement the minimum DryRun interface**

Add parameters `Mode`, `Distribution`, and `DryRun`. Validate Mode against
`Check` and `BuildTest`; print stable key-value output and return before any WSL
lookup when DryRun is selected.

- [ ] **Step 4: Run the test and verify GREEN**

Run the command from Step 2.

Expected: `PASS: windows_wsl.ps1 behavior` and exit code 0.

### Task 2: Implement Real WSL Checks and Build/Test

**Files:**
- Modify: `scripts/windows_wsl.ps1`
- Modify: `scripts/test_windows_wsl.ps1`

- [ ] **Step 1: Add a failing test for generated intent**

Require Check DryRun to report `check_environment=true` and BuildTest DryRun to
report `colcon_build_test=true` plus `.colcon/windows-wsl` isolation.

- [ ] **Step 2: Run the test and verify RED**

Expected: FAIL because the new intent keys are absent.

- [ ] **Step 3: Implement environment checking**

Verify `wsl.exe`, the selected installed distribution, repository path
translation, Ubuntu 24.04, supported architecture, ROS 2 Jazzy, required
commands, and the direct Ubuntu/ROS dependency packages. Print specific
installation guidance and return nonzero without modifying the system.

- [ ] **Step 4: Implement isolated build and test**

After the common check, run `rosdep check`, build all eleven packages under
`.colcon/windows-wsl`, source the isolated install tree, run their tests, and
print verbose test results.

- [ ] **Step 5: Run the behavior test and verify GREEN**

Expected: `PASS: windows_wsl.ps1 behavior` and exit code 0.

### Task 3: Document and Continuously Verify the Entry Point

**Files:**
- Modify: `README.md`
- Modify: `CONTRIBUTING.md`
- Modify: `.github/workflows/ros2-ci.yml`

- [ ] **Step 1: Document exact Windows commands**

Add Check and BuildTest examples using Windows PowerShell, explain the default
distribution, state that no installation occurs, and distinguish DryRun/CI
evidence from a real WSL Jazzy execution.

- [ ] **Step 2: Add a Windows CI job**

Use `windows-latest` to parse every tracked `.ps1` file and run
`scripts/test_windows_wsl.ps1`. Do not install or start WSL in CI.

- [ ] **Step 3: Run local static and behavior verification**

Run the PowerShell parser, behavior test, `git diff --check`, private-path scan,
credential scan, and shell syntax checks available from WSL if a distribution
exists.

Expected: all executable checks pass. If no WSL distribution is installed,
record the shell check as unavailable rather than claiming it ran.

- [ ] **Step 4: Commit the implementation**

```powershell
git add scripts/windows_wsl.ps1 scripts/test_windows_wsl.ps1 README.md CONTRIBUTING.md .github/workflows/ros2-ci.yml docs/superpowers/plans/2026-07-18-windows-wsl-tools.md
git commit -m "feat: add Windows WSL2 development entry point"
```

### Task 4: Publish Through CI

- [ ] **Step 1: Push `feat/windows-wsl-tools` and create a pull request**

- [ ] **Step 2: Wait for Ubuntu ROS 2 and Windows tools jobs on both push and pull_request**

- [ ] **Step 3: Merge only after every check passes**

- [ ] **Step 4: Fetch `origin/main` and wait for the post-merge workflow**
