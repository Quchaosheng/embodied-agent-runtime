# 第十七课：从 Fake Action 到 Nav2/TurtleBot3 系统仿真

## 这一课解决什么问题

Fake `NavigateToPose` 很适合验证取消、超时、重试和错误映射，但它不能证明地图、
定位、规划、控制、TF 和 Gazebo bridge 能一起工作。本课增加第五个 ROS 2 包
`runtime_simulation`，目标是保持 Runtime 安全边界不变，只替换下游执行环境：

```text
相同 ExecuteTask Goal
  -> 相同 task_guard
  -> 相同 task_executor
  -> 测试时：Fake NavigateToPose
  -> 系统时：Nav2 NavigateToPose -> TurtleBot3 -> Gazebo Sim
```

这不是删除 Fake Server。工程项目通常同时需要快速、确定性的组件测试，以及数量较少、
运行较慢的真实系统测试。

## 新包的职责

`simulation/launch/runtime_nav2_sim.launch.py` 一次编排五部分：

1. 启动官方 TurtleBot3 Gazebo world、机器人模型和 ROS/Gazebo bridge。
2. 启动 AMCL、planner、controller、BT navigator 和 lifecycle manager。
3. 发布与 Gazebo 出生点一致的 AMCL 初始位姿。
4. 启动原有 `execute_task_server`，连接标准 `/navigate_to_pose` Action。
5. 在 RViz 显示地图、机器人、规划路径、命名目标和限制区轮廓。

上游包通过 `package.xml` 和 `rosdep` 安装，仓库没有把 Navigation2 或 TurtleBot3
源码整份复制进来。这样许可证边界清楚，也不会把维护上游代码变成自己的负担。

## 为什么 AMCL 初始位姿不能省

Gazebo 知道机器人出生在世界坐标哪里，不代表 AMCL 自动知道机器人在地图哪里。
如果只启动 Gazebo 和 Nav2，常见现象是 RViz 里缺少 `map -> odom`，导航 Goal 一直
等待定位。`initial_pose_publisher` 会等待 `/initialpose` 出现订阅者，再可靠地发布三次
`home=(-2.0,-0.5,0.0)`，并在超时后明确报错退出。

这体现了一个重要技术复习点：仿真 world 坐标、静态 map 坐标和定位系统初始状态是三个
相关但不同的概念。

## 命名目标为什么改成这些坐标

原来的坐标是占位值，其中 `(0,0)` 在 TurtleBot3 官方 world 的 ROS 图案障碍物上。
本课按官方地图栅格审查并选出三个自由空间点：

| 名称 | 位姿 | 含义 |
| --- | --- | --- |
| `home` | `(-2.0, -0.5, 0.0)` | 与 Burger 出生点一致 |
| `dock` | `(0.0, -2.0, 1.57)` | 下方自由走廊 |
| `workbench` | `(0.0, 2.0, -1.57)` | 上方自由走廊 |

模型仍然只能说目标名字，不能输出这些坐标。坐标仍由版本控制配置和 C++ Runtime
拥有。

`validate_map_targets` 会读取官方 `map.yaml` 和二进制 PGM，把世界坐标转换成栅格
索引，并按 0.32 m 净空扫描目标周围像素。占用、未知、地图外或净空不足都会在启动
系统 smoke 前失败。这把“人工看起来能走”升级成了可重复检查。

## 两层验证如何分工

快速测试继续使用 Fake Action，覆盖异常分支：Goal 拒绝、取消、deadline、第一次
失败后重试、恢复耗尽和 SAFE_STOP。`smoke_nav2_sim.sh` 只保留关键系统证明：等待
Nav2 lifecycle 进入 active，然后让机器人依次从 `home -> dock -> workbench`。

系统 smoke 不应该取代单元测试，因为 Gazebo 更慢，也更容易受图形驱动、CPU 和启动
时序影响；Fake 测试也不能取代系统 smoke，因为它没有 planner、controller、AMCL、
TF 和传感器数据。

## 运行命令

```bash
sudo apt update
sudo apt install ros-jazzy-navigation2 ros-jazzy-nav2-bringup \
  ros-jazzy-turtlebot3-gazebo ros-jazzy-turtlebot3-navigation2 \
  ros-jazzy-rviz2

cd ~/embodied_ws
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install --packages-up-to runtime_simulation
source install/setup.bash

# 图形演示
ros2 launch runtime_simulation runtime_nav2_sim.launch.py

# 自动系统验证
bash src/embodied-agent-runtime/scripts/smoke_nav2_sim.sh
```

## 技术复习官常问

### 1. 既然接口一直是 NavigateToPose，这次集成的工作量在哪里？

接口相同只解决编译边界。系统集成还要对齐 Gazebo world、静态 map、出生点、AMCL
初始位姿、sim time、TF、Nav2 lifecycle、参数文件、ROS/Gazebo bridge 和进程清理。
本项目把这些放进一个可复现 launch，并用 headless smoke 验证外层 `ExecuteTask` 真正
穿过 Nav2。

### 2. 为什么不让大模型直接发 PoseStamped？

因为模型输出不稳定，也无法承担地图版本和安全区域的所有权。模型只选 `dock` 等
名字，Runtime 把名字映射成审查过的 map-frame Pose。换地图时改配置并重测，不修改
Prompt 就能改变机器人安全边界。

### 3. 为什么还保留 Fake NavigateToPose？

真实 Nav2 很难稳定制造“取消不确认”“第一次 Abort、第二次成功”等故障。Fake
Server 负责确定性故障注入，真实仿真负责证明系统接线，两者属于测试金字塔不同层。

### 4. `use_sim_time` 有什么作用？

它让 Nav2、TF、Runtime 辅助节点使用 Gazebo 发布的 `/clock`。如果部分节点使用系统
时间、部分使用仿真时间，TF 查询和消息时间戳可能被判定为过去或未来，从而导致导航
失败。

### 5. 为什么要等待 bt_navigator active，而不只检查 Action 名字？

Action 名字出现不代表 Nav2 lifecycle 节点已经激活。smoke 同时检查
`/navigate_to_pose`、`/execute_task` 和 `bt_navigator` active，减少“接口存在但系统
尚未就绪”的假阳性。真实实跑还发现了一个细节：宽松匹配 `active` 会误接受
`inactive [2]`。现在脚本只接受行首的完整 `active` 状态，并用回归测试锁定。

### 6. keepout zone 已经生效了吗？

没有。当前配置明确写 `enforced: false`，RViz 只做可视化。真正生效还需要 mask、
Costmap Filter Info Server、keepout layer 参数和拒绝穿越的系统测试。技术复习时不能把
一条红色 Marker 说成安全约束。

### 7. 为什么不把 TurtleBot3 源码直接放入仓库？

项目依赖的是稳定公开接口和发行版二进制包。通过 rosdep 声明依赖更容易升级、检查
许可证和复现；复制源码会放大仓库、混淆自己的工作量，并承担不必要的上游维护。

### 8. 实际系统证据是什么？

2026-07-17 在本机 ROS 2 Jazzy 中，`bt_navigator` 进入 active，外层 Runtime
依次完成 `home -> dock` 和 `dock -> workbench`。两次都返回
`final_state: 5`、`error_code: 0`、`attempts: 1` 和 Action `SUCCEEDED`。
全量五包测试为 76 tests、0 errors、0 failures。

这些证据证明地图、AMCL、TF、Nav2 lifecycle、规划、控制、Gazebo 和
Runtime Action 链路能共同工作，但不证明真实硬件安全、真实模型准确率或
keepout 已强制生效。
