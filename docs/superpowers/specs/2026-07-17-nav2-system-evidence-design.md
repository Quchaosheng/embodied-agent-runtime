# Nav2/TurtleBot3 真实系统证据设计

## 目标

在本机 ROS 2 Jazzy 环境中实际启动 Gazebo Sim、TurtleBot3、AMCL、
Navigation2 和项目自己的 `ExecuteTask` Runtime，用两次连续导航建立可重复的
系统证据。这项工作验证现有仿真集成，不新增一套导航框架。

## 成功标准

只有以下条件全部满足，才能在 README 中宣称真实 Nav2 系统仿真已验证：

1. `runtime_simulation` 在独立大盘工作区中构建成功。
2. 地图目标和 0.32 m 净空校验通过。
3. `/bt_navigator` 进入 `active`，`/navigate_to_pose` 和 `/execute_task`
   Action 都已可用。
4. 外层 `ExecuteTask` 依次完成 `home -> dock` 和 `dock -> workbench`，
   两次结果都是 Runtime `final_state: 5` 且 ROS Action `SUCCEEDED`。
5. 保存可审查的命令、结果摘要和失败时日志路径。

RViz 截图是 README 展示资料，不是 headless 系统验证的通过条件。

## 实施边界

- 使用独立本地分支 `feature/nav2-system-evidence`，不 push、不提前合并。
- 不操作分区，不删除旧 home，不删除 `work.old`。
- 不修改已经通过的 Fake `NavigateToPose` 测试边界。
- 不引入 Nav2 或 TurtleBot3 源码；只安装 Jazzy 二进制包并保留
  `rosdep`/`package.xml` 依赖声明。
- keepout 当前仍然只是 RViz 可视化，`enforced: false`；不宣称具备
  禁行区强制能力。
- 只修复真实启动和导航暴露的问题，不借机重构无关包。

## 磁盘与依赖策略

根分区当前只剩约 3.4 GiB，而 APT 预计安装后占用约 2.2 GiB。
`~/.cache/vscode-cpptools` 约 20 GiB，内容是可重建的 C/C++ IntelliSense
索引，不包含源码、Git 数据、VS Code 设置或扩展。

清理顺序为：

1. 记录当前空间和缓存大小。
2. 只终止正在占用缓存的 `cpptools` 和 `cpptools-srv` 子进程，不关闭
   VS Code 或 Codex。
3. 确认缓存没有打开文件后删除该目录；若进程自动重启并再次
   占用，则保留活动数据库，只删除经 `lsof`/`fuser` 确认未占用的旧索引和
   `ipch`，不强制删除活动文件。
4. 确认根分区空间已足够，再安装最小依赖集：
   `navigation2`、`nav2-bringup`、`turtlebot3-gazebo` 和
   `turtlebot3-navigation2`。
5. 安装后执行 `apt clean`，并再次记录空间。

VS Code 后续会自动重建需要的索引，首次打开 C++ 项目时可能暂时变慢。

## 工作区与执行流程

在剩余约 578 GiB 的大盘创建：

```text
/mnt/old-linux/current-data/sheng/embodied_ws_nav2
```

工作区的 `src/embodied-agent-runtime` 指向当前项目检出，所有 `build`、
`install` 和 `log` 都留在大盘。之后按以下顺序执行：

1. 运行环境检查和 `rosdep install`。
2. 构建 `runtime_simulation` 及其上游本地包，运行快速测试。
3. 运行 `scripts/smoke_nav2_sim.sh` 的 headless 系统验证。
4. 如果失败，从保留的 launch 日志定位最早的根因，做最小修复后重跑。
5. headless 通过后再进行 GUI/RViz 运行和截图，不让图形问题阻塞核心证据。

## 失败处理

- APT 下载失败：保留已安装包，清理 APT 缓存，报告具体失败源。
- 构建失败：先排查 Conda Python 污染和缺失的 ROS 依赖，不修改系统
  Python 包。
- Nav2 未 active：检查 Gazebo bridge、`/clock`、TF、AMCL 初始位姿和
  lifecycle 日志。
- 导航失败：保留 launch 日志，区分目标拒绝、规划失败、控制失败和
  Runtime deadline，不只增大超时掩盖问题。

## 验证与文档交付

实现完成时至少执行：

```bash
colcon test --packages-up-to runtime_simulation
colcon test-result --verbose
bash scripts/smoke_nav2_sim.sh
```

真实 smoke 通过后，同步更新 README、roadmap、release checklist、中文学习课和
技术复习问答，包含确切的验证日期、命令和结果。如果未通过，则 README 继续
保留“系统验证待执行”，不降低成功标准。

## 明确不做

- 本轮不实现 keepout costmap filter。
- 本轮不制作新 world、新机器人模型或自定义 planner/controller。
- 本轮不优化仿真速度或 CI 中的 Gazebo 执行。
- 本轮不 push、不 merge，最后统一 code review。
