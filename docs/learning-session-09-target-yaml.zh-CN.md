# 第九课：命名目标 YAML 与 Pose 四元数

## 本课目标

这一课把 executor 中的硬编码坐标迁移到版本控制配置：

- 模型仍然只能提交 `dock`、`workbench`、`home` 等名字。
- Runtime 从 YAML 读取真实 x、y、yaw。
- 配置错误时节点启动失败，不带着未知坐标运行。
- yaw 在发送 Nav2 Goal 前转换为四元数。

## 唯一配置来源

源文件：

    simulation/config/targets.yaml

构建时它被安装为：

    share/task_executor/config/targets.yaml

executor 运行时只读取安装后的文件，因此 source、build、install 三个空间遵循标准
ROS 2 包布局。`--symlink-install` 下安装文件是指向源码的符号链接，修改配置后仍
建议重新运行测试再启动节点。

## 为什么模型不能提交 Pose

模型请求：

    {"action":"navigate", "target":"dock"}

Runtime 配置：

    dock:
      frame_id: map
      x: 0.0
      y: 0.0
      yaw: 0.0

这样模型只选择意图，Runtime 才拥有地图坐标。地图更新时只审查配置，不需要修改
prompt，也不会让模型把旧地图坐标带进新环境。

## 失败关闭规则

TargetMap 加载器要求：

1. `targets` 必须是非空 map。
2. 三个契约目标必须全部存在。
3. 不允许 `laboratory` 等契约外目标。
4. 每个 Pose 必须且只能有 `frame_id`、`x`、`y`、`yaw`。
5. `frame_id` 必须为 `map`。
6. x、y、yaw 必须是有限数，拒绝 NaN 和 Inf。
7. yaw 必须规范到 `[-π, π]`。

这些规则在节点构造阶段执行。加载失败会抛出异常并阻止 executor 启动，避免使用
默认坐标掩盖部署错误。

## Yaw 为什么要变成四元数

YAML 使用 yaw，便于人审查；ROS `Pose` 使用 quaternion，避免欧拉角表示问题。
平面机器人只绕 z 轴旋转，因此：

    q.z = sin(yaw / 2)
    q.w = cos(yaw / 2)
    q.x = 0
    q.y = 0

例如 `home.yaw=1.57`，大约表示机器人朝向 90 度。

技术复习标准答案：

> 配置层用 yaw 保持可读性，ROS 消息层转换成规范四元数。模型既不能提交角度，
> 也不能提交 quaternion，所有姿态都来自受审查配置。

## 测试矩阵

TargetMap 当前有 7 个 C++ 单元测试：

| 场景 | 预期 |
| --- | --- |
| 完整 dock/workbench/home | 加载成功 |
| 缺 workbench | 启动失败 |
| 增加 laboratory | 启动失败 |
| x 为 NaN | 启动失败 |
| 增加 velocity 字段 | 启动失败 |
| frame_id 为 odom | 启动失败 |
| yaw 为 4.0 | 启动失败 |

完整证据：

    Summary: 4 packages finished
    Summary: 40 tests, 0 errors, 0 failures, 0 skipped

## 一次取消测试竞态

加入 TargetMap 测试后，进程启动时序发生轻微变化，原取消测试偶尔得到
`attempts=0`，而不是 1。原因是测试在外层 Goal accepted 后立刻取消，此时内层
Nav2 Goal 可能还没创建。

运行时两个结果都合理：

- 内层尚未创建：取消外层，attempts 0。
- 内层已经运行：传播取消，attempts 1。

但测试名叫“取消传播到导航”，所以前置条件必须保证导航已经开始。修复方式是先
等待第一条 RUNNING feedback，再发送 cancel。这样测试验证的场景和断言一致。

技术复习表达：

> 并发测试必须建立明确的 happens-before 条件，不能依赖机器速度。先观察 feedback
> 再 cancel，才能证明测试覆盖的是内层取消传播，而不是取消发生在 Goal 创建前。

## 当前仿真依赖状态

本机目前未安装完整 Nav2、TurtleBot3 Gazebo 和 navigation2。进入下一课前执行：

    sudo apt install -y \
      ros-jazzy-navigation2 \
      ros-jazzy-nav2-bringup \
      ros-jazzy-turtlebot3-simulations \
      ros-jazzy-turtlebot3-navigation2

这些包在当前 apt 索引中都有 Jazzy 候选版本。安装前无需修改项目代码。

## 技术复习问题与答案

### 问：为什么 Schema 有目标枚举，YAML 还要再次检查？

答：Schema 约束模型输入，TargetMap 约束部署配置。配置也可能被人工写错，因此必须
确认它完整覆盖契约且没有扩大能力，两层分别保护不同信任边界。

### 问：为什么不在缺少目标时使用 `(0,0)` 默认值？

答：默认坐标可能让机器人驶向错误位置。目标配置属于安全关键数据，缺失必须让
节点启动失败，让部署问题在运动前暴露。

### 问：为什么只允许 map frame？

答：当前命名目标是全局静态位置，必须与 Nav2 全局地图一致。混入 odom 或 base_link
会改变语义并随机器人运动，应该直接拒绝。

### 问：这些坐标现在是真实可用的吗？

答：加载机制和安全校验已完成，但当前数值仍是 placeholder。必须先选择 TurtleBot3
世界和地图，再在 RViz 中检查并更新坐标，不能把 placeholder 说成真实标定结果。

## 本课完成状态

- [x] 唯一版本控制目标 YAML。
- [x] executor 从安装空间加载配置。
- [x] yaw 转 Pose quaternion。
- [x] 7 个配置加载/拒绝测试。
- [x] 取消测试建立明确 feedback 前置条件。
- [ ] 安装 Nav2/TurtleBot3 仿真依赖。
- [ ] 在实际地图上标定三个目标。
