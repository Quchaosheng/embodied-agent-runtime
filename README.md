# Embodied Agent Runtime

A safety-bounded ROS 2 runtime that lets an AI model request approved robot
tasks without giving the model direct control of coordinates, velocity,
trajectories, recovery logic, or Nav2.

> Current status: ROS 2 Jazzy workspace verified. Five packages build
> successfully. Latest local evidence: 123 tests, 0 errors, 0 failures, plus
> repeatable Runtime and AI Gateway smoke tests. A provider-independent ROS
> Action bridge and offline Fake AI now turn Chinese user intent into either one
> guarded task or a bounded 1-3 step mission. Fixed Fake-model evaluations pass
> 20/20 single-task cases and 12/12 mission cases with zero unsafe mission
> acceptances. Official OpenAI and configurable OpenAI-compatible relay profiles,
> plus no-ROS probes, are implemented and protocol-tested offline; no real model
> service is connected yet.
> Runtime launch tests cover outer Action success, feedback,
> Guard rejection, confirmed cancellation, global deadline expiry, and process
> cleanup. Bounded retry, recovery exhaustion, and SAFE_STOP are also verified.
> A reproducible TurtleBot3 Burger + Gazebo Sim + AMCL + Nav2 launch, initial
> localization helper, RViz scene view, and headless system smoke are verified
> locally. On 2026-07-17, `bt_navigator` reached active and the outer Runtime
> completed the AI-planned `dock -> workbench` mission through the Guard,
> ExecuteTask, real Nav2, and Gazebo Sim.

## 中文速览

这是一个“AI 负责理解和任务级决策，确定性 Runtime 掌握运动权”的 ROS 2
项目。模型可以规划 1-3 个 `dock` / `workbench` / `home` 命名目标，
并在步骤间从 Runtime 提供的有限选项中决定继续、回家或终止。坐标、
deadline、取消确认、重试上限、恢复策略和 Nav2 调用仍全部由受测试的
C++ Runtime 控制。

### 可量化工程量

| 维度 | 当前证据 |
| --- | --- |
| ROS 2 包 | 5 个：契约、Guard、执行器、AI Gateway、Nav2 仿真编排 |
| 双层 Action | 外层 `ExecuteTask` + 内层真实 `NavigateToPose` 接口 |
| 安全机制 | 严格 Schema、重复键拒绝、全局 deadline、确认取消、有限恢复、失败关闭 YAML |
| 自动化测试 | 123 tests，覆盖 C++/Python 单测、Action launch、地图/场景配置和 Mission 状态机 |
| AI 评测 | 单任务 20/20；多步 Mission 12/12，`unsafe_acceptances=0` |
| 可重复演示 | Runtime/AI/Mission smoke、无 ROS probe、已实跑的 AI Mission + Nav2 headless smoke |
| 模型接入 | Fake、官方 OpenAI、OpenAI-compatible 中转站三种 profile |
| 工程化 | GitHub Actions、发布自检、贡献规范、安全说明、变更记录 |
| 学习沉淀 | 18 课中文实现笔记与 AI/Nav2 系统集成技术复习问答 |

项目用本机实际 Gazebo/Nav2 运行记录支撑系统仿真结论，但没有把真实模型联网、
真机安全或 keepout 强制说成已完成。README、测试输出和 roadmap 明确区分这些证据边界。

### 一条能讲清的 AI 主线

```text
“先去充电桩，再去工作台”
  -> FakeMissionModel / 可选真实 Provider        AI 参与 1：规划
  -> strict MissionPlan(dock, workbench)
  -> ExecuteTask(dock) -> C++ Guard -> real Nav2 -> success
  -> checkpoint choices: abort / continue / return_home
  -> AI selects continue                                  AI 参与 2：受限决策
  -> ExecuteTask(workbench) -> C++ Guard -> real Nav2 -> success
  -> read-only summary                                    AI 参与 3：解释
```

每次运动都必须重新通过 `ExecuteTask -> task_guard -> Nav2`。AI 不能生成
坐标、速度、轨迹、重试次数、task ID 或 ROS 消息；`return_home` 也只是一个
普通、再次受 Guard 校验的命名任务。

## Why this project exists

An AI model is useful for interpreting user intent, but robot motion is a poor
place for unconstrained generation. This project separates those concerns:

