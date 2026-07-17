# Runtime Simulation

`runtime_simulation` is the reproducible system-integration layer for the
runtime. It composes installed ROS 2 Jazzy packages instead of copying their
source trees:

```text
TurtleBot3 Burger + Gazebo Sim
  -> robot topics and TF bridge
  -> AMCL + Nav2 lifecycle stack
  -> NavigateToPose Action
  -> task_executor ExecuteTask adapter
```

The launch file also starts a bounded AMCL initial-pose publisher and an RViz
marker publisher for the reviewed named targets. The same
`config/targets.yaml` remains the source used by `task_executor` at build time.
Before starting Gazebo, the system smoke parses the upstream occupancy PGM and
requires at least 0.32 m of free-space clearance around every named target.

## Install the system dependencies

```bash
sudo apt update
sudo apt install ros-jazzy-navigation2 ros-jazzy-nav2-bringup \
  ros-jazzy-robot-state-publisher \
  ros-jazzy-turtlebot3-gazebo ros-jazzy-turtlebot3-navigation2 \
  ros-jazzy-rviz2
```

Check them without starting motion or simulation:

```bash
bash scripts/check_nav2_sim_environment.sh
```

## Build and run

```bash
cd ~/embodied_ws
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install --packages-up-to runtime_simulation
source install/setup.bash
ros2 launch runtime_simulation runtime_nav2_sim.launch.py
```

The default launch uses TurtleBot3 Burger, the upstream `turtlebot3_world`, its
matching static map, simulated time, Nav2 autostart, and this repository's RViz
view. The robot starts at `home` (`-2.0`, `-0.5`) and the other reviewed free
poses are `dock` (`0.0`, `-2.0`) and `workbench` (`0.0`, `2.0`).

For a non-GUI integration run:

```bash
ros2 launch runtime_simulation runtime_nav2_sim.launch.py \
  headless:=true use_rviz:=false
```

Run the end-to-end proof after the packages are installed and built:

```bash
bash scripts/smoke_nav2_sim.sh
```

The smoke waits for the Nav2 lifecycle stack, then sends two sequential
`ExecuteTask` Goals. They travel through the real Nav2 `NavigateToPose` Action,
not the deterministic fake server used by fast tests.

On 2026-07-17 this command was verified locally with ROS 2 Jazzy: Nav2 reached
`active`, then `home -> dock` and `dock -> workbench` both returned Runtime
`final_state: 5`, `error_code: 0`, `attempts: 1`, and ROS Action `SUCCEEDED`.

## Deliberate limitation

The restricted-area polygon is visible in RViz but currently has
`enforced: false`. It is not yet connected to a Nav2 costmap filter, so the
project does not claim keepout enforcement. The next keepout increment must
add a mask, filter-info server, costmap filter plugin configuration, and a
system test that proves planning refuses the region.

SLAM, vision, manipulation, hardware safety, and multi-robot scenarios remain
outside this simulation package.
