# Rosbag Task Audit Design

## Goal

Add reproducible, cross-process evidence for the existing `/task_events`
stream. One command records known success and rejection scenarios to an MCAP
bag, reads the bag back, and fails unless the persisted task timelines match
the Runtime contract.

This work starts from `feature/task-event-observability` on the independent
`feature/rosbag-task-audit` branch. It does not merge the Runtime readiness,
SocketCAN, Nav2 simulation, or README visual branches.

## Scope

The first audit records only `/task_events`. The TaskEvent branch predates the
Runtime diagnostics branch, so recording `/diagnostics` here would require an
early cross-feature merge. The final integration branch will add diagnostics
to the same recording command.

The feature does not add a database, a custom storage format, committed bag
artifacts, Foxglove layouts, or new Runtime state. Rosbag2 and its installed
MCAP plugin already provide the required persistence.

`.gitignore` will exclude `*.mcap` and the task-audit output directory pattern.
It will not ignore generic `metadata.yaml` files because that name may be used
by legitimate version-controlled configuration.

## Data Flow

```text
fake NavigateToPose + task_executor
  -> /task_events
  -> ros2 bag record --storage mcap
  -> temporary MCAP bag
  -> audit_task_event_bag.py
  -> deterministic timeline output
  -> smoke assertions
```

The smoke runs two requests:

1. `bag-success`: valid `dock` request.
2. `bag-rejected`: invalid `laboratory` target.

Expected persisted evidence:

```text
bag-success:  VALIDATING -> DISPATCHING -> RUNNING -> SUCCEEDED
bag-rejected: VALIDATING -> FAILED(error 13, attempt 0)
```

## Components

### MCAP reader

`scripts/audit_task_event_bag.py` uses the installed `rosbag2_py`
`SequentialReader` and ROS deserialization APIs. It accepts one bag directory,
rejects missing or unreadable input, reads `/task_events`, and prints one
stable line per event:

```text
task_id=bag-success state=1 error_code=0 attempt=0 detail=validating task
```

The reader does not own policy. It only turns persisted messages into a stable
text form that shell assertions and project demonstrations can inspect.

### End-to-end smoke

`scripts/smoke_task_event_bag.sh`:

1. Sources ROS and the selected workspace.
2. Starts fake navigation and `task_executor` in an isolated ROS domain.
3. Starts `ros2 bag record` with MCAP storage before sending any Goal.
4. Sends the success and rejection Goals with fixed task IDs.
5. Stops the recorder with SIGINT and waits for a clean exit.
6. Runs the MCAP reader.
7. Asserts exact state order and terminal error/attempt values.
8. Removes the temporary bag on success and retains it on failure.

All background processes are cleaned up by a single shell trap. Each wait is
condition-based and bounded.

### Release and learning evidence

The local release gate runs the new smoke after the existing Runtime and AI
smokes. CI installs rosbag2 through declared test metadata and runs the same
command without credentials or network model requests.

Documentation adds:

- README commands and an accurate evidence statement.
- A Chinese lesson 20 explaining transient-local history versus rosbag2.
- Project answers covering MCAP, replay, audit limits, and artifact size.
- A release checklist item that forbids committing generated bag files.
- Narrow Git ignore rules for MCAP files and task-audit output directories.

## Error Handling

The smoke fails when:

- the Action servers or bag recorder do not become ready before their timeout;
- either Goal produces an unexpected result;
- the recorder exits unsuccessfully;
- `/task_events` is absent or cannot be deserialized;
- an expected task is missing, duplicated, out of order, or has the wrong
  terminal error code or attempt count.

Generated bags live under `${TMPDIR:-/tmp}`. They are deleted after success and
retained with their path printed after failure.

## Verification

The implementation is complete when all of the following pass from the large
ROS workspace:

```bash
colcon build --symlink-install --packages-up-to task_executor agent_gateway
colcon test --packages-select task_contract task_guard task_executor agent_gateway
colcon test-result --verbose
bash scripts/smoke_task_event_bag.sh
bash scripts/verify_release.sh
```

Static checks include `bash -n`, Python compilation, XML validation, and
`git diff --check`. The generated MCAP directory must not appear in Git status.

## Deferred Work

- Record `/diagnostics` after the readiness branch is integrated.
- Add Foxglove Bridge and a captured layout after TaskEvent and Nav2 simulation
  share one integration branch.
- Define retention, upload, or privacy policy only when bags leave the local
  demonstration machine.