- The model selects a small, named task such as navigate to dock.
- The C++ runtime owns target poses, deadlines, readiness checks, cancellation,
  retry limits, and every ROS navigation call.
- Nav2 owns planning, control, and local obstacle avoidance.

The key rule is simple: an invalid or unsafe request must be rejected before a
Nav2 Goal exists.

## Architecture

    User intent
      -> MissionModel.plan()                      untrusted, closed MissionPlan
      -> MissionRunner                            serial steps + bounded transitions
      -> MissionModel.decide()                    only Runtime-offered choices
      -> ExecuteTask Action Server                outer lifecycle
      -> task_guard                               final task safety authority
      -> NavigateToPose / Nav2                    planning and control
      -> TurtleBot3 Burger / Gazebo Sim           real two-step mission verified
      -> MissionModel.summarize()                 read-only, no Action access

The Guard decision combines three inputs:

    TaskRequest + GuardPolicy + RobotContext -> ValidationResult

- TaskRequest: what the caller wants.
- GuardPolicy: what this deployment permits.
- RobotContext: whether the robot is currently ready and idle.

## Safety contract

The model-facing request is deliberately narrow:

    {
      "contract_version": 1,
      "action": "navigate",
      "target": "dock",
      "deadline_s": 90
    }

The model cannot provide coordinates, velocity commands, trajectories,
behavior-tree XML, retry counts, or recovery policy. Named targets are mapped
to reviewed poses by runtime configuration.

## Implemented today

- ROS Action and message definitions in task_contract.
- JSON Schema with a closed field set, named targets, and a bounded deadline.
- C++ task types, states, and explicit error codes.
- task_guard checks contract version, action, target, deadline, active-task
  state, localization readiness, and navigation readiness.
- Version-controlled YAML GuardPolicy loading with fail-closed validation.
- Strict Gateway JSON parsing, duplicate-field rejection, and Draft 7 Schema validation.
- Provider-independent Gateway Action Client, generated task IDs, stable
  feedback/result objects, offline Fake AI, and a natural-language CLI.
- Configurable OpenAI-compatible provider with one closed-schema task tool,
  environment-only credentials, bounded HTTP response size, and fail-closed
  handling when intent is unsupported, ambiguous, or produces invalid output.
- Separate official OpenAI and relay profiles, configurable model/base URL,
  remote HTTPS enforcement, and a one-request probe that sends no ROS Action.
- Version-controlled AI intent evaluation with 12 accepted commands and 8
  fail-closed cases, including negation, multiple targets, and prompt injection.
- Strict 1-3 step MissionPlan validation with a 180-second total budget and no
  adjacent duplicate target.
- Three bounded mission-model stages: planning, Runtime-constrained checkpoint
  selection, and a read-only summary with deterministic fallback.
- Serial MissionRunner execution through the existing ExecuteTask client;
  invalid or unavailable checkpoint responses abort without a new Goal.
- Twelve fixed mission cases, a no-motion mission probe, an offline process
  smoke, and an opt-in Fake-model mission through real Nav2.
- Outer ExecuteTask Action Server with named-target mapping and bounded inner
  cancellation confirmation.
- Global task deadline based on monotonic time, followed by a fixed 500 ms
  cancellation-confirmation grace period.
- Version-controlled recovery policy with two total navigation attempts.
- Explicit `RECOVERING` feedback and `SAFE_STOP + kRecoveryExhausted` after
  the reviewed attempt budget is exhausted.
- Fail-closed named-target YAML loading with exact contract coverage, map-frame
  enforcement, finite coordinates, normalized yaw, and quaternion conversion.
- Deterministic fake NavigateToPose Action Server using the real `nav2_msgs`
  interface.
- `runtime_simulation` composes TurtleBot3 Burger, Gazebo Sim, the matching
  static map, AMCL, Nav2 lifecycle bringup, the Runtime, and an RViz scene view.
- Bounded AMCL initial-pose publication aligns localization with the Gazebo
  spawn pose; named targets use reviewed free cells on the upstream map.
- An occupancy-grid validator rejects named targets outside the map, on
  occupied/unknown cells, or without 0.32 m Burger clearance.
- A headless system smoke waits for active Nav2 and sends two sequential
  `ExecuteTask` Goals through the real `NavigateToPose` server.
- Unit tests for accepted requests, semantic rejection, invalid policy files, and malformed JSON.
- `launch_testing` coverage for feedback/result mapping, unknown targets,
  cancellation propagation, and clean process shutdown.

