# Repository Governance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add accurate security, contribution, dependency, and ownership policies for the current ROS 2 runtime.

**Architecture:** Use GitHub's standard repository-policy filenames so the documents are discoverable without application code. Adapt only the useful structure from historical files; all technical claims come from the current package manifests, scripts, CI workflow, and verified evidence boundary.

**Tech Stack:** Markdown, GitHub CODEOWNERS, Git, PowerShell, ROS 2 CI

---

### Task 1: Add Current Repository Policies

**Files:**
- Create: `SECURITY.md`
- Create: `CONTRIBUTING.md`
- Create: `THIRD_PARTY_NOTICES.md`
- Create: `.github/CODEOWNERS`

- [ ] **Step 1: Write the security policy**

Document private reporting through GitHub Security Advisories, forbidden
public disclosure content, credential rotation, and the distinction between
software workflow validation and certified physical safety.

- [ ] **Step 2: Write the contribution guide**

Document Windows as the editing/Git host, WSL2 Ubuntu 24.04 with ROS 2 Jazzy as
the local build environment, native Linux as an equivalent build environment,
and target-side scripts for generic ARM64 and optional X5 profiles. Include the
exact README build commands and require pull requests to state their evidence
boundary.

- [ ] **Step 3: Write the dependency notice**

List the current direct dependencies: ROS 2 Jazzy, BehaviorTree.CPP, gRPC,
Protocol Buffers, SQLite, OpenCV ArUco, Linux SocketCAN, GoogleTest, pytest,
colcon, rosdep, can-utils, and `ros-tooling/setup-ros`. State that upstream
source is not vendored and binary distributions need a transitive-license
review.

- [ ] **Step 4: Assign repository ownership**

Write exactly this ownership rule:

```text
* @Quchaosheng
```

### Task 2: Verify Public-Repository Hygiene

**Files:**
- Verify: `SECURITY.md`
- Verify: `CONTRIBUTING.md`
- Verify: `THIRD_PARTY_NOTICES.md`
- Verify: `.github/CODEOWNERS`

- [ ] **Step 1: Check required files and whitespace**

Run:

```powershell
git diff --check
@('SECURITY.md','CONTRIBUTING.md','THIRD_PARTY_NOTICES.md','.github/CODEOWNERS') |
  ForEach-Object { if (-not (Test-Path $_) -or (Get-Item $_).Length -eq 0) { throw "missing or empty: $_" } }
```

Expected: exit code 0 with no output.

- [ ] **Step 2: Scan for obsolete claims**

Run:

```powershell
rg -n "Nav2|TurtleBot3|Gazebo|model provider|JSON Schema|yaml-cpp" SECURITY.md CONTRIBUTING.md THIRD_PARTY_NOTICES.md
```

Expected: exit code 1 and no matches.

- [ ] **Step 3: Scan for private paths and credentials**

Run the repository's established private-path and common-credential regular
expression scans over tracked files.

Expected: both scans print `clean` and exit code 0.

- [ ] **Step 4: Review the final diff**

Run:

```powershell
git status --short
git diff --stat
git diff
```

Expected: only the four policy files and this plan are new after the design
commit, with no runtime or workflow modifications.

### Task 3: Publish Through the Existing Gate

**Files:**
- Commit: the four policy files and implementation plan

- [ ] **Step 1: Commit the verified policies**

```powershell
git add SECURITY.md CONTRIBUTING.md THIRD_PARTY_NOTICES.md .github/CODEOWNERS docs/superpowers/plans/2026-07-18-repository-governance.md
git commit -m "docs: add current repository governance"
```

- [ ] **Step 2: Push and open a pull request**

Push `feat/repository-governance`, create a pull request into `main`, and
wait for both push and pull-request ROS 2 CI checks.

- [ ] **Step 3: Merge and verify main**

Merge only after every check passes. Fetch `origin/main`, verify the policy
commit is an ancestor, and wait for the post-merge `main` workflow to pass.
