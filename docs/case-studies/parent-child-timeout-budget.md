# 父子层取消预算不能打平：一次 113 覆盖 204 的排障记录

## 现象

我在取消链路回归里遇到过一个很容易误判的结果：设备层明明已经进入 STOP 流程，最终对外暴露的却是父层 `SAFE_STOP/113`，而不是设备层用于表示 STOP ACK 超时的 `SAFE_STOP/204`。事故配置中，父层取消确认预算和设备层 STOP 等待预算都设成了 `500ms`。两个计时器几乎同时起跑，父层先到达终止条件，于是它用“取消未确认”结束任务；稍后子层形成的 `204` 已经没有机会成为最终 Action result。

这不是两个错误码谁更重要的问题，而是父层观察窗口没有覆盖子层完成安全收尾所需的完整窗口。对调用方而言，`113` 只能说明父层没有及时看到确认，不能回答 STOP 是否发出、设备是否回复、回复是否被协议接受。

## 排查证据

我先沿 Action 层级核对定义和终止分支：`ros2_ws/src/task_executor/src/task_executor_node.cpp` 定义 `kErrorCancelUnconfirmed = 113`，并在取消超时后生成父层 `SAFE_STOP`；同一文件的 `finish_from_child()` 只有在子结果已经到达时，才会把子层 `SAFE_STOP` 的 `error_code` 透传出去。`ros2_ws/src/device_bridge/include/device_bridge/command_policy.hpp` 则定义 `kErrorStopTimeout = 204`，`ros2_ws/src/device_bridge/src/device_bridge_node.cpp` 在 STOP 等待超时后返回该错误。

我使用下面的命令交叉核对当前默认值和测试断言：

```bash
rg -n "cancel_timeout_ms|kErrorCancelUnconfirmed" ros2_ws/src/task_executor
rg -n "stop_timeout_ms|kErrorStopTimeout" ros2_ws/src/device_bridge
rg -n "error_code, 113|SAFE_STOP 2 204|assert_stop_can" \
  ros2_ws/src/task_executor/test scripts/run_industrial_e2e.sh
```

当前仓库已经是修复后的配置：`ros2_ws/src/task_executor/config/targets.yaml` 和节点默认值均为 `cancel_timeout_ms: 1000`，`ros2_ws/src/device_bridge/config/device_bridge.yaml` 与节点默认值均为 `stop_timeout_ms: 500`。单元测试 `CancelTimeoutStillDrainsChildAndReportsUnconfirmedStop` 还证明了另一条重要约束：父层即使标记取消超时，也要继续 drain 子 Action，不能抢先发布终态。

## 根因/设计

根因是把父层预算误当成了子层预算的镜像。父层计时覆盖的不只是设备等待 STOP ACK 的 `500ms`，还包括取消请求调度、Action 状态传播、线程轮询和子结果回传。若两层都为 `500ms`，父层没有任何调度余量；边界抖动就足以让 `113` 先落地，覆盖本应更具体的 `204`。

我的设计原则是：上层确认预算必须严格大于下层安全动作预算。这里最终采用父 `cancel_timeout = 1000ms`、子 `stop_timeout = 500ms`。这不是承诺 STOP 一定在某个硬实时期限内完成，而是给软件链路留出先让设备层形成权威结果、再由父层归并结果的顺序关系。

我也同时核对了参数文件与节点内建默认值，避免只改启动配置却在另一种启动方式下退回旧预算。两处保持一致后，回归才真正覆盖部署行为，而不只是某一条测试命令。

## 修复与回归

修复后，我把验证拆成三条互相独立的证据链。第一条看 Action result：正常取消应得到 `CANCELED/0`；故意丢弃 STOP ACK 时，父任务应得到子层透传的 `SAFE_STOP/204`，而不是 `113`。第二条看时间戳：以取消请求、TaskEvent 的 `stamp`、观察器写入的 `observed_at_ns` 和 Action 终态排序，确认父层终态发生在子层结果形成之后。第三条看 CAN 帧：`scripts/run_industrial_e2e.sh` 用 `candump -L vcan0` 保存窗口，并由 `assert_stop_can` 检查恰有一条 opcode 为 `0xFF` 的 STOP；正常取消窗口存在对应 STOP 响应，`drop_stop_ack` 窗口则明确不存在该响应。

可复现入口和定向测试命令为：

```bash
colcon test --packages-select task_executor device_bridge runtime_can
./scripts/run_industrial_e2e.sh
cat /tmp/runtime-industrial-evidence/summary.tsv
```

回归判断不能只看一个错误码。我会同时保存 task JSON、TaskEvent、带时间戳的观察记录和 CAN 日志，使“父层何时结束、子层返回什么、线上实际出现了哪些协议帧”可以互相校验。

## 证据边界

事故中的旧值 `500ms/500ms` 及“113 先终止并覆盖 204”来自本次排障叙述；当前可见 Git 历史和代码能够直接证明的是修复后的 `1000ms/500ms`、错误码归属、drain 行为及 vcan E2E 断言。我没有把脚本中的等待上限当成设备实测响应时间，也不据此虚构时延分布。

最后，`vcan0`、ROS Action result、软件时间戳和 CAN 帧只能证明软件协议链路按预期工作。它们不等于真实控制器已切断动力，也不等于硬件急停达到安全等级；硬件急停仍需在实机上结合独立安全回路、控制器状态和物理效果验证。