## Roadmap

| Milestone | Status | Evidence |
| --- | --- | --- |
| M0 environment and first build | Complete | Both packages build; Guard tests pass |
| M1 contract and semantic Guard | Complete | Guard, YAML policy, and strict JSON adapter verified |
| M2 outer Action and fake navigation | Complete | Success, feedback, rejection, cancel, and timeout tests pass |
| M3 bounded recovery | Complete | Retry success, exhaustion, SAFE_STOP, and shared deadline verified |
| M4 Nav2 and TurtleBot3 | Complete | Nav2 active; `home -> dock -> workbench` succeeded through the outer Runtime on 2026-07-17 |
| M5 bounded AI mission | Complete | 20/20 intent + 12/12 mission evaluation; real Nav2 executes `dock -> workbench` |
| M6 observability and release | In progress | CI/release gate implemented; TaskEvent, rosbag, live model, and hardware remain |

## Build and test

Prerequisites: Ubuntu 24.04, ROS 2 Jazzy, colcon, and rosdep. Deactivate Conda
before building so CMake uses /usr/bin/python3.

    cd ~/embodied_ws
    source /opt/ros/jazzy/setup.bash
    rosdep install --from-paths src --ignore-src --rosdistro jazzy -r -y
    colcon build --symlink-install --packages-up-to runtime_simulation agent_gateway
    source install/setup.bash
    colcon test --packages-select task_guard agent_gateway task_executor runtime_simulation
    colcon test-result --verbose

Run the complete pre-push release gate from the repository root:

    bash scripts/verify_release.sh

This command checks repository metadata and possible credentials, rebuilds all
five packages, runs the complete non-Gazebo test result set, evaluates 20 offline Chinese
intents, and runs both process smoke tests. GitHub Actions reproduces the same
core evidence on Ubuntu 24.04 with ROS 2 Jazzy.

Run the process-level M2 proof after building:

    bash src/embodied-agent-runtime/scripts/smoke_phase_2.sh

Current milestone evidence:

    Summary: 5 packages finished
    Summary: 123 tests, 0 errors, 0 failures, 0 skipped
    Intent evaluation: 20/20 passed (100.0%)
    Mission evaluation: 12/12 passed; unsafe_acceptances=0

Run the offline AI-to-ROS proof:

    bash src/embodied-agent-runtime/scripts/smoke_ai_gateway.sh

The smoke defaults explicitly to Fake Provider. After configuring OpenAI or a
relay, opt into one live model request while still using fake navigation:

    AI_SMOKE_PROVIDER=openai bash src/embodied-agent-runtime/scripts/smoke_ai_gateway.sh
    AI_SMOKE_PROVIDER=openai-compatible bash src/embodied-agent-runtime/scripts/smoke_ai_gateway.sh

Before any ROS process, verify one model request with the no-motion probe:

    ros2 run agent_gateway probe_provider --provider openai "回充电桩"
    ros2 run agent_gateway probe_provider --provider openai-compatible "回充电桩"

Run the fixed 20-case Chinese intent evaluation without network access:

    ros2 run agent_gateway evaluate_intents --provider fake

Run the bounded mission evaluation and a no-motion plan probe:

    ros2 run agent_gateway evaluate_missions --provider fake
    ros2 run agent_gateway probe_mission --provider fake "先去充电桩，再去工作台"

Run the complete offline mission through ExecuteTask and fake navigation:

    bash src/embodied-agent-runtime/scripts/smoke_ai_mission.sh

After configuring a real compatible service, the same command can evaluate it:

    ros2 run agent_gateway evaluate_intents --provider openai-compatible

Real-provider evaluation sends 20 model requests and may incur service cost. A
request that is unsupported, negated, or asks for multiple targets is expected
to produce no tool call and is rejected before ROS.

## Nav2 and TurtleBot3 system simulation

Install the real system dependencies once:

    sudo apt update
    sudo apt install ros-jazzy-navigation2 ros-jazzy-nav2-bringup \
      ros-jazzy-turtlebot3-gazebo ros-jazzy-turtlebot3-navigation2 \
      ros-jazzy-rviz2

Then build and start the graphical system:

    cd ~/embodied_ws
    source /opt/ros/jazzy/setup.bash
    colcon build --symlink-install --packages-up-to runtime_simulation
    source install/setup.bash
    ros2 launch runtime_simulation runtime_nav2_sim.launch.py

