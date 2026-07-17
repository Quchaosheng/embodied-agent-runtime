# AI Mission Planner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded mission-level AI loop that plans up to three named navigation steps, chooses only Runtime-approved checkpoint transitions, and generates a read-only result summary while every motion still passes through the existing Guard and ExecuteTask Action.

**Architecture:** Keep all mission intelligence inside the existing `agent_gateway` package. A strict JSON mission contract feeds a synchronous `MissionRunner`, which serially reuses `ExecuteTaskClient`; Fake and OpenAI-compatible mission models implement planning, bounded transition selection, and summarization without gaining access to ROS coordinates or motion APIs.

**Tech Stack:** Python 3.12, ROS 2 Jazzy, rclpy Actions, Draft 7 JSON Schema, Python standard-library HTTP, pytest, launch_testing, Nav2, TurtleBot3, Gazebo Sim

## Global Constraints

- Work only on local branch `feature/ai-mission-planner`; do not push or merge.
- Keep the existing `ExecuteTask.action`, C++ Task Guard, named targets, deadlines, cancellation, and recovery semantics unchanged.
- The model may select only `navigate` and `dock` / `workbench` / `home`; it may not output coordinates, velocity, trajectories, retries, BehaviorTree XML, task IDs, ROS messages, or tools.
- Accept 1..3 mission steps, each deadline 1..90 seconds, with a summed mission budget no greater than 180 seconds.
- Reject adjacent duplicate targets and every unknown or additional field before creating a ROS node or Goal.
- Allow checkpoint choices only from Runtime-computed `continue`, `return_home`, and `abort`; invalid or unavailable model responses become `abort` with no new Goal.
- `return_home` is one ordinary Guard-checked navigation task using `min(90, floor(remaining_budget))`, then the mission terminates.
- The final successful planned step does not trigger a checkpoint model request.
- Summaries are at most 500 characters, are read-only, and fall back to a deterministic local summary.
- Reuse Chat Completions compatibility and the current HTTPS, timeout, environment-only credential, and 1 MB response limits.
- Do not add OpenAI SDK, LangChain, an Agent framework, a sixth ROS package, voice, vision, SLAM, manipulation, or hardware control.
- CI and default smoke use Fake models and never require a Key, incur model cost, or launch Gazebo.
- Keep keepout visualization-only and do not describe software SAFE_STOP as a hardware emergency stop.

---

### Task 1: Strict Mission Plan Contract

**Files:**
- Create: `agent_gateway/schema/mission_plan.schema.json`
- Create: `agent_gateway/agent_gateway/mission_plan.py`
- Create: `agent_gateway/test/test_mission_plan.py`
- Modify: `agent_gateway/setup.py`

**Interfaces:**
- Produces: `MissionStep(action: str, target: str, deadline_s: int)`, `MissionPlan(contract_version: int, steps: tuple[MissionStep, ...], budget_s: int)`, `MissionPlanError`, `load_mission_plan_schema()`, and `parse_mission_plan(raw_json: str) -> MissionPlan`.
- Consumes: Draft 7 `jsonschema`, `ament_index_python`, and the existing target/deadline values from `task_request.schema.json`.

- [ ] **Step 1: Write failing parser tests**

Create tests covering the public contract:

```python
def test_accepts_two_step_plan_and_computes_budget():
    plan = parse_mission_plan(json.dumps({
        "contract_version": 1,
        "steps": [
            {"action": "navigate", "target": "dock", "deadline_s": 90},
            {"action": "navigate", "target": "workbench", "deadline_s": 60},
        ],
    }))
    assert tuple(step.target for step in plan.steps) == ("dock", "workbench")
    assert plan.budget_s == 150


@pytest.mark.parametrize("payload", [
    {"contract_version": 1, "steps": []},
    {"contract_version": 1, "steps": [
        {"action": "navigate", "target": "dock", "deadline_s": 61},
        {"action": "navigate", "target": "home", "deadline_s": 60},
        {"action": "navigate", "target": "workbench", "deadline_s": 60},
    ]},
    {"contract_version": 1, "steps": [
        {"action": "navigate", "target": "dock", "deadline_s": 30},
        {"action": "navigate", "target": "dock", "deadline_s": 30},
    ]},
])
def test_rejects_invalid_mission_semantics(payload):
    with pytest.raises(MissionPlanError):
        parse_mission_plan(json.dumps(payload))
```

