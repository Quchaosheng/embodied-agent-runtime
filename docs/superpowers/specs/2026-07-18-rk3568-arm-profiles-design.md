# RK3568 ARM Profiles Design

## Goal

Prepare the runtime for a low-cost RK3568 board without coupling the code to
Rockchip or X5 vendor SDKs.

## Configuration

The ARM scripts keep `generic-arm64` as the default profile and accept `rk3568`
as a generic CPU-only alias. `x5` remains an intent-only alias. The selected
ROS distribution comes from `ROS_DISTRO`, defaulting to `jazzy`:

| ROS_DISTRO | Required Ubuntu |
| --- | --- |
| `jazzy` | `24.04` |
| `humble` | `22.04` |

Only `jazzy` and `humble` are accepted. The package list, OpenCV check, rosdep
check, build, and smoke scripts derive their ROS package and setup paths from
that value. No vendor runtime, NPU, GPU, camera, or CAN claim is added.

## Verification

Add a shell configuration test that exercises the RK3568/Humble values and
rejects unknown profiles or distributions without requiring ARM hardware. CI
runs this test alongside the existing shell syntax check. Documentation gives
the exact RK3568 command and states that a real board is still required for
native ARM64 evidence.