For repeatable evidence instead of a manual RViz click-through:

    bash src/embodied-agent-runtime/scripts/smoke_nav2_sim.sh

Opt into the bounded Fake-model mission while keeping the same real Nav2 stack:

    NAV2_SMOKE_MODE=mission AI_MISSION_SMOKE_PROVIDER=fake \
      bash src/embodied-agent-runtime/scripts/smoke_nav2_sim.sh

The smoke launches headless Gazebo, waits for Nav2 lifecycle activation, and
sends `home -> dock -> workbench` as two outer Runtime Goals. It is deliberately
kept separate from the fast release gate because a full physics simulation is
slower and needs additional system packages. The configured restricted-area
polygon is currently an RViz marker with `enforced: false`; Nav2 keepout-filter
enforcement is not claimed.

Local AI mission system evidence recorded on 2026-07-17:

    Mission plan: dock -> workbench
    Step result: target=dock ... SUCCEEDED
    Step result: target=workbench ... SUCCEEDED
    AI decision: continue
    Mission result: completed

The lifecycle check is anchored to the exact `active` state; a regression test
ensures `inactive` cannot produce a false positive.

The AI smoke verifies three feedback messages and a successful terminal result.
The Runtime smoke separately verifies `final_state: 5` for `dock`, then checks
that `laboratory` returns `error_code: 13` with `attempts: 0` before an inner
navigation Goal is sent.

## Repository map

| Directory | Responsibility |
| --- | --- |
| task_contract | ROS interfaces, JSON Schema, task types, and error codes |
| task_guard | C++ validation and static safety policy |
| task_executor | Outer Action Server and inner Nav2 Action Client |
| agent_gateway | Model-output normalization, provider boundary, and outer Action Client |
| simulation | TurtleBot3/Gazebo/Nav2 launch, AMCL initialization, targets, markers, and system smoke support |
| test | Deterministic fault and simulation scenarios |
| docs | Architecture, roadmap, learning, and project material |

## Open-source foundation

This project is built on ROS 2 and focused upstream libraries; it does not copy
their source trees into this repository. Direct dependencies are declared in
the five `package.xml` manifests and installed reproducibly with `rosdep`.

- ROS 2 Jazzy and ament provide nodes, Actions, interfaces, and the build model.
- Navigation2 provides the real `NavigateToPose` Action interface.
- yaml-cpp loads reviewed runtime policy and named targets.
- python-jsonschema validates model-facing requests.
- GoogleTest, pytest, and launch_testing provide verification.

See `THIRD_PARTY_NOTICES.md` for project links, license identifiers, and a
strict separation between current dependencies and planned/reference-only
projects. OpenAI-compatible support is a protocol integration using the Python
standard library; no OpenAI SDK source is bundled.

## GitHub release quality

- `.github/workflows/ros2-ci.yml` rebuilds from a clean ROS 2 Jazzy container.
- `.github/CODEOWNERS` assigns review ownership to `@Quchaosheng`.
- `scripts/verify_release.sh` is the local pre-push gate.
- `CONTRIBUTING.md` records safety-preserving change rules.
- `SECURITY.md` defines credential handling and the non-certified safety scope.
- `CHANGELOG.md` separates delivered evidence from unfinished integration.
- `LICENSE` publishes the repository under Apache-2.0.
- `THIRD_PARTY_NOTICES.md` records direct upstream dependencies and licenses.
- `.gitignore` excludes ROS build trees, Python caches, local environments,
  private keys, and editor state.

The workflow intentionally uses only Fake Provider. GitHub secrets are not
required, and CI cannot spend model credits or send a robot Action to real
hardware.

## Project and design notes

- Chinese project answers: docs/project-talking-points.zh-CN.md
- Latest guided lesson: docs/learning-session-18-bounded-ai-mission-agent.zh-CN.md
- GitHub release lesson: docs/learning-session-15-github-release-engineering.zh-CN.md
- Bounded AI mission lesson: docs/learning-session-18-bounded-ai-mission-agent.zh-CN.md
- Architecture and trust boundary: docs/architecture.md
- Ordered implementation roadmap: docs/project-roadmap.md
- Final demonstration: docs/final-demo-spec.md

The README states current implementation separately from planned work so the
project can be explained accurately in an project.
