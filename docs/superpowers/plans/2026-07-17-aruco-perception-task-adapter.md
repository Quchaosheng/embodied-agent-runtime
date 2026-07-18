# ArUco Perception Task Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a deterministic ArUco image/camera adapter that maps marker 10 to `single_task/dock_a` and marker 20 to `ready_then_task/home`, submitting each physical appearance at most once through `ExecuteWorkflow`.

**Architecture:** Create one `perception_task_adapter` ROS 2 package with a pure detector library, a pure trigger state object, and a testable ROS Action node. It uses the existing `execute_workflow` Action and never calls `ExecuteTask` or CAN directly.

**Tech Stack:** C++17, ROS 2 Jazzy, `rclcpp`, `rclcpp_action`, `robot_task_interfaces`, OpenCV core/imgcodecs/videoio/aruco, GoogleTest, ament/colcon.

**Design reference:** `docs/superpowers/specs/2026-07-17-aruco-perception-task-adapter-design.md`

---

## File Map

Create under `ros2_ws/src/perception_task_adapter/`:

- `package.xml`, `CMakeLists.txt`, `config/perception_task_adapter.yaml`
- `include/perception_task_adapter/aruco_detector.hpp`, `src/aruco_detector.cpp`
- `include/perception_task_adapter/marker_trigger.hpp`, `src/marker_trigger.cpp`
- `include/perception_task_adapter/perception_task_adapter_node.hpp`, `src/perception_task_adapter_node.cpp`
- `src/perception_task_adapter_main.cpp`
- `test/test_aruco_detector.cpp`, `test/test_marker_trigger.cpp`, `test/test_action_submission.cpp`

Modify only after public evidence: `README.md` and, only if needed,
`scripts/check_arm64_environment.sh`. Record only evidence produced by the
public verification commands. Do not modify `ai_task_adapter`, `task_executor`,
`device_bridge`, `task_orchestrator`, or `runtime_gateway`.

### Task 1: Package Skeleton and Trigger State

**Files:** package manifest, CMake, YAML, marker trigger header/source/test.

- [ ] **Step 1: Add package and defaults**

Use `ament_cmake`, `rclcpp`, `rclcpp_action`, `robot_task_interfaces`, `OpenCV`, `ament_cmake_gtest`, `ament_lint_auto`, and `ament_lint_common`. Build a C++17 `marker_trigger` library and add tests only inside `if(BUILD_TESTING)`.

```yaml
perception_task_adapter:
  ros__parameters:
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

- [ ] **Step 2: Write RED tests**

Define and test:

```cpp
struct MarkerMapping { int marker_id; std::string workflow_id; std::string target_id; };
struct TriggerEvent { MarkerMapping mapping; };
class MarkerTrigger {
public:
  MarkerTrigger(std::vector<MarkerMapping>, std::size_t confirm_frames,
                std::size_t rearm_missing_frames);
  static std::string validate(const std::vector<MarkerMapping>&,
                              std::size_t, std::size_t);
  std::optional<TriggerEvent> observe(const std::vector<int>& visible_ids,
                                      bool workflow_active, bool immediate = false);
  void on_terminal();
};
```

Cover invalid empty/duplicate mappings, IDs outside `0..49`, three-frame confirmation, persistent suppression, five-frame rearm, ambiguity rejection, and direct replacement of ID 10 by ID 20 without an empty interval.

- [ ] **Step 3: Verify RED**

```bash
set +u; source /opt/ros/jazzy/setup.bash; set -u
cd ~/embodied-agent-runtime/ros2_ws
colcon test --packages-select perception_task_adapter \
  --ctest-args -R test_marker_trigger --output-on-failure
