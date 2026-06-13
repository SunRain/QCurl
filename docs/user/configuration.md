# 常见配置

本页给出“常用配置点”的导航，避免读者在大量 API 中迷路；更细的行为契约以代码与注释为准（Ground Truth）。

## 1. 请求级配置（推荐从 `QCNetworkRequest` 开始）

`QCNetworkRequest` 是当前唯一的 public request 配置入口，覆盖常用请求参数配置（详见 `src/QCNetworkRequest.h`）：

- Header：`setRawHeader(...)`
- 超时：`setTimeout(...)` / `setTimeoutConfig(...)`
- 代理：`setProxyConfig(...)`
- TLS/SSL：`setSslConfig(...)`
- HTTP 版本：`setHttpVersion(...)`
- 优先级：`setPriority(...)`
- 重试策略：`setRetryPolicy(...)`
- 认证：`setHttpAuth(...)`
- 上传：
  - raw body：`QCNetworkAccessManager::post(..., QIODevice *, sizeBytes)` / `put(..., QIODevice *, sizeBytes)`
  - 单文件 multipart：`QCNetworkMultipartBody::fromSingleFileDevice(device, ...)` + `post(..., QIODevice *, sizeBytes)`

上传入口的关键合同：

- raw-body `QIODevice *` 是借用对象，调用方持有所有权，并负责保持到 reply 结束。
- device overload 必须从 manager 所在线程调用，且 `device->thread()` 必须与 manager/reply 线程一致；异步请求还需要该线程存在 Qt event loop。
- raw-body 从调用时的当前 `pos()` 开始读；需要重定向、重试或认证协商重发 body 时，设备必须能 seek 回起点。
- POST 未知长度只支持 HTTP/1.1 chunked raw-body；PUT 必须有已知长度或可从 seekable device 推导。
- async raw-body 可以通过 `readyRead()` 从 source-not-ready 恢复；sync raw-body 遇到 `read() == 0 && !atEnd()` 会失败。
- `QCNetworkMultipartBody::fromSingleFileDevice(device, ...)` 在构造阶段只校验 source device、长度和 seek 能力；返回空值表示构造失败并通过 `error` 给出原因。发送阶段仍要求 source device、wrapper device、manager/reply 在同一线程，并拒绝 unknown-size/sequential source。

### 值语义与 `operator==`

- `QCNetworkRequest` 是值语义配置对象，但 `operator==` 当前只比较 URL、follow redirect、raw headers、Range、HTTP version 与 `lane`。
- `sslConfig()` / `proxyConfig()` / `timeoutConfig()` / `retryPolicy()` / `httpAuth()` 以及 `priority`、cache 等执行配置族 **不参与** `operator==`。
- 如果你需要判断“完整执行配置是否一致”，请分别读取这些 config family，而不要把 `operator==` 当作全量 diff。

### 优先级（调度器）

`setPriority(...)` 会设置 `QCNetworkRequestPriority`，用于调度器出队顺序。当前调度契约为**非抢占式**（non-preemptive）：已 Running 的请求不会因更高优先级到来而被中断。

补充说明：

- 通常优先使用 `High/VeryHigh`，适合大多数“希望尽快处理”的前台请求。
- 如果你还需要理解 `lane`、`Critical`、lane reservation，或想按 `Control / Transfer / Background` 分车道配置，请统一参考 `docs/user/lane-scheduler.md`。

## 2. 管理器级配置（统一策略与复用）

当你需要在全局层面统一策略（如日志、中间件、连接池/调度器等），建议通过 `QCNetworkAccessManager` 管理与注入（入口定义见 `src/QCNetworkAccessManager.h`）。

补充说明（优先级调度契约）：

- 调度器为**非抢占式**（non-preemptive）：优先级只影响 pending 出队顺序；已 Running 的请求不会因更高优先级到来而被中断。
- 与 lane 相关的完整行为、推荐车道划分和配置建议，统一参考 `docs/user/lane-scheduler.md`。
- 更细的底层定义仍以 `src/QCNetworkRequestScheduler.h` 与 `src/QCNetworkRequestPriority.h` 的注释为准。

## 3. WebSocket 配置

WebSocket 的 public 配置入口集中在以下头文件注释：

- `src/QCWebSocket.h`：连接生命周期、`QCWebSocketOptions`、`close(...)` / `ping(...)` / `pong(...)`
- `src/QCWebSocketReconnectPolicy.h`：重连次数、指数退避参数、可重连 close code 集合
- `src/QCWebSocketCompressionConfig.h`：压缩开关、 window bits、context takeover、compression level

常见边界可先关注：

- `QCWebSocket` 构造时必须传入 `QCWebSocketOptions`；连接超时、TLS、重连、压缩和自动 Pong 都通过 options 配置
- `QCWebSocket::open()` 在 `Closed` 状态下允许再次调用；只有 `Connecting / Connected / Closing` 状态会拒绝并发 `open()`
- `close(reason)` 的 `reason` 最大 123 字节
- `ping(...)` / `pong(...)` 的 payload 最大 125 字节
- `setOptions(...)` 必须在下一次 `open()` 前调用；`Closed` 状态可为重连前重新配置，`Connecting / Connected / Closing` 阶段会拒绝修改并保留旧值
- `QCWebSocketCompressionConfig` 的 `clientMaxWindowBits` / `serverMaxWindowBits` 约束为 8-15

示例目录（`examples/WebSocketDemo/`、`examples/WebSocketCompressionDemo/`、`examples/WebSocketPoolDemo/`）只演示典型用法，不定义参数合同。

## 4. 排障与常见问题

建议优先：

1. 开启/查看日志（如你已启用日志或中间件）
2. 对照 `docs/reference/performance.md` 与相关模块文档
3. 复现最小样例后再上报问题（见 `SUPPORT.md`）
