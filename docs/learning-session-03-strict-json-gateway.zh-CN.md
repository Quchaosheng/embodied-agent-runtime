# 第三课：严格 JSON Gateway

## 本课目标

本课实现从不可信模型文本到规范化任务对象的边界。

完成后的本地证据：

    3 packages finished
    21 tests, 0 errors, 0 failures, 0 skipped

## Gateway 的职责

agent_gateway 只做三件事：

1. 把文本解析为 JSON。
2. 使用 task_contract 的 Draft 7 Schema 校验。
3. 返回不可变的 NormalizedTaskRequest。

它不负责：

- 目标坐标。
- 速度或轨迹。
- 重试次数。
- 恢复行为树。
- Nav2 调用。
- 最终安全决策。

## 为什么 Gateway 被视为不可信

Gateway 接触模型输出。模型可能产生：

- 非法 JSON。
- 缺少字段。
- 额外字段。
- 错误类型。
- 未知目标。
- 超过期限。
- 重复字段。

因此 Gateway 是输入清洗边界，不是安全权威。最终权威仍是 C++ Task Guard。

## 错误分类

Gateway 使用与 C++ 契约一致的数字：

- 10：Invalid JSON。
- 11：Invalid Contract。

Invalid JSON 包括：

- 空字符串。
- JSON 语法错误。
- 重复字段。

Invalid Contract 包括：

- 缺少必填字段。
- 出现额外字段。
- 字段类型错误。
- 目标不在枚举中。
- deadline 超出 1 到 90。
- contract_version 不是 1。

## 为什么重复字段很危险

下面的文本包含两个 target：

    {
      "target": "dock",
      "target": "home"
    }

多数 Python JSON 解析器会静默保留后一个值。其他语言或安全设备可能采用不同规则，从而对同一请求产生不同理解。

实现使用 object_pairs_hook 保留字段顺序，并在发现重复 key 时立即返回 Invalid JSON。

技术复习标准答案：

问题：重复字段不是合法 JSON 吗？

答案：标准和解析器行为存在差异，安全边界不应该依赖“最后一个字段获胜”。明确拒绝可以避免组件间解释不一致。

## Schema 如何复用

Schema 的唯一来源是：

    task_contract/schema/task_request.schema.json

task_contract 在安装时把 schema 复制到自己的 share 目录。Gateway 使用 ament_index_python 查找安装后的 task_contract share 路径，而不是复制一份 Schema。

好处：

- 协议只有一个来源。
- C++ 包和 Python Gateway 不会维护两份枚举。
- 测试能发现 Schema 未安装或路径错误。

## NormalizedTaskRequest

规范化结果包含：

- contract_version
- action
- task_id
- target
- deadline_s

task_id 在 Schema 中可选。缺失时统一转换为空字符串，便于后续填写 ExecuteTask Goal。

数据类使用 frozen=True，避免验证后又被意外修改。

## 为什么 Gateway 和 Guard 都要校验

这是纵深防御。

Gateway 校验：

- 面向模型文本。
- 负责 JSON 语法和 Schema。
- 提供快速、清晰的输入错误。

Guard 校验：

- 面向 Runtime 内部任务。
- 负责部署策略和机器人实时状态。
- 防止其他 ROS 客户端绕过 Gateway。

如果只依赖 Gateway，任何直接调用 Action 的客户端都可能绕过模型边界。

## 新增测试场景

- 合法请求并补齐空 task_id。
- 保留合法 task_id。
- malformed JSON。
- 重复字段。
- 未知目标。
- deadline 91。
- deadline 字符串类型。
- 额外字段。
- 缺少 target。

参数化测试用于同一类 Schema 失败，减少重复代码。

## ROS 2 Python 包结构

agent_gateway 使用 ament_python：

- package.xml 声明运行和测试依赖。
- setup.py 安装 Python 模块与资源索引。
- setup.cfg 设置 ROS 可执行脚本目录。
- resource/agent_gateway 注册包。
- test/ 由 colcon test 调用 pytest。

Jazzy 自带的 ament_python 包不需要在 package.xml 中声明一个名为 ament_python 的 rosdep 包。最初添加该依赖时 rosdep 无法解析，对照系统 ros2node manifest 后移除。

## 当前实现边界

已经实现：

- 严格 JSON 解析。
- 重复字段检测。
- Draft 7 Schema 校验。
- 默认 Schema 安装与查找。
- 不可变规范化对象。
- 9 个 Python 测试场景。
- 第二阶段一键构建脚本。

尚未实现：

- 调用 ExecuteTask Action。
- 模型厂商 SDK。
- prompt 或 function calling。
- 网络服务。
- Action feedback 和 result 转发。

当前先使用固定 JSON fixture。这样能把协议问题与模型 API 问题分开。

## 高频技术复习问题

问题：为什么先做固定 JSON，不先接大模型？

答案：固定 fixture 可重复、无网络依赖，能先证明协议边界。模型 SDK 加入后，如果失败，可以明确判断是模型输出问题还是 Runtime 问题。

问题：为什么使用 JSON Schema 而不是手写一堆 if？

答案：Schema 是机器可读的协议文档，能够统一必填字段、类型、枚举、范围和额外字段规则，并被测试和多个语言复用。语义策略仍由 C++ Guard 负责。

问题：为什么 NormalizedTaskRequest 中 action 还是字符串？

答案：Gateway 保留模型契约表达。下一阶段构造 ROS Action Goal 时再把 navigate 映射为数值常量，映射位置单一且可测试。

## 下一步

M1 已完成。下一课进入 M2：

- ROS 2 Action 的 Goal、Feedback、Result 和 Cancel。
- ExecuteTask 外层 Action Server。
- 确定性的假 NavigateToPose Action Server。
- 成功、拒绝、反馈和取消测试。