```

Expected: discovery or compilation fails because the package target and `MarkerTrigger` do not exist.

- [ ] **Step 4: Implement the minimum state machine**

A single allowlisted ID increments confirmation and triggers at `confirm_frames`; multiple allowlisted IDs clear the streak and never rearm; no allowlisted ID increments the missing streak only while disarmed; rearm occurs at `rearm_missing_frames`. `workflow_active=true` suppresses triggers. `on_terminal()` disarms and clears streaks. `immediate=true` bypasses confirmation for image mode but still rejects ambiguity and unknown-only input.

- [ ] **Step 5: Run and commit**

```bash
colcon build --packages-select perception_task_adapter --cmake-args -DBUILD_TESTING=ON
colcon test --packages-select perception_task_adapter --event-handlers console_direct+
colcon test-result --test-result-base build/perception_task_adapter --verbose
git add ros2_ws/src/perception_task_adapter
git commit -m "feat: add ArUco marker trigger state"
```

Expected: zero trigger-test errors and failures.

### Task 2: ArUco Detector

**Files:** detector header/source/test and CMake.

- [ ] **Step 1: Add API and RED tests**

```cpp
struct DetectedMarker {
  int marker_id;
  std::array<cv::Point2f, 4> corners;
};
class ArucoDetector {
public:
  ArucoDetector();
  std::vector<DetectedMarker> detect(const cv::Mat&) const;
};
```

Generate 400x400 markers at runtime with `getPredefinedDictionary(DICT_4X4_50)` and `drawMarker`. Test ID 10, ID 20, empty, and two-marker images. The initial run must fail because the detector target is absent.

- [ ] **Step 2: Implement**

Use `cv::aruco::detectMarkers` with a dictionary created once from `DICT_4X4_50`. Return one result per ID and exactly four corners. Do not add pose, confidence, distance, or orientation.

```cmake
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs aruco)
add_library(aruco_detector src/aruco_detector.cpp)
target_compile_features(aruco_detector PUBLIC cxx_std_17)
target_include_directories(aruco_detector PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(aruco_detector PUBLIC ${OpenCV_LIBS})
```

- [ ] **Step 3: Verify and commit**

```bash
colcon build --packages-select perception_task_adapter --cmake-args -DBUILD_TESTING=ON
colcon test --packages-select perception_task_adapter \
  --ctest-args -R test_aruco_detector --output-on-failure
colcon test-result --test-result-base build/perception_task_adapter --verbose
git add ros2_ws/src/perception_task_adapter
git commit -m "feat: detect fixed ArUco dictionary markers"
```

Expected: detector tests pass with zero errors/failures. If OpenCV or `aruco`
is unavailable, return the dependency report and do not alter the target image.

### Task 3: ROS Action Adapter and Image Mode

**Files:** node header/source/main, CMake, YAML, Action integration test.

- [ ] **Step 1: Define the testable node**

```cpp
class PerceptionTaskAdapterNode : public rclcpp::Node {
public:
  explicit PerceptionTaskAdapterNode(const rclcpp::NodeOptions& options = {});
  bool process_frame(const cv::Mat& frame, bool immediate);
  int run_image();
  int run_camera();
  void cancel_active();
};
```

The constructor declares and validates all YAML parameters, constructs `ArucoDetector` and `MarkerTrigger`, and creates an Action client for exactly `execute_workflow`. It must not include `ExecuteTask` or CAN packages.

- [ ] **Step 2: Write RED Action test**

Run a C++ fake `ExecuteWorkflow` Action Server in a `MultiThreadedExecutor`. Generate an ID 10 image, construct the ID 10/20 mapping, call `process_frame(image, true)`, and assert one Goal with `single_task`, `dock_a`, nonempty IDs, and positive duration. A second call while active creates no Goal. After completion plus five empty frames, ID 10 creates exactly one second Goal. An image with IDs 10 and 20 creates no Goal. The first run must fail to compile because the node class is absent.

- [ ] **Step 3: Implement Goal submission**

Use `rclcpp_action::create_client<ExecuteWorkflow>(this, "execute_workflow")`. Build:

```cpp
goal.request_id = "aruco-" + std::to_string(marker_id) + "-" +
  timestamp + "-" + sequence;
goal.workflow_id = mapping.workflow_id;
goal.task_id = goal.request_id;
goal.target_id = mapping.target_id;
goal.allowed_duration.sec = static_cast<std::int32_t>(duration_ms / 1000);
goal.allowed_duration.nanosec =
  static_cast<std::uint32_t>((duration_ms % 1000) * 1000000);
```

Set the active flag before dispatch. Null Goal handle, server timeout, Action error, and terminal results clear the flag and call `on_terminal()`; never retry automatically.

- [ ] **Step 4: Implement image mode and main**

`run_image()` reads `image_path` with `cv::imread(..., IMREAD_COLOR)\), returns 2 for an unreadable image or no single allowlisted marker, calls `process_frame(frame, true)`, spins until Goal acceptance/result or a five-second server timeout, and returns 0 only for a completed Action. Main initializes ROS, dispatches the configured mode, calls `cancel_active()` before shutdown, and returns the exit code.

- [ ] **Step 5: Build, test, commit**

```bash
colcon build --packages-select perception_task_adapter --cmake-args -DBUILD_TESTING=ON
colcon test --packages-select perception_task_adapter \
  --ctest-args -R test_action_submission --output-on-failure
colcon test-result --test-result-base build/perception_task_adapter --verbose
git add ros2_ws/src/perception_task_adapter
git commit -m "feat: submit ArUco image observations as workflows"
```

