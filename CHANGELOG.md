# QCurl Changelog

记录当前 `2.1.0` 候选的变化与历史版本说明。2.0 / 1.0 条目保留当时合同，不代表当前兼容政策；旧日期流水、RC 草稿和审查证据只从[维护者历史索引](docs/dev/archive/README.md)查阅。

<a id="current-release"></a>
## [2.1.0] - Unreleased

本条目以 `v2.0.0` 公共头、安装面与行为差异为依据。2.1.0 是当前开发候选，发布资格以[2.1 正式合同](docs/dev/release/2.1.0-release-contract.md)绑定的真实机器证据为准；本条目不表示 tag、Release 或资产已经发布。

### 版本与兼容政策

- 从 2.1.0 起，`major.minor` 是兼容版本线；同线后续 patch 保证全部已发布 Public 组件的源码、受支持 shared ABI、安装消费与公开行为向后兼容，包括 Preview。跨 minor 不保证兼容；这是自定义版本政策，不是 SemVer minor 兼容承诺。
- 两个运行库使用 `SOVERSION 2.1` / `VERSION 2.1.0`，包规则改为 `SameMinorVersion`。不提供历史 `.so.2` 兼容链接；从 2.0 升级须修改受影响源码并重建下游。
- 保留四个逻辑组件和三个物理库：Blocking 仍是实现内嵌 Core 的 INTERFACE target，Other Extras 独立，TestSupport 静态交付。首次 shared ABI 支持范围为 Linux x86_64 GCC/libstdc++ C++17、Release、未插桩、完整默认功能及匹配的依赖/ABI 配置，详见[长期政策](docs/dev/release/versioning-and-compatibility.md)。

### Breaking changes

- libcurl 最低版本从 `v2.0.0` 的 **7.85.0 提高至 8.14.1**，构建头、链接库与运行库统一执行；Qt 最低版本保持 6.10.3。运行库版本号不能替代 HTTP/3/QUIC 和 WebSocket 的实际能力检查。
- 删除 `ShareHandleConfig::shareDnsCache()` / `setShareDnsCache()`；显式 share 只控制 manager 的 Cookie/SSL session store。DNS/连接资源由 CURLM 传输域管理，不提供旧 share-DNS 兼容路径。
- 解析覆盖提升为普通 Core API，使用严格、原子校验的 `host:port:IPv4` 列表和 Qt ACE 主机规范化。曾使用内部高级路径开关的调用方须处理 `setResolveOverride()` 从链式 request 引用改为 `QCNetworkConfigUpdateResult`；无效列表保留旧状态。
- 新默认 `Isolated` 按完整有效解析覆盖隔离异步 DNS/连接资源；显式 `NativeShared` 才选择同线程原生共享域。空列表不清缓存、不重置策略，Blocking 不提供跨调用缓存。连接限额和缓存容量按每个 CURLM 应用，同线程合计可超过单域值，不能继续视作线程总配额。

### 新增能力

- RetryPolicy 增加累计计划等待与总耗时预算；非负可选值、默认不限，在决定下一次重试时检查，保留正在执行的传输和已有错误/输出。Reply 增加重试次数、累计计划等待，以及不依赖自动重试开启状态的 `retryAfterDelay()` 查询。
- 提供从当前工作区构建的 Arch Linux 完整开发包，覆盖四组件安装、最终归档检查与独立安装 consumer；本地产包不自动建立正式发布资格。

### 缺陷修复

- 原生终态保留 `CURLINFO_RESPONSE_CODE`，不把代理 CONNECT 的拒绝状态冒充目标响应；未收到目标响应时为 0。缓存命中正确恢复缓存元数据中的 HTTP 状态。
- 中间件注销立即使当前注册失效，已开始请求跳过尚未执行阶段；同地址重注册只参与新请求。处理回调中删除 manager、reply、job 的重入，以及下载设备写入中的取消/销毁，避免继续使用过期借用。
- WebSocket pool 在完成 future 前提交连接、容量和 lease 状态；清池先移除旧索引再通知，允许 owner-thread 同步 continuation 查询、再 acquire、clear 或销毁 pool，迟到通知不会复活旧 lease。
- 子项目构建的生成头与安装路径使用自身 build/source 边界；正式安装 consumer 与测试伴随库使用独立构建验证，避免把测试目标或顶层路径泄漏给下游。

### 发布证据

- 当前唯一 authority 切换到 2.1 合同。首版 full/final 使用 `snapshot` 捕获两库 ABI、完整安装头、tuple 和四组件预编译 consumer，独立 round-trip 结果为 `REFERENCE_VALIDATED` / `patchCompatibility=false`，不能标成真实 patch 升级 PASS。后续 patch 使用 `current` 比较本线首版与最近正式 patch，并分别验证旧源码、旧二进制和公开行为。ABI 完整/残余分析固定比较两侧真实 ELF，避免 XML snapshot 对纯类方法/虚表变化的漏报；snapshot 仍为必需归档与语义校验输入。
- 延续五树产品资格；TSan 在 2.1 为独立非阻断诊断，缺少执行写 NOT_RUN，ASan/UBSan/LSan 和普通线程/生命周期验证仍为必需。文档、工具或局部测试结果不替代 clean C1 的 fresh final、独立 verifier 或实际资产回验。

具体调整与调用方义务见[迁移到 2.1](docs/user/migration-2.1.md)。

<a id="2.0.0"></a>
## [2.0.0]

以下为保留的 2.0 历史说明；其中 2.x 源码、ABI 非稳定及当时 gate 模式不适用于 2.1 新线。

本条目是 QCurl 2.0.0 的冻结发布说明。发布资格、tag、远端 Release 和资产状态以[正式发布合同](docs/dev/release/2.0.0-hard-break-release-contract.md)及其机器证据为准；本条目本身不构成 readiness 或发布证明。

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
