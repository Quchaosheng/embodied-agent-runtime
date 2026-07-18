# Robot Runtime

A deterministic ROS 2 task runtime connecting a loopback gRPC workflow boundary to fixed BehaviorTree.CPP orchestration, nested ROS Actions, SocketCAN device control, diagnostics, and SQLite task history.

## Runtime Chain

```text
gRPC -> ExecuteWorkflow/fixed BT -> ExecuteTask -> ExecuteDeviceCommand
-> Device Bridge -> SocketCAN -> device -> TaskEvent -> SQLite
```

The repository contains eleven packages: interfaces, CAN protocol, virtual
device, Device Bridge, Task Executor, Runtime Monitor, History, Orchestrator,
Gateway, an optional rule-based AI task adapter, and an optional ArUco
perception adapter.

## Optional ArUco Workflow Input

`perception_task_adapter` detects `DICT_4X4_50` markers from an image or USB
camera and submits only through the existing `ExecuteWorkflow` boundary:

```text
image/camera -> ArUco detection -> ExecuteWorkflow -> fixed BT runtime
```

The default fixed mappings are:

| Marker ID | Workflow | Target |
| --- | --- | --- |
| `10` | `single_task` | `dock_a` |
| `20` | `ready_then_task` | `home` |

Camera mode requires three consecutive matching frames before submission,
suppresses duplicate submissions, rearms after five empty frames, and rejects
frames containing multiple mapped markers. These defaults are configurable,
but the marker dictionary is deliberately fixed.

## Candidate Baseline

This candidate targets Ubuntu 24.04 with ROS 2 Jazzy on x86_64 Linux and
generic ARM64 Linux. On 2026-07-18, GitHub Actions verified the eleven-package
build, tests, generated ArUco image flow, fake Action server integration, and
software-only `vcan0` workflow on Ubuntu 24.04 with ROS 2 Jazzy.

## Build

### Windows With WSL2

From the repository root in Windows PowerShell, check an existing WSL2
development environment without changing it:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\windows_wsl.ps1 -Mode Check
```

After the check reports `compatible=true`, build and test all eleven packages
inside WSL2:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\windows_wsl.ps1 -Mode BuildTest
```

The default distribution is `Ubuntu-24.04`; override it with
`-Distribution <name>`. The script does not install WSL, Ubuntu, ROS, or system
packages and never invokes `sudo`. Build artifacts stay under
`.colcon/windows-wsl`. Use `-DryRun` to inspect the selected mode without
calling `wsl.exe`.

Windows CI verifies PowerShell syntax and DryRun/error behavior only. A real
WSL2 Jazzy run is evidence only when `Check` or `BuildTest` completes on that
machine.

### Linux

```bash
set +u
source /opt/ros/jazzy/setup.bash
set -u
cd ros2_ws
rosdep install --from-paths src --ignore-src --rosdistro jazzy -y
colcon build --cmake-args -DBUILD_TESTING=ON
colcon test --return-code-on-test-failure
colcon test-result --test-result-base build --verbose
```

On x86_64 Linux, build and test directly with the commands above. On ARM64,
start with `scripts/check_arm64_environment.sh`, then run
`scripts/build_on_arm64.sh` and `scripts/run_arm64_smoke.sh`. The default
platform profile is `generic-arm64`; select the optional X5 profile with
`RUNTIME_PLATFORM_PROFILE=x5`. The X5 profile records target intent only and
does not add vendor dependencies or compatibility claims. Never reuse an
x86_64 build, install, or log tree on ARM64.

32-bit ARM is not supported. BPU/NPU runtimes, cameras, GPIO, and physical CAN
adapters remain board-specific integration work behind their respective
adapter boundaries.

## Evidence Boundary

The accepted evidence is a local software chain using generated images and
SocketCAN `vcan0`. It does not prove a physical USB camera, ARM64 or X5 target
execution, physical CAN hardware, physical stopping, board-specific BPU/NPU
or camera compatibility, GPIO behavior, TLS/authentication, high availability,
or production throughput.