Also assert duplicate JSON keys, four steps, unknown targets, task IDs, and extra root/step fields fail.

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```bash
cd agent_gateway
/usr/bin/python3 -m pytest test/test_mission_plan.py -q
```

Expected: collection fails because `agent_gateway.mission_plan` does not exist.

- [ ] **Step 3: Add the closed mission schema and minimal parser**

The schema must define exact fields and 1..3 steps. The parser must reject duplicate keys before Draft 7 validation, compute `budget_s`, reject a sum above 180, and reject adjacent duplicate targets:

```python
@dataclass(frozen=True)
class MissionStep:
    action: str
    target: str
    deadline_s: int


@dataclass(frozen=True)
class MissionPlan:
    contract_version: int
    steps: tuple[MissionStep, ...]
    budget_s: int


def parse_mission_plan(raw_json: str, schema_path: str | Path | None = None) -> MissionPlan:
    payload = _load_unique_object(raw_json)
    errors = sorted(_validator(schema_path).iter_errors(payload), key=_error_path)
    if errors:
        raise MissionPlanError(_format_error(errors[0]))
    steps = tuple(MissionStep(**item) for item in payload["steps"])
    budget_s = sum(step.deadline_s for step in steps)
    if budget_s > 180:
        raise MissionPlanError("mission deadline budget exceeds 180 seconds")
    if any(left.target == right.target for left, right in zip(steps, steps[1:])):
        raise MissionPlanError("adjacent mission targets must differ")
    return MissionPlan(payload["contract_version"], steps, budget_s)
```

Install the schema through `setup.py` under `share/agent_gateway/schema`.

- [ ] **Step 4: Run focused and existing request tests**

Run:

```bash
cd agent_gateway
/usr/bin/python3 -m pytest test/test_mission_plan.py test/test_task_request.py -q
```

Expected: all mission and existing single-task parser tests pass.

- [ ] **Step 5: Commit the mission contract**

```bash
git add agent_gateway/schema/mission_plan.schema.json \
  agent_gateway/agent_gateway/mission_plan.py \
  agent_gateway/test/test_mission_plan.py agent_gateway/setup.py
git commit -m "feat: validate bounded AI mission plans"
```

### Task 2: Three-Stage Mission Model Boundary

**Files:**
- Create: `agent_gateway/agent_gateway/mission_provider.py`
- Create: `agent_gateway/test/test_mission_provider.py`
- Modify: `agent_gateway/agent_gateway/provider.py`

**Interfaces:**
- Consumes: `MissionPlan`, `load_mission_plan_schema()`, existing `_post_json`, official OpenAI and relay environment variables.
- Produces: `MissionModel` Protocol with `plan(user_text, schema) -> str`, `decide(checkpoint, choices) -> str`, and `summarize(trace) -> str`; `FakeMissionModel`; `OpenAICompatibleMissionModel`; `create_mission_model(provider_name)`.
- `checkpoint` and `trace` are JSON-serializable dictionaries so this task does not depend on runner dataclasses.

- [ ] **Step 1: Write failing model protocol tests**

Tests must prove three distinct AI participation points:

```python
def test_fake_model_plans_ordered_named_targets():
    model = FakeMissionModel()
    raw = model.plan("先去充电桩，再去工作台", load_mission_plan_schema())
    assert tuple(step.target for step in parse_mission_plan(raw).steps) == (
        "dock", "workbench"
    )


def test_fake_model_selects_only_offered_transition():
    model = FakeMissionModel()
    assert model.decide({"last_step_succeeded": True}, ("abort", "continue")) == "continue"
    assert model.decide({"last_step_succeeded": False}, ("abort",)) == "abort"


def test_fake_summary_is_bounded():
    assert len(FakeMissionModel().summarize({"final_reason": "completed"})) <= 500
```

Protocol tests for the compatible model must capture payloads and assert the plan tool is `submit_mission_plan`, the decision tool enum exactly equals the offered choices, malformed/multiple calls raise `ProviderError`, and summary text over 500 characters is rejected.

