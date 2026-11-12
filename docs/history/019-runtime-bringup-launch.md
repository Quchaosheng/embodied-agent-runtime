# One Launch Description for the Whole Stack

Seven processes have to be running for `ExecuteWorkflow` to reach a device, and
the only place that said so was `scripts/run_industrial_e2e.sh`. Everything a
person actually deploys with — a bringup — existed as instructions in a README
and as `--ros-args` lines inside a 719-line test script.

Two of the values those processes must agree on were dangerous to get wrong:

- **`database_path`** defaults to the relative `runtime_history.sqlite3`.
  Started from different working directories, `runtime_history` and
  `runtime_gateway` open *different files* and neither complains. The gateway
  then agrees to a `task_id` the history never recorded, and `GetTask` reports
  "not found" for a task that ran.
- **`stage_event_log_path`** has to be the same file in the bridge, the executor
  and the gateway, or `scripts/stage_latency_report.py` silently gets a partial
  capture — the exact failure mode 015 is built on.

## What was added

A `runtime_bringup` package whose only content is `launch/runtime.launch.py`.
It starts the six core processes (bridge, executor, monitor, history,
orchestrator, gateway) plus the software ECU, and takes the shared paths as
launch arguments with absolute defaults under `/var/tmp/embodied-agent-runtime/`.

Parameters that belong to one node still live in that package's own params
file; the launch file passes them through and overrides only the shared values.
The orchestrator gets no params file at all: it resolves `workflows.xml` from
its own share directory, so a second copy here could only disagree with it.

`respawn` is a launch argument and defaults to **false**. Restarting a crashed
bridge automatically would hide the crash, and today a restart is exactly what
makes the first STOP get refused by a device that kept its replay cache — see
017 and the stop-id fix. It is offered, not enabled.

The launch description creates the parent directory of both shared paths before
starting anything, because SQLite will not create a directory and the resulting
error ("unable to open database file") points at the wrong thing.

## What is verified, and what is not

`test_runtime_launch` passes (6 cases) and checks the description, not a running
stack:

- every core process appears exactly once;
- `database_path` reaches both `runtime_history` and `runtime_gateway` as the
  same value, and `stage_event_log_path` reaches all three writers;
- `can_interface` overrides reach both ends of the bus;
- `use_virtual_device:=false` drops the ECU;
- the missing parent directory gets created;
- `respawn` stays off by default.

The package builds and its own `colcon test` passes 4 of 5: `uncrustify` is not
run for Python, `flake8` fails only because `ament_flake8` cannot start its
process pool in this sandbox (the same finding as 015). Run single-process with
the ament config, `flake8` reports nothing.

**The stack was not launched end to end here.** `runtime_gateway` and
`task_orchestrator` cannot be built on this host (no gRPC/protobuf or
BehaviorTree.CPP development packages), and there is no `vcan0`. Running the
description against the packages that *can* be built stops at the first missing
executable, which is what a launch file is supposed to do. The first real run
is CI or the container, not this session.

## Follow-ups this does not do

- The E2E script still starts its own processes and does not use this launch
  file. Replacing 60 lines of `start_direct` with an `IncludeLaunchDescription`
  would be the right next step, but the E2E also starts nodes mid-scenario
  (recovered bridge, recovered executor) and asserts on PIDs, so it is a
  separate change with its own evidence.
- `database_path` still has a relative default in the two nodes themselves.
  Making it required would break every existing invocation; the launch file
  gives one correct path, which is what the check was for.
