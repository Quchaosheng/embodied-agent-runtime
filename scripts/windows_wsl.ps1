[CmdletBinding()]
param(
    [ValidateSet('Check', 'BuildTest')]
    [string]$Mode = 'Check',

    [ValidateNotNullOrEmpty()]
    [string]$Distribution = 'Ubuntu-24.04',

    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repository = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$isolationRoot = '.colcon/windows-wsl'

if ($DryRun) {
    Write-Output "mode=$Mode"
    Write-Output "distribution=$Distribution"
    Write-Output 'required_ros=jazzy'
    Write-Output "repository=$repository"
    Write-Output 'check_environment=true'
    if ($Mode -eq 'BuildTest') {
        Write-Output "isolation_root=$isolationRoot"
        Write-Output 'colcon_build_test=true'
    }
    Write-Output 'invoke_wsl=false'
    exit 0
}

$wsl = Get-Command 'wsl.exe' -ErrorAction SilentlyContinue
if ($null -eq $wsl) {
    [Console]::Error.WriteLine(
        'wsl.exe is unavailable. Enable WSL2, then install Ubuntu-24.04.'
    )
    exit 2
}

$rawDistributions = @(& $wsl.Source --list --quiet 2>$null)
$listExitCode = $LASTEXITCODE
$installedDistributions = @(
    $rawDistributions |
        ForEach-Object { ($_ -replace "`0", '').Trim() } |
        Where-Object { $_ }
)

if ($listExitCode -ne 0 -or
    -not ($installedDistributions | Where-Object { $_ -ieq $Distribution })) {
    [Console]::Error.WriteLine(
        "WSL distribution '$Distribution' is unavailable. " +
        "Install it explicitly with: wsl.exe --install --distribution $Distribution"
    )
    exit 2
}

$translatedPath = @(& $wsl.Source -d $Distribution -- wslpath -a $repository 2>&1)
if ($LASTEXITCODE -ne 0 -or $translatedPath.Count -eq 0) {
    [Console]::Error.WriteLine(
        "Unable to translate the repository path for '$Distribution'."
    )
    exit 2
}
$linuxRepository = ($translatedPath | Select-Object -Last 1).ToString().Trim()

$environmentCheck = @'
set -Eeuo pipefail
repository=$1
issues=()

record() {
  printf '%s\n' "$*"
}

require_command() {
  local command_name=$1
  if command -v "$command_name" >/dev/null 2>&1; then
    record "tool.$command_name=$(command -v "$command_name")"
  else
    record "tool.$command_name=MISSING"
    issues+=("missing command: $command_name")
  fi
}

os_id=unknown
os_version=unknown
if [[ -r /etc/os-release ]]; then
  os_id=$(bash -c '. /etc/os-release; printf "%s" "${ID:-unknown}"')
  os_version=$(bash -c '. /etc/os-release; printf "%s" "${VERSION_ID:-unknown}"')
fi
architecture=$(uname -m)

record "distribution=$WSL_DISTRO_NAME"
record "os=$os_id"
record "os_version=$os_version"
record "architecture=$architecture"
record "repository=$repository"

[[ "$os_id" == ubuntu && "$os_version" == 24.04 ]] ||
  issues+=("WSL baseline requires Ubuntu 24.04")
case "$architecture" in
  x86_64|aarch64|arm64) ;;
  *) issues+=("unsupported architecture: $architecture") ;;
esac
[[ -f /opt/ros/jazzy/setup.bash ]] || issues+=("ROS 2 Jazzy not found")
[[ -d "$repository/ros2_ws/src" ]] || issues+=("repository layout is missing ros2_ws/src")

for command_name in bash cmake c++ colcon dpkg-query python3 rosdep pkg-config; do
  require_command "$command_name"
done

packages=(
  build-essential can-utils cmake libgrpc++-dev libopencv-dev
  libprotobuf-dev libprotoc-dev libsqlite3-dev pkg-config
  protobuf-compiler protobuf-compiler-grpc python3-colcon-common-extensions
  python3-rosdep ros-jazzy-behaviortree-cpp
)
for package in "${packages[@]}"; do
  version=$(dpkg-query -W -f='${Version}' "$package" 2>/dev/null || true)
  if [[ -n "$version" ]]; then
    record "package.$package=$version"
  else
    record "package.$package=MISSING"
    issues+=("missing package: $package")
  fi
done

if ((${#issues[@]})); then
  record 'compatible=false'
  for issue in "${issues[@]}"; do
    record "issue=$issue"
  done
  record 'NEXT: install the reported WSL dependencies, then rerun Check mode'
  exit 2
fi

record 'compatible=true'
'@

$buildAndTest = @'
set +u
source /opt/ros/jazzy/setup.bash
set -u

workspace="$repository/ros2_ws"
isolation="$repository/.colcon/windows-wsl"
build="$isolation/build"
install="$isolation/install"
log="$isolation/log"
packages=(
  robot_task_interfaces runtime_can virtual_can_device device_bridge
  task_executor runtime_monitor runtime_history task_orchestrator
  runtime_gateway ai_task_adapter perception_task_adapter
)

mkdir -p "$build" "$install" "$log"
cd "$workspace"
if ! rosdep check --from-paths src --ignore-src --rosdistro jazzy; then
  printf '%s\n' 'NEXT: install the reported dependencies before rerunning BuildTest mode' >&2
  exit 3
fi

colcon --log-base "$log" build \
  --build-base "$build" \
  --install-base "$install" \
  --packages-select "${packages[@]}" \
  --cmake-args -DBUILD_TESTING=ON

set +u
source "$install/setup.bash"
set -u
colcon --log-base "$log" test \
  --build-base "$build" \
  --install-base "$install" \
  --packages-select "${packages[@]}" \
  --return-code-on-test-failure
colcon test-result --test-result-base "$build" --verbose
printf '%s\n' 'PASS: Windows WSL2 Jazzy build and test completed'
'@

$bashCommand = $environmentCheck
if ($Mode -eq 'BuildTest') {
    $bashCommand += "`n$buildAndTest"
}

Write-Output "mode=$Mode"
Write-Output "distribution=$Distribution"
Write-Output "linux_repository=$linuxRepository"
& $wsl.Source -d $Distribution -- bash -lc $bashCommand -- $linuxRepository
exit $LASTEXITCODE
