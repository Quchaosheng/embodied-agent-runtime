# ArUco Perception Task Adapter Design

Date: 2026-07-17

## 1. Goal

Add one bounded perception demo to the existing Robot Runtime. The demo detects
allowlisted ArUco markers from either a still image or a USB camera and submits
an existing `ExecuteWorkflow` Action Goal.

The first mapping is fixed in configuration:

| Marker ID | Workflow | Target |
| --- | --- | --- |
| 10 | `single_task` | `dock_a` |
| 20 | `ready_then_task` | `home` |

This feature extends the verified software control chain without allowing
perception code to access `ExecuteTask`, Device Bridge, SocketCAN, or CAN frames.

## 2. Evidence Boundary

The feature may claim only that an ArUco observation was converted into a
validated `ExecuteWorkflow` request and exercised through the existing software
runtime. It does not prove object detection, BPU inference, camera calibration,
physical navigation, real CAN behavior, physical stopping, or production safety.

## 3. Package Boundary

Create one ROS 2 package named `perception_task_adapter`.

The package contains:

- `ArucoDetector`: a small C++17 wrapper over OpenCV ArUco. It accepts a
  `cv::Mat` and returns detected marker IDs and corners.
- `MarkerTrigger`: a ROS-independent state object that validates the parallel
  mapping arrays and implements confirmation, ambiguity rejection, duplicate
  suppression, and rearming.
- `perception_task_adapter_node`: selects still-image or USB-camera input and
  submits `robot_task_interfaces/action/ExecuteWorkflow` goals.

These components are concrete types in one package. No plugin API, factory,
generic perception interface, or new ROS message is introduced.

## 4. Data Flow

```text
still image or USB camera
  -> OpenCV DICT_4X4_50 detector
  -> allowlist and trigger state
  -> ExecuteWorkflow Action Client
  -> task_orchestrator
  -> existing ExecuteTask and device-control chain
```

The ArUco dictionary is fixed to `DICT_4X4_50` in the first version. Runtime
selection of arbitrary dictionaries is outside scope.

## 5. Configuration

The node uses ROS parameters with conservative defaults:

```yaml
input_mode: image
image_path: ""
camera_index: 0
marker_ids: [10, 20]
workflow_ids: [single_task, ready_then_task]
target_ids: [dock_a, home]
allowed_duration_ms: 3000
confirm_frames: 3
rearm_missing_frames: 5
```

Startup validation requires:

- `input_mode` is exactly `image` or `camera`.
- The three mapping arrays have the same nonzero length.
- Marker IDs are unique and valid for `DICT_4X4_50`.
- Workflow and target strings are nonempty.
- `allowed_duration_ms`, `confirm_frames`, and `rearm_missing_frames` are
  positive and safely representable by their target ROS types.
- Image mode has a readable `image_path`.

Invalid configuration prevents the node from running and produces a specific
error log. Configuration is loaded once at startup; hot reload is not included.

## 6. Detection And Trigger Semantics

Image mode loads one image, performs one detection pass, treats a single
allowlisted marker as immediately confirmed, submits at most one workflow, and
then exits after the Action reaches a terminal state or submission fails. No
allowlisted marker, an unknown marker, or multiple allowlisted markers produce
no Goal and a nonzero process exit.

Camera mode applies the following rules:

1. Exactly one allowlisted marker must be visible.
2. The same marker must be visible for `confirm_frames` consecutive frames.
3. The confirmed appearance submits at most one workflow.
4. Continued visibility never submits duplicates.
5. The trigger rearms only after no allowlisted marker is visible for
   `rearm_missing_frames` consecutive frames.
6. Two or more allowlisted markers are ambiguous. They reset confirmation and
   do not count toward rearming.
7. A different marker appearing without an empty interval does not bypass the
   rearm rule.

The node permits only one workflow submitted by this process to be active. It
does not queue observations while busy. Trigger confirmation is disabled while
the Goal is active. After the terminal callback, the trigger remains disarmed
until `rearm_missing_frames` empty frames are observed, so a marker must leave
the frame and reappear after the active workflow finishes before it can trigger.

