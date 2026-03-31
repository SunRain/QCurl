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
- 上传：`setUploadDevice(...)` / `setUploadFile(...)`

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

- `src/QCWebSocket.h`：连接生命周期、`close(...)` / `ping(...)` / `pong(...)` / `setAutoPongEnabled(...)` / SSL 配置
- `src/QCWebSocketReconnectPolicy.h`：重连次数、指数退避参数、可重连 close code 集合
- `src/QCWebSocketCompressionConfig.h`：压缩开关、 window bits、context takeover、compression level

常见边界可先关注：

- `close(reason)` 的 `reason` 最大 123 字节
- `ping(...)` / `pong(...)` 的 payload 最大 125 字节
- `setAutoPongEnabled(...)` 必须在 `open()` 前设置，连接建立后修改无效
- `QCWebSocketCompressionConfig` 的 `clientMaxWindowBits` / `serverMaxWindowBits` 约束为 8-15

示例目录（`examples/WebSocketDemo/`、`examples/WebSocketCompressionDemo/`、`examples/WebSocketPoolDemo/`）只演示典型用法，不定义参数合同。

## 4. 排障与常见问题

建议优先：

1. 开启/查看日志（如你已启用日志或中间件）
2. 对照 `docs/reference/performance.md` 与相关模块文档
3. 复现最小样例后再上报问题（见 `SUPPORT.md`）