- [ ] **Step 2: Run focused tests and verify RED**

Run:

```bash
cd agent_gateway
/usr/bin/python3 -m pytest test/test_mission_provider.py -q
```

Expected: collection fails because `agent_gateway.mission_provider` does not exist.

- [ ] **Step 3: Extract one protected Chat Completions helper**

Refactor `OpenAICompatibleProvider.generate_task()` without changing its payload or tests:

```python
def _chat(self, messages, tools=None, tool_choice=None) -> dict[str, Any]:
    payload = {"model": self._model, "messages": messages, "temperature": 0}
    if tools is not None:
        payload["tools"] = tools
    if tool_choice is not None:
        payload["tool_choice"] = tool_choice
    headers = {"Content-Type": "application/json"}
    if self._api_key:
        headers["Authorization"] = f"Bearer {self._api_key}"
    return self._transport(self._endpoint, headers, payload, self._timeout_s)
```

Keep the existing one-tool behavior byte-for-byte equivalent at the protocol level.

- [ ] **Step 4: Implement Fake and compatible mission models**

The compatible model subclasses the existing provider only to reuse URL/auth/transport boundaries. Use one exact tool-call extractor for planning and decisions. The decision schema is generated from `choices`; summaries use message content and reject non-text or more than 500 characters.

The Fake model maps target keywords in text order, rejects negation, unknown requested tasks, adjacent duplicates, and more than three target mentions. It returns deterministic `continue` when offered, otherwise `abort`, and a deterministic Chinese summary.

- [ ] **Step 5: Run mission and existing provider tests**

Run:

```bash
cd agent_gateway
/usr/bin/python3 -m pytest test/test_mission_provider.py test/test_provider.py -q
```

Expected: new three-stage tests pass and all existing single-task provider tests remain green.

- [ ] **Step 6: Commit the mission model boundary**

```bash
git add agent_gateway/agent_gateway/provider.py \
  agent_gateway/agent_gateway/mission_provider.py \
  agent_gateway/test/test_mission_provider.py
git commit -m "feat: add bounded mission model stages"
```

### Task 3: Serial Mission Runner and Transition Guard

**Files:**
- Create: `agent_gateway/agent_gateway/mission_runner.py`
- Create: `agent_gateway/test/test_mission_runner.py`

**Interfaces:**
- Consumes: `MissionPlan`, `MissionStep`, `MissionModel`, `NormalizedTaskRequest`, `TaskOutcome`, and a callable `step_executor(request, feedback_callback) -> TaskOutcome`.
- Produces: `MissionCheckpoint`, `MissionStepRecord`, `MissionTrace`, `MissionResult`, and `MissionRunner.run(plan, feedback_callback=None) -> MissionResult`.

- [ ] **Step 1: Write failing state-machine tests**

Use real `TaskOutcome` values and a recording callable, not ROS mocks:

```python
def test_two_successful_steps_run_in_order_and_skip_final_checkpoint():
    model = RecordingMissionModel(decisions=["continue"])
    executor = RecordingExecutor([success("dock"), success("workbench")])
    result = MissionRunner(model, executor, clock=FakeClock()).run(two_step_plan())
    assert [request.target for request in executor.requests] == ["dock", "workbench"]
    assert len({request.task_id for request in executor.requests}) == 2
    assert model.decision_calls == 1
    assert result.trace.final_reason == "completed"


def test_provider_failure_at_checkpoint_aborts_without_second_goal():
    model = RecordingMissionModel(decision_error=ProviderError("offline"))
    executor = RecordingExecutor([success("dock")])
    result = MissionRunner(model, executor, clock=FakeClock()).run(two_step_plan())
    assert [request.target for request in executor.requests] == ["dock"]
    assert result.trace.final_reason == "checkpoint_provider_failed"
```

Also cover model abort, failed step plus return_home, disallowed transition, insufficient remaining budget, ActionBridgeError, summary fallback, and deadline expiry before the next Goal.

- [ ] **Step 2: Run focused tests and verify RED**

Run:

```bash
cd agent_gateway
/usr/bin/python3 -m pytest test/test_mission_runner.py -q
```

