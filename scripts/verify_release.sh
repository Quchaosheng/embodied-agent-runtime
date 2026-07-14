#!/usr/bin/env bash

set -Eeuo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_dir}/.." && pwd)"
workspace_root="${EMBODIED_WS:-}"

if [[ -z "${workspace_root}" ]]; then
  candidate_root="$(cd "${project_root}/../.." && pwd)"
  if [[ -d "${candidate_root}/src" ]]; then
    workspace_root="${candidate_root}"
  else
    workspace_root="${HOME}/embodied_ws"
  fi
fi

required_files=(
  README.md
  LICENSE
  THIRD_PARTY_NOTICES.md
  CHANGELOG.md
  CONTRIBUTING.md
  SECURITY.md
  .github/workflows/ros2-ci.yml
  task_contract/schema/task_request.schema.json
  task_guard/config/task_policy.yaml
  simulation/config/targets.yaml
  agent_gateway/evaluation/intent_cases.json
)

for required_file in "${required_files[@]}"; do
  if [[ ! -f "${project_root}/${required_file}" ]]; then
    printf 'Missing release file: %s\n' "${required_file}" >&2
    exit 1
  fi
done

if rg -n \
  'Project Maintainer|maintainer@example\.com|<license>TODO</license>|license="TODO"' \
  "${project_root}" \
  --glob '!scripts/verify_release.sh' \
  --glob '!docs/learning-session-15-*'; then
  printf 'Replace placeholder maintainer and license metadata before release.\n' >&2
  exit 1
fi

if rg -n \
  '(gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|sk-[A-Za-z0-9_-]{20,})' \
  "${project_root}"; then
  printf 'Possible credential found; review before release.\n' >&2
  exit 1
fi

if find "${project_root}" -type f -size +10M -print -quit | rg -q .; then
  printf 'Repository contains a file larger than 10 MB.\n' >&2
  exit 1
fi

if [[ ! -f /opt/ros/jazzy/setup.bash ]]; then
  printf 'ROS 2 Jazzy is required for the release gate.\n' >&2
  exit 1
fi

if [[ ! -d "${workspace_root}/src" ]]; then
  printf 'Expected a ROS workspace at %s\n' "${workspace_root}" >&2
  exit 1
fi

export PATH="/usr/bin:/bin:${PATH}"
set +u
source /opt/ros/jazzy/setup.bash
set -u

cd "${workspace_root}"
colcon build --symlink-install --packages-up-to task_executor agent_gateway

set +u
source install/setup.bash
set -u
colcon test --packages-select task_contract task_guard task_executor agent_gateway
colcon test-result --verbose

ros2 run agent_gateway evaluate_intents --provider fake
ROS_DOMAIN_ID=161 EMBODIED_WS="${workspace_root}" \
  bash "${project_root}/scripts/smoke_phase_2.sh"
ROS_DOMAIN_ID=162 EMBODIED_WS="${workspace_root}" \
  bash "${project_root}/scripts/smoke_ai_gateway.sh"

printf '\nRelease verification passed. The repository is ready for GitHub review.\n'
