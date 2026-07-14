# 第二课：YAML GuardPolicy 加载器

## 本课目标

本课把 task_guard/config/task_policy.yaml 从“文档中的配置”变成真正可加载、可验证的 GuardPolicy。

完成后的本地证据：

    12 tests, 0 errors, 0 failures, 0 skipped

## 为什么要从 YAML 加载策略

如果目标白名单、期限和恢复次数全部写死在 C++ 中，每次部署调整都需要改代码和重新编译。放进版本控制的 YAML 后，可以：

- 审查每次策略变化。
- 为不同现场使用不同的受控配置。
- 在启动阶段发现缺字段或错误类型。
- 用测试证明配置确实影响 Guard。

但配置化不等于放弃安全边界。YAML 只能收紧契约，不能扩大契约。

## 当前策略文件

task_guard/config/task_policy.yaml 包含：

    contract_version: 1
    deadline:
      min_s: 1
      max_s: 90
    allowed_targets:
      - dock
      - workbench
      - home
    recovery:
      max_navigation_attempts: 2
      cancel_confirmation_timeout_ms: 500

## 加载器接口

接口位于 task_guard/include/task_guard/task_guard.hpp：

    GuardPolicy load_policy_from_yaml(const std::string& path);

实现位于 task_guard/src/task_guard.cpp。

加载成功返回 GuardPolicy；配置错误抛出 std::runtime_error。

## 为什么采用失败关闭

失败关闭意味着配置不确定时不继续运行。

当前加载器会拒绝：

- 根节点不是 map。
- 必填字段缺失。
- 数值字段类型错误或超出 uint32 范围。
- contract_version 不是当前支持的版本 1。
- deadline 小于契约最小值或大于契约最大值。
- deadline 的 min_s 大于 max_s。
- allowed_targets 为空。
- 目标字符串为空或重复。
- YAML 试图加入 Schema 外的新目标。
- 恢复次数或取消确认时间为 0。

技术复习标准答案：

问题：为什么不在配置错误时回退到默认值？

答案：安全配置错误通常代表部署或审查流程出了问题。静默使用默认值会掩盖错误，现场行为也可能与运维人员预期不同。启动失败更容易发现，也符合失败关闭原则。

## 契约上限和部署策略

task_contract 现在提供：

- kContractVersion
- kMinDeadlineS
- kMaxDeadlineS
- kContractTargets

加载器用这些常量验证 YAML。

关系是：

    Contract 定义能力上限
    YAML Policy 定义当前部署子集

例如 YAML 可以只允许 home，但不能添加 laboratory，也不能把 deadline 扩大到 120 秒。

## 为什么恢复字段也进入 GuardPolicy

max_navigation_attempts 和 cancel_confirmation_timeout_ms 现在由执行器读取，分别限制导航尝试次数和取消确认时间；它们仍由同一个受审查策略文件加载和验证。

这样后续实现 task_executor 时，不需要重新设计策略来源。不过技术复习时必须说明：字段已经加载，恢复执行逻辑尚未实现。

## CMake 和依赖知识

项目直接依赖 yaml-cpp 0.8.0。

关键 CMake 步骤：

- find_package(yaml-cpp REQUIRED)
- 将 yaml-cpp::yaml-cpp 链接到 task_guard。
- package.xml 声明 yaml-cpp 依赖。
- 测试目标通过编译宏获得测试配置文件路径。

实现时遇到的 CMake 问题：

ament_target_dependencies 使用 plain target_link_libraries 签名。因此同一目标链接 yaml-cpp 时也必须使用 plain 签名，不能混用 PUBLIC 关键字版本。

技术复习标准答案：

问题：如何定位这个 CMake 错误？

答案：错误信息明确指出同一 target 混用了 plain 和 keyword signature，并给出第一次调用来自 ament_target_dependencies。将 yaml-cpp 链接改为相同的 plain signature 后解决。

## 新增测试

### LoadsVersionControlledPolicy

证明正式 task_policy.yaml 可以加载出：

- 版本 1。
- deadline 1 到 90。
- dock、workbench、home 三个目标。
- 最大导航尝试次数 2。
- 取消确认时间 5 秒。

### RejectsPolicyThatRelaxesContractDeadline

使用 max_s 为 91 的测试配置，证明部署策略不能突破契约的 90 秒上限。

### RejectsPolicyThatExpandsContractTargets

使用 laboratory 目标的测试配置，证明 YAML 不能绕过 Schema 扩大机器人能力。

## 当前实现边界

已经实现：

- YAML 文件读取。
- 类型转换。
- 必填字段检查。
- 安全范围验证。
- 目标子集验证。
- 恢复字段基本验证。
- 单元测试。

尚未实现：

- ROS 节点启动时自动定位配置文件。
- 使用 ament_index_cpp 查找安装后的 share 目录。
- 参数覆盖策略文件路径。
- 热更新配置。
- 执行器真正使用恢复字段。

当前阶段不做热更新。安全策略更适合在节点启动时加载并固定，避免任务执行过程中规则突然改变。

## 高频技术复习问题

问题：为什么 YAML 不能新增 Schema 外目标？

答案：C++ Guard 是最终安全权威，不能只依赖模型 Gateway 的 Schema 校验。ROS Action 客户端也可能绕过 Gateway 直接发送请求，因此 Guard 必须再次保证目标属于契约集合。

问题：配置文件是否应该支持热更新？

答案：核心安全策略暂时不支持热更新。运行中改变白名单、期限或恢复次数会让正在执行的任务难以解释。更安全的做法是在受控重启时加载新版本策略。

问题：为什么测试要使用真实正式配置文件？

答案：它能发现正式 YAML 与 C++ 字段不同步的问题。同时还需要专门的非法 fixture 验证失败关闭路径。

## 下一步

M1 还缺 Gateway 边界的 JSON 解析和 Schema 校验。下一课将研究如何把不可信 JSON 转换为 TaskRequest，并确保额外字段、错误类型和缺字段在进入 C++ Guard 前被拒绝。
