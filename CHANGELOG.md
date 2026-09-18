# QCurl Changelog

记录当前 `2.0.0` hard-break 候选与已发布的 `1.0.0`。旧日期流水、RC 草稿和审查证据只从[维护者历史索引](docs/dev/archive/README.md)查阅。

## [Unreleased]

`v1.0.0` 已发布；`v2.0.0` 尚未创建 tag 或远端发布。以下是候选变更，不是 readiness 或 CI 通过证明；正式发布说明从本版本条目生成。

### Core 与包交付

- 当前版本为 `2.0.0`，shared 库使用 `SOVERSION 2` / `libQCurl.so.2.0.0`。Core 在 2.x 内保证源码兼容，但 ABI 非稳定，下游每次更新都必须重新编译和链接。
- 保留 Core、Blocking Extras、Other Extras、Test Support 四个逻辑消费面，物理产物收敛为 Core、Other Extras 与静态 Test Support 三个库。Blocking Extras 的实现和符号并入 Core，显式 `QCurl::BlockingExtras` 变为 INTERFACE target，只需额外安装 `BlockingExtrasDevelopment`。
- Other Extras 仍独立编译；Diagnostics、WebSocket 为 Preview，Middleware Extras 为 Stable。Test Support 仅供开发，以上均不自动成为默认 Core。
- shared/static 安装与隔离 consumer 证据改由 `BUILD_TESTING=OFF` producer 产生；static 与测试开启的组合在 configure 时以 `QCURL_STATIC_TESTING_UNSUPPORTED：静态构建不支持测试` 拒绝。

### Breaking changes

- 相对 v1 有意打破源码与 ABI，不提供 alias、wrapper、shim、兼容开关或迁移窗口。2.0 不生成稳定 ABI baseline，动态符号 allowlist 只防止实现符号泄漏。
- `QCNetworkRequest(const QUrl &)` 改为 explicit，依赖隐式转换的调用须显式构造请求。
- 缓存策略移除有损 QMap overload，改用有序 raw headers，保留重复字段；缓存读取改为包含 method、规范化 URL、Vary 请求头和认证分区的结构化 key，清理返回结构化结果。
- 未配置代理或显式选择 None 时禁止继承环境代理；需要代理的程序须显式设置配置。合法自定义 HTTP method token 按原字节发送，请求头按大小写无关字段名替换。
- 协议限制改用 `QCNetworkProtocols` flags；HTTP 头名和 content encoding 提供命名常量，仍允许自定义字符串。
- 可失败的重定向/传输配置、RetryPolicy、连接池配置、logger 与 WebSocket 命令返回显式接受结果；无效输入拒绝且不修改旧状态。RetryPolicy 使用 validated factory，自动重试默认关闭，非 GET/HEAD 需要显式幂等键策略。
- logger 注入改为 opaque `QCNetworkLoggerHandle`；manager 和已创建 reply 分别保留快照。文件配置/日志写入返回 `QCNetworkLogResult`，不得忽略部分失败。
- cookie 异步操作只通过每次调用的 QFuture 返回结果，不再并行发出结果信号。`QCCookie` 是 v1 已有的 Core 值模型，不是本次新增迁移。
- WebSocket pool 的 acquire/preWarm 使用逐调用 QFuture；租借以纯值 LeaseId 表达，owner thread 临时 resolve，归还返回 LeaseResult；clearPool/setConfig 返回可检查的 bool 和可选错误输出，失败不改变池状态。

### 调度与传输

- lane 配置、调度通知、同步启动前取消及 Pending 的 defer/undefer/优先级控制统一经 manager；命令返回 `SchedulerCommandResult`，拒绝无副作用，借用 reply 与值快照的生命周期明确区分。
- scheduler 实现私有化，旧公开实现类型/配置统计类型、quantum、可配置 default lane、UnknownLaneMode 和旧带宽开关退出；按单一启动权重轮转。
- admission core 统一管理队列，修正 busy lane 删除、policy 重入、重复取消计数与极端权重算术；保留启动票据和原 multi 生命周期。admissionByteBudget 只延迟新请求启动，不提供 manager 聚合限速。
- 修正流式消费后透明重试与下载输出回滚边界，续传成功要求 identity 表示与完整长度一致。手动 libcurl 关闭由应用级 QCurlRuntime 协调，不支持动态卸载。
- 补充 WebSocket pool 四类公共信号及 acquire/release/clearPool 的同步重入覆盖，以及匹配、延迟、错误、缺失 Pong 和连接断开的本地 keepalive 用例；测试存在不等于当前候选已执行通过。

### 发布与证据边界

- release manifest 绑定唯一正式发布合同，而不是可变任务进度或历史审查；使用五棵显式 producer 树、full/final 验收和 `abiMode=none`。本次 2.0.0 的 TSan 为独立非阻断诊断，不计入 final 必需证据；ASan/UBSan/LSan 及线程安全合同不放宽，未取得 TSan 证据不宣称通过。操作命令只在[发布流程](docs/dev/release/release-procedure.md)维护。
- tag、GitHub Release、assets/checksums 不由本条目或本地 PASS 自动创建。SBOM、签名与 provenance 尚不是已交付能力。
- 稳定 ABI 合同与首份 baseline 为[Deferred 项目](docs/dev/release/stable-abi-contract-and-baseline.md)，不属于 2.0 发布阻断项。

调用方逐项升级见[迁移指南](docs/user/migration-2.0.md)，精确组件范围见[正式发布合同](docs/dev/release/2.0.0-hard-break-release-contract.md)。

## [1.0.0] - 2026-06-24

v1.0.0 已发布，是历史 Core ABI 基线；完整发布记录从[历史索引](docs/dev/archive/README.md)查阅。后续 2.0 hard-break 不提供 v1 兼容层。
