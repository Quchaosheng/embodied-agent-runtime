# Start Here

This folder is the handoff point for continuing the project on another
computer. It contains the project source, design constraints, a reading map,
and scripts for the first build milestone. It intentionally does not bundle
Nav2, TurtleBot3, BehaviorTree.CPP, or Qwen-Agent source code.

## 1. Prepare the target machine

Use Ubuntu 24.04 in WSL2 or on a native machine. Install ROS 2 Jazzy from the
official Ubuntu deb-package instructions:

<https://docs.ros.org/en/jazzy/Installation/Ubuntu-Install-Debs.html>

Install the ROS development tools required by this repository, then initialize
rosdep. The official installation guide is the source of truth for package
names and setup steps.

## 2. Put the project in a ROS workspace

In Ubuntu, unpack this folder so the source path is:

```text
~/embodied_ws/src/embodied-agent-runtime
```

The workspace layout must be:

```text
~/embodied_ws/
  src/
    embodied-agent-runtime/
```

Run the environment check from the project root:

```bash
bash scripts/check_environment.sh
```

## 3. Build and verify the repository

From the project root, run the complete release gate:

```bash
bash scripts/verify_release.sh
```

It builds all four packages, runs the complete test result set, evaluates the
offline AI corpus, and runs both process smoke tests.

## 4. Initialize the project remote

The handoff ZIP excludes `.git`. After release verification and metadata
replacement:

```bash
git init -b main
git config user.name "<your-github-name>"
git config user.email "<your-public-github-email>"
git add .
git commit -m "feat: add safety-bounded embodied agent runtime"
gh auth login
gh repo create embodied-agent-runtime --public --source=. --remote=origin --push
```

`gh auth login` stores authentication locally. Never paste a GitHub PAT, SSH
private key, OpenAI key, or relay key into the repository or chat. If SSH is
used instead, upload only the `.pub` public key to GitHub.

## 5. Continue in order

Read `docs/learning-guide.md` before each study session. It explains what to
read, what to implement immediately afterward, and what proof to keep. Use
`docs/project-roadmap.md` for the implementation order and acceptance
criteria. `docs/final-demo-spec.md` defines the finished project. The
architecture and upstream boundaries are in `docs/architecture.md` and
`docs/upstream-dependencies.md`.

`docs/study-path.md` provides the ordered curriculum. The Chinese project
outline is in `docs/project-talking-points.zh-CN.md`.
