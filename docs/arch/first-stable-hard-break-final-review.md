# first Stable release hard-break 最终结论

## 结论

QCurl 当前不建议直接发布 first Stable release。

项目已经具备 hard-break 清理基础，但不应把当前 API / ABI 和实现结构直接冻结为 Stable。发布前应先完成一轮“稳定发布面重置 + 核心实现拆分 + 历史兼容叙事清理”。

## 判断边界

本结论基于以下前提：

- 允许 hard-break。
- 不需要保持向前兼容。
- 不需要保持向后兼容。
- 允许破坏 API / ABI 兼容。
- 判断必须符合 Qt6 网络编程、libcurl 的 Qt6 binding、Qt6 / KDE 应用库开发实践。

## 必须保留的有效结论

### 1. 实现巨石必须拆分

以下实现文件已经明显超过项目强制拆分阈值：

- `src/QCNetworkReply.cpp`
- `src/QCNetworkRequestScheduler.cpp`
- `src/QCCurlMultiManager.cpp`

这不是单纯风格问题。它会直接影响 first Stable 之后的维护、回归定位和 API 行为稳定性。

建议拆分方向：

- curl option adapter
- request pipeline
- callback / backpressure
- retry / cache
- multi event driver
- share / cookie 管理

### 2. 稳定发布面必须重置

当前默认安装面包含 Core、Extras 候选能力、Test Support、调度、缓存、mock、middleware 等多类能力。

first Stable 前必须重新定义：

- Core：生产运行时稳定 API。
- Extras：WebSocket、diagnostics、可选高级能力。
- Test Support：mock、capture、测试辅助能力。
- Internal：不承诺给下游使用的实现细节。

### 3. `QCNetworkAccessManager` 表面过宽

`QCNetworkAccessManager` 同时暴露发送、同步请求、scheduler、cache、logger、middleware、mock、cookie / share / HSTS / Alt-Svc 等能力。

结论不是“只保留请求创建”，而是要按稳定发布面重新分层，避免 first Stable 把过宽 manager API 锁死。

### 4. public header / install surface 必须收缩

public header 与 install surface 也需要作为 first Stable 发布边界的一部分处理，不能只拆 `.cpp` 实现文件。

以下头文件应进入收缩清单：

- `src/QCNetworkRequest.h`
- `src/QCNetworkRequestScheduler.h`
- `src/QCNetworkReply.h`

收缩目标不是按行数机械删除 API，而是避免默认安装面继续固化过宽的稳定合同。应优先拆分配置族、调度策略、测试入口和高层便利能力，把只服务测试、诊断、迁移或可选高级场景的内容移出 Core install surface。

该项应与 Core / Extras / Test Support / Internal 边界重置同步执行，否则后续只拆实现文件，仍会把过重 public header 冻结为 first Stable API。

### 5. 同步阻塞 API 应移出 Core

`getSync()`、`postSync()`、`putSync()` 与公开 `BlockingQueuedConnection` 相关路径不应作为 Qt6 / KDE 应用库推荐主路径。

在允许 hard-break 的前提下，first Stable 不应选择“保留但用文档禁止 UI 热路径、持锁路径、析构路径使用”。该做法仍会把同步阻塞能力留在 Core 稳定发布面，用户会把它理解为一等稳定 API，后续再移除会形成更高成本。

first Stable 应选择以下收敛方案：

- **必选：Core Async**。Core 只承诺异步网络 API，以 `QCNetworkAccessManager::send*()`、`QCNetworkReply`、取消、进度、错误和完成信号作为主路径。
- **必选：Blocking Extras**。将同步阻塞能力移入可选 Extras / blocking utility，服务 CLI、测试、worker thread、迁移脚本等场景。它应返回 value result，不应返回 `QCNetworkReply *`，也不应进入默认 Core install surface。raw `QIODevice *` 同步上传可作为 first Stable 稳定 API，但必须只存在于 Blocking Extras。
- **可选：Qt Async Operation Extras**。仅在项目确实需要比 `QCNetworkReply` 更高层的一次性任务封装时再实现。该层应只依赖 Qt，不引入 KDE 依赖；如果只是转发 `QCNetworkReply` 的信号、进度和取消，则属于重复封装，不应作为 first Stable 阻塞项。

`BlockingQueuedConnection` 相关公开路径也应同步收敛：Core 不应提供透明跨线程阻塞调用。需要跨线程使用时，应优先提供显式异步 marshal、owner-thread only 约束或内部队列化；确需阻塞的场景应留在 Extras / internal，不扩散为 Core public contract。

Blocking Extras 应采用 snapshot-first 设计：

