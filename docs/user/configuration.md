# 常见配置

本页给出“常用配置点”的导航，避免读者在大量 API 中迷路；更细的行为契约以代码与注释为准（Ground Truth）。

## 1. 请求级配置（推荐从 QCRequest 开始）

`QCRequest` 提供链式 API，覆盖常用请求参数配置（详见 `src/QCRequest.h`）：

- Header：`withHeader(...)`
- 超时：`withTimeout(...)`
- 代理：`withProxyConfig(...)`
- TLS/SSL：`withSslConfig(...)`
- HTTP 版本：`withHttpVersion(...)`
- 优先级：`withPriority(...)`
- 重试策略：`withRetryPolicy(...)`
- 认证：`withBasicAuth(...)` / `withHttpAuth(...)`
- 上传：`withUploadDevice(...)` / `withUploadFile(...)`

### 优先级（调度器）

`withPriority(...)` 会设置 `QCNetworkRequestPriority`，用于调度器出队顺序。当前调度契约为**非抢占式**（non-preemptive）：已 Running 的请求不会因更高优先级到来而被中断。

说明（Critical）：

- `Critical` 会绕过 pending 队列立即启动；当前实现可能突破 `maxConcurrentRequests/maxRequestsPerHost`，建议仅在明确需要时使用。
- 通常优先使用 `High/VeryHigh`：会优先出队且仍遵守并发/每主机限制。

## 2. 管理器级配置（统一策略与复用）

当你需要在全局层面统一策略（如日志、中间件、连接池/调度器等），建议通过 `QCNetworkAccessManager` 管理与注入（入口定义见 `src/QCNetworkAccessManager.h`）。

补充说明（优先级调度契约）：

- 调度器为**非抢占式**（non-preemptive）：优先级只影响 pending 出队顺序；已 Running 的请求不会因更高优先级到来而被中断。
- `Critical` 会绕过 pending 队列立即启动，但同样不会抢占 Running；当前实现可能突破并发/每主机限制，建议仅在明确需要时使用。
- 详细定义以 `src/QCNetworkRequestScheduler.h` 与 `src/QCNetworkRequestPriority.h` 的注释为准。

## 3. WebSocket 配置

WebSocket 能力入口为 `QCWebSocket`，常见配置包括重连策略、压缩配置等；可参考：

- 示例：`examples/WebSocketDemo/`、`examples/WebSocketCompressionDemo/`、`examples/WebSocketPoolDemo/`

## 4. 排障与常见问题

建议优先：

1. 开启/查看日志（如你已启用日志或中间件）
2. 对照 `docs/reference/performance.md` 与相关模块文档
3. 复现最小样例后再上报问题（见 `SUPPORT.md`）