Non-allowlisted markers may be logged at debug level but never produce a Goal.

## 7. Action Submission

For an accepted trigger, the node constructs an `ExecuteWorkflow::Goal` using
the configured workflow and target. `request_id` and `task_id` use the marker ID,
the current Unix millisecond timestamp, and a process-local sequence number to
avoid duplicates during normal operation.

The node waits up to five seconds for the `execute_workflow` server when a
trigger occurs. Goal rejection, server unavailability, and Action errors are
reported but are not automatically retried. Retrying a perception-triggered
robot task without a new physical observation is intentionally forbidden.

Feedback is logged at debug level. Terminal outcome, error code, and message are
logged at info or error level according to the result. A graceful node shutdown
requests cancellation of its active workflow and waits for at most two seconds
for the cancellation response; it never reports that the device stopped unless
the existing runtime returns the corresponding terminal result.

## 8. Failure Handling

- Missing or unreadable image: exit nonzero before Action submission.
- Camera open failure: exit nonzero with the camera index in the error.
- Repeated camera read failure: stop after five consecutive failures instead of
  spinning forever.
- OpenCV exception: catch at the node boundary, log the message, and exit or
  stop capture without submitting a Goal from that frame.
- Ambiguous markers: camera mode rejects the frame and remains idle; image mode
  exits nonzero.
- Invalid mapping: reject startup.
- Orchestrator busy or Goal rejected: record the failure and require a fresh
  marker appearance; do not queue or retry.
- Node shutdown with an active Goal: request Action cancellation and preserve
  the runtime's actual result boundary.

No image or video is stored by default.

## 9. Verification

The minimum automated checks are:

1. Generate ArUco markers in memory with OpenCV and verify detection of ID 10,
   ID 20, no marker, and multiple markers.
2. Verify mapping validation rejects unequal arrays, duplicate IDs, invalid IDs,
   and empty workflow or target values.
3. Verify camera trigger state: three-frame confirmation, persistent-marker
   duplicate suppression, five-frame rearm, ambiguity rejection, and direct
   marker replacement without rearm.
4. Use a fake `ExecuteWorkflow` Action Server to verify exact Goal fields,
   exactly one Goal per appearance, rejection handling, and terminal result
   handling.
5. Run image mode with a generated PNG and verify one structured workflow is
   submitted.
6. Perform one manual USB-camera smoke test on a Linux runtime and record
   the detected ID, submitted Goal, terminal result, and absence of duplicate
   submissions. Generic ARM64 is the primary target; X5 remains an optional
   manual profile.

Tests generate marker images at runtime; binary fixtures are not committed.

## 10. Windows Development Workflow

Source editing, Git review, configuration, and generated test-image inspection
take place in the Windows workspace. ROS 2 compilation, Action integration,
OpenCV linkage, SocketCAN tests, and ARM64 evidence run in a compatible Linux
environment. X5 may be selected as an optional manual profile. No x86_64
build, install, or log tree is copied to an ARM64 target.

## 11. Acceptance Criteria

The feature is complete when:

- `perception_task_adapter` builds with C++17 and the installed OpenCV/ROS 2
  toolchain.
- All package tests report zero errors and zero failures.
- Image mode maps generated marker 10 to `single_task/dock_a` exactly once.
- Image mode maps generated marker 20 to `ready_then_task/home` exactly once.
- Unknown and ambiguous markers submit no Action Goal.
- A persistent camera marker submits no duplicate Goal.
- A removed and re-presented marker can submit a second Goal after rearming.
- The adapter has no dependency on `ExecuteTask`, Device Bridge, or CAN packages.
- Project status documentation records only the evidence actually produced.

## 12. Explicit Non-Goals

- BPU inference or neural-network object detection
- Pose estimation, distance measurement, or camera calibration
- Navigation planning or arbitrary workflow generation
- Dynamic mapping updates or remote configuration
- Multiple-camera support
- Observation queues or automatic Action retry
- GUI preview, video recording, or image retention
- Direct device or CAN access