- 使用独立 install component 和独立头文件，不挂在 `QCNetworkAccessManager` Core 表面。
- API 返回不可依赖 QObject 生命周期的 value result，内部可复用 curl option adapter、capability policy、cookie / proxy / TLS / cache 映射逻辑，避免重复实现。
- 请求执行依赖不可变配置快照，包括 proxy、cookie、TLS、cache policy、share policy、capability policy 等；不得跨线程借用 live manager 可变状态。
- main / application thread 默认 fail-fast；CLI、测试或 worker thread 场景需要显式 opt-in。
- raw `QIODevice *` 同步上传稳定 API 仅允许同线程读取，调用期间 device 生命周期必须由调用方保证；device 必须可读，且应要求可 seek 或显式 size。跨线程借用、未知长度隐式 rewind、边读边改等行为不进入稳定合同。
- `schedulerOnOwnerThread()` 这类 owner-thread 阻塞 getter 不应进入 Core 稳定 API，应移动到 diagnostics / internal，或改成显式异步查询。
- Core cookie store 是 `QCNetworkAccessManager` 的 owner-thread live state；`importCookies()`、`exportCookies()`、`clearAllCookies()` 若保留在 Core，只允许 owner thread 调用，非 owner thread 应 fail-fast，不得自动 `BlockingQueuedConnection` marshal。
- 跨线程 cookie 操作必须走显式 async bridge；API 名称应表达 async 语义，调用立即返回，真正读写在 owner thread 执行，结果通过 signal、callback 或 `QFuture` 返回。
- Blocking Extras 不访问 live manager cookie store，不接收 `QCNetworkAccessManager *`，也不调用 Core cookie import / export；它只接收 `CookieSnapshot`，并产出新的 `CookieSnapshot` 或 `CookieDelta`，由调用方在 owner thread 或 async bridge 中显式 merge。
- `applyPauseMask()` 这类 libcurl pause / multi 内部操作应保持 internal，并优先通过 multi owner-thread 队列化串行执行，不扩散成 public blocking contract。

该选择更符合 SOLID、KISS、DRY：

- Core 只负责异步传输，Blocking Extras 只负责阻塞工具，职责分离。
- first Stable 不一次性实现 KDE job adapter、Qt operation wrapper、blocking client 多套抽象，避免过度设计。
- Blocking Extras 与 Core 复用 internal curl option adapter 和 capability policy，不复制 libcurl binding 规则。
- 只有当高层 operation 能收敛重复的 body / headers / status 聚合、统一错误、取消、超时或 `QFuture` 转换时，才值得作为 Extras。

### 6. Mock / Test Support 需要重新决策

`QCNetworkMockHandler` 当前不是无依据污染。项目文档已明确把它定义为 Core Test Support。

但面向 first Stable，仍建议重新决策：生产 Core 与 Test Support 最好分离，避免下游把 mock / capture 误认为生产运行时能力。

### 7. `TestOnlyKey` 不应被误判为稳定 API 泄漏

`QCNetworkReply::TestOnlyKey` 受 `QCURL_ENABLE_TEST_HOOKS` 控制，且当前由 `BUILD_TESTING` 的 private 编译定义启用。

正确结论是：继续确保 test hooks 不进入 install / export consumer contract，而不是把它定性为已经泄漏。

### 8. 历史兼容层清理必须分辨对象

可以清理：

- qmake 遗留说明
- v2.x 演进注释
- legacy acceptance 叙事
- 旧迁移措辞
- 过时文档

不能误删：

- libcurl capability adaptation
- share-handle locking
- 协议能力探测
- 安全 `Fail` / `Warn` 策略
- 运行时 capability warning

这些属于 Qt6 / libcurl binding 必需适配，不是历史兼容包袱。

## 发布前建议顺序

1. 重置发布边界。
2. 收缩 public header / install surface。
3. 拆分三处实现巨石。
4. 从 Core 移除同步阻塞 API，并建立 Blocking Extras 边界。
5. 收敛公开 `BlockingQueuedConnection` 路径。
6. 清理历史兼容叙事。
7. 重跑稳定门禁。

## 发布门禁

first Stable 前至少应通过：

- public-api gate
- Qt Test strict gate
- libcurl consistency gate
- deprecated curl API guard
- install / export consumer smoke

## 最终一句话

QCurl 的 first Stable release 不应基于当前表面直接冻结；应先执行 hard-break 的稳定面重置和核心实现拆分，把生产 Core、Blocking Extras、其他 Extras、Test Support、Internal 边界重新收紧后，再进入 Stable 发布。