Expected: fake-server Goal and duplicate-suppression assertions pass.

### Task 4: Camera Mode and Smoke Coverage

**Files:** node source/main and Action test.

- [ ] **Step 1: Implement bounded camera mode**

Open `cv::VideoCapture(camera_index)\), return 2 if it cannot open, call `process_frame(frame, false)`, call `rclcpp::spin_some` once per loop, and return 2 after exactly five consecutive read failures. Keep confirmation at three frames and rearm at five empty frames.

- [ ] **Step 2: Extend hardware-free tests**

Model camera frames directly: two ID 10 frames produce no Goal, the third produces one; persistent ID 10 produces no duplicate; five empty frames rearm; a new ID 10 produces one more Goal; a two-marker frame resets confirmation. Do not require a physical camera in automated tests.

- [ ] **Step 3: Run package and image smoke**

```bash
colcon build --packages-select perception_task_adapter --cmake-args -DBUILD_TESTING=ON
colcon test --packages-select perception_task_adapter --event-handlers console_direct+
colcon test-result --test-result-base build/perception_task_adapter --verbose
source install/setup.bash
ros2 run perception_task_adapter perception_task_adapter_node \
  --ros-args --params-file ../src/perception_task_adapter/config/perception_task_adapter.yaml \
  -p input_mode:=image -p image_path:=/tmp/aruco-id-10.png
```

Expected: all tests pass and image mode submits one `single_task/dock_a` Goal.

- [ ] **Step 4: Commit**

```bash
git add ros2_ws/src/perception_task_adapter
git commit -m "feat: add ArUco camera trigger mode"
```

### Task 5: ARM64 Smoke, Documentation, Regression

**Files:** README, status/task docs, and environment script only if required.

- [ ] **Step 1: Verify OpenCV read-only**

```bash
pkg-config --modversion opencv4
pkg-config --exists opencv4
```

If either fails, return the report and wait for an approved dependency strategy. Do not run `apt upgrade`, replace ROS, or install an unapproved vendor package.

- [ ] **Step 2: Build/test ARM64**

```bash
cd ~/embodied-agent-runtime
bash scripts/build_on_arm64.sh
set +u; source /opt/ros/jazzy/setup.bash; source ros2_ws/install/setup.bash; set -u
cd ros2_ws
colcon test --packages-select perception_task_adapter --return-code-on-test-failure
colcon test-result --test-result-base build --verbose
```

Expected: ARM64 compilation succeeds and the package has zero errors/failures. Record compiler, OpenCV, and ROS versions.

- [ ] **Step 3: Run bounded software smoke**

Use a generated ID 10 PNG with the verified Virtual Device, Device Bridge, Task Executor, Monitor, and Orchestrator. Capture the detector log, one `single_task/dock_a` Goal, one terminal workflow result, one SQLite event, and cleanup output. Repeat with ID 20 and assert `ready_then_task/home`. This remains software/vcan evidence, not physical CAN or physical-stop evidence.

- [ ] **Step 4: Update measured documentation**

Update `README.md` package count and optional chain only after fresh evidence.
Preserve BPU/NPU, camera, GPIO, physical CAN, physical stop, security, and
throughput exclusions.

- [ ] **Step 5: Run affected regression and commit**

```bash
cd ~/embodied-agent-runtime/ros2_ws
colcon test --packages-select \
  robot_task_interfaces runtime_can virtual_can_device device_bridge \
  task_executor runtime_monitor runtime_history task_orchestrator \
  runtime_gateway ai_task_adapter perception_task_adapter \
  --return-code-on-test-failure
colcon test-result --test-result-base build --all --verbose
git add README.md \
  ros2_ws/src/perception_task_adapter \
  scripts/check_arm64_environment.sh
git commit -m "feat: verify ArUco perception workflow gate"
```

Expected: existing totals remain explainable, the new package adds no errors/failures, and cppcheck skips retain their documented reason.

## Self-Review

- Spec coverage: fixed dictionary, both input modes, mapping, single-trigger semantics, ambiguity, Action boundary, bounded camera failure, shutdown, tests, Windows workflow, and evidence exclusions each have a task.
- Placeholder scan: no `TBD`, `TODO`, `FIXME`, or vague handling instructions.
- Type consistency: `MarkerMapping`, `TriggerEvent`, `MarkerTrigger`, `DetectedMarker`, `ArucoDetector`, and `PerceptionTaskAdapterNode` signatures match across tasks.
- Scope: no BPU, pose estimation, GUI, recording, queue, dynamic mapping, or direct device access.
