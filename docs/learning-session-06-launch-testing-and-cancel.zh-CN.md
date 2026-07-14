# 第六课：Launch Testing 与安全取消确认

## 本课目标

这一课把手工 smoke 证据升级为 ROS 2 自动化集成测试，并解决一个安全语义问题：

- 测试框架自动启动和停止两个 ROS 节点。
- Python ActionClient 自动发送 Goal、收集 feedback、检查 result。
- cancel 不只检查“请求被接受”，还等待内层 Goal 真正进入 `CANCELED`。
- xUnit 必须明确记录测试用例数量，避免“命令成功但一个测试也没执行”。

## 测试文件的三部分

文件位于：

    task_executor/test/test_execute_task_launch.py

### 1. generate_test_description

它声明测试期间需要启动：

    fake_navigate_to_pose_server
    execute_task_server

`ReadyToTest` 表示进程已经交给 launch 管理，测试代码可以开始等待 Action Server。

### 2. 运行期测试

`TestExecuteTaskLifecycle` 使用真实 `rclpy.action.ActionClient`，当前覆盖：

| 用例 | 关键断言 |
| --- | --- |
| success | 状态 SUCCEEDED，feedback 为 3.0、2.0、1.0 |
| unknown target | 状态 ABORTED，error 13，attempts 0 |
| cancel | 内层取消确认后，外层状态 CANCELED |
| deadline | 一秒到期后，error 32，attempts 1 |

### 3. 进程退出测试

`TestProcessExit` 在 launch 关闭节点后检查退出码。它能发现节点崩溃、异常退出，
或测试结束后进程无法正确清理的问题。

## 一次“假绿灯”为什么重要

第一次实现使用了 pytest 风格的普通类。`colcon test` 当时显示没有失败，但 xUnit
文件写的是：

    tests="0" failures="0" errors="0"

原因是 `launch_testing.launch_test` 在这里使用 unittest 收集规则；测试类必须
继承 `unittest.TestCase`，生命周期函数也必须使用 `setUp`、`tearDown` 等名字。

修正后 xUnit 明确记录：

    tests="5" failures="0" errors="0"

技术复习标准答案：

> 测试命令返回 0 不等于业务测试已经执行。我会检查测试报告中的 collected
> count、具体 testcase 名称和失败数，防止 zero-test pass 造成假绿灯。

## Cancel 的两个确认层次

取消内部导航时有两个不同事件：

1. cancel service response：内层服务器愿意处理取消。
2. Action terminal result：内层 Goal 已经进入 `CANCELED`。

只收到第一个响应时，执行线程可能还没停。因此当前 executor 使用同一个 500 ms
总窗口：

    outer cancel requested
      -> send inner cancel
      -> inner accepts cancel
      -> wait inner result == CANCELED
      -> outer result = CANCELLED

任一步拒绝、超时，或内层以其他状态结束，外层都返回 `kCancelUnconfirmed`。
使用一个共享 deadline，而不是连续等待两个 500 ms，可以保证整个确认过程最多
占用 500 ms。

## 为什么测试取消比测试成功更重要

成功路径主要证明协议连接正确。取消路径同时涉及：

- 外层 Goal Handle。
- 内层 Goal Handle。
- cancel service future。
- 内层 result future。
- 两个线程和一个总时间上限。

这些对象的顺序错误，可能让上层看到 `CANCELLED`，但机器人仍在执行导航。
因此取消结果必须由内层终态支撑，不能由上层猜测。

## 如何复现

从项目根目录执行：

    bash scripts/build_phase_2.sh

当前证据：

    Summary: 4 packages finished
    Summary: 32 tests, 0 errors, 0 failures, 0 skipped

只运行 executor 测试：

    cd ~/embodied_ws
    source /opt/ros/jazzy/setup.bash
    source install/setup.bash
    colcon test --packages-select task_executor
    colcon test-result --verbose

## 技术复习问题与答案

### 问：为什么有 smoke test 还需要 launch_testing？

答：smoke test 适合人快速演示完整链路；launch test 能自动管理进程，并对每个
Goal、feedback、result、cancel 和退出码做结构化断言。两者分别服务于演示和回归。

### 问：为什么不能收到 cancel response 就返回 CANCELLED？

答：cancel response 只说明服务端接受请求，不说明执行线程已经结束。必须等待
内层 Action result 进入 `CANCELED`，否则外层状态可能和真实运动状态不一致。

### 问：为什么取消的两个等待共用一个 deadline？

答：如果每个阶段都重新获得 500 ms，总取消时间会被悄悄放大。共享绝对 deadline
使整个操作的最坏时间可证明、可测试。

### 问：32 tests 是否全部是 launch test？

答：不是。它是 `colcon` 对 Guard、Gateway 和 executor 测试结果的总汇总。
executor 的两个 xUnit 文件明确包含 9 个 launch 用例，其余主要是已有单元测试和测试套件
汇总项。

## 本课完成状态

- [x] success、feedback、unknown target 自动测试。
- [x] cancel 传播并等待内层 `CANCELED`。
- [x] 测试进程退出码检查。
- [x] xUnit 明确收集 5 个用例。
- [x] global deadline 自动取消。
- [x] 导航失败后的有限 retry/recovery。