Expected: collection fails because `agent_gateway.mission_runner` does not exist.

- [ ] **Step 3: Implement immutable trace types and allowed transitions**

Use exact fields:

```python
@dataclass(frozen=True)
class MissionStepRecord:
    request: NormalizedTaskRequest
    outcome: TaskOutcome | None
    error: str


@dataclass(frozen=True)
class MissionTrace:
    mission_id: str
    records: tuple[MissionStepRecord, ...]
    decisions: tuple[str, ...]
    final_reason: str
    elapsed_s: float


@dataclass(frozen=True)
class MissionResult:
    trace: MissionTrace
    summary: str
```

`_allowed_transitions()` always includes `abort`, adds `continue` only after success with pending steps, and adds `return_home` only when current target is not home and at least one whole second remains.

- [ ] **Step 4: Implement the minimal synchronous runner**

Generate mission IDs with `uuid4().hex`; step IDs are `<mission-id>-<1-based-index>`. Before each Goal, use `min(step.deadline_s, floor(remaining_budget))`; never dispatch when it is below one. Catch `ProviderError` around decisions and summaries, catch `ActionBridgeError` around step execution, and call the summary exactly once after terminal trace construction.

A step succeeds only when `error_code == 0`, `goal_status_name == "SUCCEEDED"`, and `final_state_name == "SUCCEEDED"`. An `ActionBridgeError` permits only abort and does not call the checkpoint model.

When `return_home` is selected, send exactly one ordinary `NormalizedTaskRequest(1, "navigate", id, "home", bounded_deadline)` and terminate regardless of that Goal's result.

- [ ] **Step 5: Run runner and dependent unit tests**

Run:

```bash
cd agent_gateway
/usr/bin/python3 -m pytest \
  test/test_mission_runner.py test/test_action_bridge.py test/test_mission_plan.py -q
```

Expected: all tests pass with no ROS daemon or network.

- [ ] **Step 6: Commit the runner**

```bash
git add agent_gateway/agent_gateway/mission_runner.py \
  agent_gateway/test/test_mission_runner.py
git commit -m "feat: execute guarded AI missions serially"
```

### Task 4: Mission CLI and Offline Process Smoke

**Files:**
- Create: `agent_gateway/agent_gateway/mission_cli.py`
- Create: `agent_gateway/test/test_mission_cli.py`
- Create: `scripts/smoke_ai_mission.sh`
- Modify: `agent_gateway/setup.py`
- Modify: `scripts/verify_release.sh`

**Interfaces:**
- Consumes: `create_mission_model()`, `parse_mission_plan()`, `MissionRunner`, `ExecuteTaskClient`, and existing Fake NavigateToPose/ExecuteTask processes.
- Produces: `ros2 run agent_gateway run_mission`, confirmation gate, stable step/checkpoint/final output, and a no-network process smoke.

- [ ] **Step 1: Write failing CLI tests**

Patch model/client factories at module boundaries and assert planning happens before `rclpy.init`, rejection creates no client, confirmation `n` creates no Goal, `--yes` runs two steps, and only `completed` returns exit code 0.

```python
def test_declined_plan_never_initializes_ros(monkeypatch):
    monkeypatch.setattr("builtins.input", lambda _: "n")
    monkeypatch.setattr(
        "agent_gateway.mission_cli.rclpy.init",
        lambda **_: pytest.fail("ROS must not initialize before confirmation"),
    )
    assert main(["先去充电桩，再去工作台"]) == 1
```

- [ ] **Step 2: Run focused tests and verify RED**

Run:

```bash
cd agent_gateway
/usr/bin/python3 -m pytest test/test_mission_cli.py -q
```

Expected: collection fails because `agent_gateway.mission_cli` does not exist.

- [ ] **Step 3: Implement CLI and install its entry point**

The CLI flow is strictly plan -> parse -> print -> confirm -> initialize ROS -> run -> destroy/shutdown. Add:

```python
"run_mission = agent_gateway.mission_cli:main"
```

Print stable lines containing `Mission plan`, `Step result`, `AI decision`, `Mission result`, and `AI summary` for smoke assertions.

- [ ] **Step 4: Add the offline mission smoke**

