# Windows WSL2 Tools Design

## Goal

Give Windows developers one non-destructive PowerShell entry point for checking
and exercising the Linux ROS 2 Jazzy workspace through WSL2.

## Interface

Create `scripts/windows_wsl.ps1` with these parameters:

- `-Mode Check` verifies the selected WSL distribution, Ubuntu 24.04, ROS 2
  Jazzy, required commands and packages, and the repository layout.
- `-Mode BuildTest` performs the same checks, then builds and tests all eleven
  packages.
- `-Distribution Ubuntu-24.04` selects the WSL distribution name.
- `-DryRun` prints the selected distribution, repository path, and Bash command
  without invoking `wsl.exe`.

The script never enables Windows features, installs a distribution, invokes
`sudo`, installs packages, or changes WSL configuration. Failed checks print a
specific next action and return a nonzero exit code.

## Workspace Isolation

WSL builds use `.colcon/windows-wsl/build`, `.colcon/windows-wsl/install`, and
`.colcon/windows-wsl/log` under the repository. These paths are already covered
by the ignored `.colcon/` directory and do not overlap the default workspace or
ARM64 evidence trees.

## Verification

Add a PowerShell test script that launches the tool in both DryRun modes and
checks its stable output, then verifies an unsupported mode is rejected. Add a
small `windows-latest` CI job to parse every PowerShell file and run those
tests. Existing Ubuntu Jazzy CI remains unchanged.

Update `README.md` and `CONTRIBUTING.md` with the exact Windows commands and an
explicit evidence statement: Windows CI proves PowerShell behavior only; it
does not prove a local WSL Jazzy runtime, ARM64/X5, or physical hardware.
