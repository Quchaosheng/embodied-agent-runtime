# Repository Governance Design

## Goal

Add the standard public repository policies needed to maintain the current
eleven-package ROS 2 runtime without reviving claims or dependencies from its
earlier development history.

## Scope

Create four files:

- `SECURITY.md` defines private vulnerability reporting, credential handling,
  and the boundary between software validation and physical robot safety.
- `CONTRIBUTING.md` documents the supported Windows development workflow,
  Linux/ROS verification commands, ARM64 target checks, and pull-request
  evidence rules.
- `THIRD_PARTY_NOTICES.md` inventories direct runtime, build, test, and CI
  dependencies used by the current repository.
- `.github/CODEOWNERS` assigns repository-wide ownership to `@Quchaosheng`.

Runtime code, package manifests, CI behavior, and supported hardware claims do
not change.

## Decisions

The old governance files are structural references only. Their Nav2,
TurtleBot3, Gazebo, model-provider, JSON Schema, and YAML claims describe a
different runtime and must not be copied.

Windows is the primary editing and Git host. ROS 2 Jazzy builds run in WSL2
Ubuntu 24.04 or native Linux, while ARM64 smoke checks run on the target. Build,
install, and log trees are never shared across Windows, x86_64 Linux, and ARM64.

The dependency notice lists software actually referenced by source, package
manifests, scripts, or CI. It states that dependencies are installed rather
than vendored, and that release artifacts still require a transitive-license
review.

## Verification

Run `git diff --check`, scan tracked files for private host paths and common
credential formats, confirm every listed policy file is non-empty, and rely on
the existing ROS 2 CI workflow for the unchanged runtime. Do not claim physical
camera, CAN, X5, BPU/NPU, or stopping evidence.