Follow the existing process cleanup pattern in `smoke_ai_gateway.sh`. Launch the deterministic fake navigation and `execute_task_server`, run:

```bash
ros2 run agent_gateway run_mission --provider fake --yes \
  "先去充电桩，再去工作台"
```

Assert ordered `dock` and `workbench` step results, exactly one `continue`, terminal `completed`, and a summary. Add this smoke to `verify_release.sh` with a unique ROS domain ID.

- [ ] **Step 5: Build, test, and run the offline smoke**

Run:

```bash
cd /mnt/old-linux/current-data/sheng/embodied_ws_nav2
source /opt/ros/jazzy/setup.bash
PATH="/usr/bin:/bin:${PATH}" colcon build --symlink-install \
  --packages-up-to agent_gateway --cmake-args -DPython3_EXECUTABLE=/usr/bin/python3
source install/setup.bash
colcon test --packages-select agent_gateway
colcon test-result --test-result-base build/agent_gateway --verbose
bash src/embodied-agent-runtime/scripts/smoke_ai_mission.sh
```

Expected: agent_gateway tests have zero failures and the smoke prints `AI mission smoke checks passed.`

- [ ] **Step 6: Commit CLI and process proof**

```bash
git add agent_gateway/agent_gateway/mission_cli.py \
  agent_gateway/test/test_mission_cli.py agent_gateway/setup.py \
  scripts/smoke_ai_mission.sh scripts/verify_release.sh
git commit -m "feat: run AI missions through ExecuteTask"
```

### Task 5: Fixed Mission Evaluation and No-Motion Probe

**Files:**
- Create: `agent_gateway/evaluation/mission_cases.json`
- Create: `agent_gateway/agent_gateway/mission_evaluation.py`
- Create: `agent_gateway/agent_gateway/mission_evaluation_cli.py`
- Create: `agent_gateway/agent_gateway/mission_probe.py`
- Create: `agent_gateway/test/test_mission_evaluation.py`
- Create: `agent_gateway/test/test_mission_probe.py`
- Modify: `agent_gateway/setup.py`
- Modify: `scripts/verify_release.sh`

**Interfaces:**
- Consumes: `MissionModel.plan()`, `parse_mission_plan()`, and mission schema.
- Produces: 12 version-controlled cases, `evaluate_missions`, `evaluate_mission` and `probe_mission` entry points; neither initializes ROS nor sends an Action.

- [ ] **Step 1: Write failing evaluation tests and the 12-case fixture**

Use six accepted cases with exact ordered target lists and six rejected cases covering negation, unsupported tasks, four targets, adjacent duplicates, ambiguous instructions, and prompt injection.

Tests must reject duplicate case IDs, unexpected fixture fields, and targets outside the mission schema. The Fake model must score 12/12.

- [ ] **Step 2: Run focused tests and verify RED**

Run:

```bash
cd agent_gateway
/usr/bin/python3 -m pytest \
  test/test_mission_evaluation.py test/test_mission_probe.py -q
```

Expected: collection fails because the evaluation/probe modules do not exist.

- [ ] **Step 3: Implement evaluation and probe without ROS imports**

Mirror the strict shape validation of `intent_evaluation.py`, but compare ordered tuples:

```python
actual_targets = tuple(step.target for step in parse_mission_plan(raw).steps)
passed = expected_targets is not None and actual_targets == expected_targets
```

Report `<passed>/12`, unsafe acceptance count, elapsed seconds, and provider request count. The probe processes one text and prints only the normalized plan.

- [ ] **Step 4: Install data and entry points, then add evaluation to release gate**

Install `evaluation/mission_cases.json` and add:

```python
"evaluate_missions = agent_gateway.mission_evaluation_cli:main",
"probe_mission = agent_gateway.mission_probe:main",
```

Run Fake mission evaluation in `verify_release.sh` immediately after the existing 20 intent cases.

- [ ] **Step 5: Run all gateway tests and Fake evaluation**

Run:

```bash
cd /mnt/old-linux/current-data/sheng/embodied_ws_nav2
source /opt/ros/jazzy/setup.bash
PATH="/usr/bin:/bin:${PATH}" colcon build --symlink-install \
  --packages-up-to agent_gateway --cmake-args -DPython3_EXECUTABLE=/usr/bin/python3
source install/setup.bash
colcon test --packages-select agent_gateway
colcon test-result --test-result-base build/agent_gateway --verbose
ros2 run agent_gateway evaluate_missions --provider fake
ros2 run agent_gateway probe_mission --provider fake \
  "先去充电桩，再去工作台"
```

Expected: zero test failures, mission evaluation `12/12`, and probe targets `dock, workbench` without starting ROS.

- [ ] **Step 6: Commit evaluation evidence**

```bash
git add agent_gateway/evaluation/mission_cases.json \
  agent_gateway/agent_gateway/mission_evaluation.py \
  agent_gateway/agent_gateway/mission_evaluation_cli.py \
  agent_gateway/agent_gateway/mission_probe.py \
  agent_gateway/test/test_mission_evaluation.py \
  agent_gateway/test/test_mission_probe.py \
  agent_gateway/setup.py scripts/verify_release.sh
git commit -m "test: evaluate bounded AI missions"
```

### Task 6: Mission Mode on the Real Nav2 System Smoke

**Files:**
- Modify: `scripts/smoke_nav2_sim.sh`
- Create: `scripts/validate_nav2_smoke_mode.sh`
- Create: `simulation/test/test_nav2_smoke_mode.py`

**Interfaces:**
- Consumes: existing Gazebo/Nav2 launch lifecycle, `run_mission`, and `AI_MISSION_SMOKE_PROVIDER`.
- Produces: default direct-Goal smoke unchanged plus opt-in mission mode that runs Fake/official/compatible planning through real Nav2.

- [ ] **Step 1: Write a failing shell-mode regression test**

The test executes the missing validator with one argument and checks exit codes:

```python
VALIDATOR = Path(__file__).parents[2] / "scripts" / "validate_nav2_smoke_mode.sh"


@pytest.mark.parametrize("mode", ["direct", "mission"])
def test_accepts_supported_nav2_smoke_modes(mode):
    assert subprocess.run(["bash", str(VALIDATOR), mode], check=False).returncode == 0


def test_rejects_unknown_nav2_smoke_mode():
    assert subprocess.run(
        ["bash", str(VALIDATOR), "anything"], check=False
    ).returncode != 0
```

The real mission-mode smoke in Step 4 verifies command selection and ordered mission execution; default direct mode remains protected by its existing system evidence.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
/usr/bin/python3 -m pytest simulation/test/test_nav2_smoke_mode.py -q
```

Expected: FAIL because mission mode is not implemented.

- [ ] **Step 3: Add opt-in mission mode without changing direct mode**

Create the validator:

```bash
#!/usr/bin/env bash
[[ "${1:-}" == "direct" || "${1:-}" == "mission" ]]
```

Accept:

```bash
NAV2_SMOKE_MODE=mission
AI_MISSION_SMOKE_PROVIDER=fake
```

After the existing readiness gate, mission mode runs:

```bash
timeout 240s ros2 run agent_gateway run_mission \
  --provider "${AI_MISSION_SMOKE_PROVIDER}" --yes \
  "先去充电桩，再去工作台"
```

Assert ordered step success, `AI decision: continue`, `Mission result: completed`, and a summary. Default direct mode remains the already-verified two Action calls.

- [ ] **Step 4: Run syntax, focused tests, and real Fake-model/Nav2 mission**

Run:

```bash
bash -n scripts/smoke_nav2_sim.sh
/usr/bin/python3 -m pytest simulation/test/test_nav2_smoke_mode.py -q
cd /mnt/old-linux/current-data/sheng/embodied_ws_nav2
NAV2_SMOKE_MODE=mission AI_MISSION_SMOKE_PROVIDER=fake EMBODIED_WS="$PWD" \
  bash src/embodied-agent-runtime/scripts/smoke_nav2_sim.sh
```

Expected: Nav2 reaches active, the mission plans `dock -> workbench`, both outer ExecuteTask steps return success through real Nav2, and the final mission smoke line passes.

- [ ] **Step 5: Commit real system mission evidence**

```bash
git add scripts/smoke_nav2_sim.sh scripts/validate_nav2_smoke_mode.sh \
  simulation/test/test_nav2_smoke_mode.py
git commit -m "test: run AI mission through real Nav2"
```

### Task 7: Reviewer-Facing Evidence and Final Verification

**Files:**
- Modify: `README.md`
- Modify: `agent_gateway/README.md`
- Modify: `CHANGELOG.md`
- Modify: `docs/project-roadmap.md`
- Modify: `docs/project-talking-points.zh-CN.md`
- Create: `docs/learning-session-18-bounded-ai-mission-agent.zh-CN.md`
- Modify: `docs/learning-guide.md`
- Modify: `docs/study-path.md`
- Modify: `docs/release-checklist.zh-CN.md`

**Interfaces:**
- Consumes: exact full test total, 20/20 intent result, 12/12 mission result, offline mission smoke, and real Nav2 mission output.
- Produces: consistent public evidence and a Chinese lesson that explains all three AI participation points and their safety boundaries.

- [ ] **Step 1: Update README and package usage from measured output**

Document this exact chain without claiming a real model call unless one was actually made:

```text
“先去充电桩，再去工作台”
  -> FakeMissionModel / optional real Provider
  -> strict MissionPlan(dock, workbench)
  -> AI checkpoint continue
  -> ExecuteTask(dock) -> real Nav2 -> success
  -> ExecuteTask(workbench) -> real Nav2 -> success
  -> read-only AI summary
```

Keep these limitations explicit: no live model evidence without a Key, no model coordinates, no enforced keepout, no real hardware, no hardware emergency-stop claim.

- [ ] **Step 2: Add Session 18 and project answers**

Teach MissionPlan validation, why the model participates multiple times, why final-step success skips a checkpoint call, why Provider failure aborts, how return_home stays Guard-checked, and how Fake/real-provider evidence differs.

- [ ] **Step 3: Run documentation consistency scans**

Run:

```bash
rg -n 'MissionPlan|12/12|checkpoint|return_home|read-only|AI 参与' \
  README.md agent_gateway/README.md docs
rg -n 'model.*coordinates|模型.*坐标|enforced: false|硬件急停' \
  README.md agent_gateway/README.md docs
git diff --check
```

Expected: evidence and limitations appear consistently with no stale test total.

- [ ] **Step 4: Run the complete release gate**

Run:

```bash
EMBODIED_WS=/mnt/old-linux/current-data/sheng/embodied_ws_nav2 \
  bash scripts/verify_release.sh
```

Expected: five packages build, all tests pass, intent evaluation is 20/20, mission evaluation is 12/12, Runtime/AI/mission smokes pass, and no credentials or large files are detected.

- [ ] **Step 5: Rerun and retain concise real Nav2 mission evidence**

Run:

```bash
cd /mnt/old-linux/current-data/sheng/embodied_ws_nav2
mkdir -p log/ai-mission-evidence
NAV2_SMOKE_MODE=mission AI_MISSION_SMOKE_PROVIDER=fake EMBODIED_WS="$PWD" \
  bash src/embodied-agent-runtime/scripts/smoke_nav2_sim.sh \
  > log/ai-mission-evidence/smoke-2026-07-17.log 2>&1
rg -n 'Mission plan|Step result|AI decision|Mission result|AI summary|checks passed' \
  log/ai-mission-evidence/smoke-2026-07-17.log
```

Expected: ordered dock/workbench success, one continue decision, completed mission, summary, and final smoke success.

- [ ] **Step 6: Commit reviewer-facing evidence**

```bash
git add README.md agent_gateway/README.md CHANGELOG.md \
  docs/project-roadmap.md docs/project-talking-points.zh-CN.md \
  docs/learning-session-18-bounded-ai-mission-agent.zh-CN.md \
  docs/learning-guide.md docs/study-path.md docs/release-checklist.zh-CN.md
git commit -m "docs: teach bounded AI mission execution"
```

- [ ] **Step 7: Verify the local branch is ready for later unified review**

Run:

```bash
git diff --check feature/nav2-system-evidence...HEAD
git status --short --branch
git log --oneline --decorate feature/nav2-system-evidence..HEAD
```

Expected: clean `feature/ai-mission-planner`, local commits only, no push or merge.
