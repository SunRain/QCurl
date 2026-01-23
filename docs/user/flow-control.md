# FlowControl（传输级 pause/resume + backpressure + 上传 source pause）

> 本页面向 **QCurl 使用者（下游项目）**，总结“方案2：新增 v2 API + 保留旧 API 并标记弃用一段版本”的对外 API 变更与迁移要点。  
> **Ground Truth：** 如本文与代码不一致，以 `src/QCNetworkReply.h` / `src/QCNetworkRequest.h` 为准。

## 1. 速查：旧 API → v2 API（推荐）

### 1.1 `QCNetworkReply`：传输级 pause/resume（仅 Async）

| 旧 API（保留但已弃用） | v2 API（推荐） | 备注 |
| --- | --- | --- |
| `pause()` / `pause(PauseMode)` | `pauseTransport(PauseMode mode = PauseMode::All)` | 传输级暂停；仅 `Running → Paused` 生效，幂等 |
| `resume()` | `resumeTransport()` | 传输级恢复；仅 `Paused → Running` 生效，幂等 |

弃用窗口：旧 API 带 `[[deprecated]]`，**计划在 v3.0 移除**。

### 1.2 `QCNetworkRequest`：下载 backpressure 配置命名统一

| 旧 API（保留但已弃用） | v2 API（推荐） | 备注 |
| --- | --- | --- |
| `setAsyncBodyBufferLimitBytes(qint64)` / `asyncBodyBufferLimitBytes()` | `setBackpressureLimitBytes(qint64)` / `backpressureLimitBytes()` | `bytes > 0` 启用；`bytes <= 0` 禁用 |
| `setAsyncBodyBufferResumeBytes(qint64)` / `asyncBodyBufferResumeBytes()` | `setBackpressureResumeBytes(qint64)` / `backpressureResumeBytes()` | `0` 表示使用默认值（`limit/2`）；`resume >= limit` 会回退默认 |

弃用窗口：旧 API 带 `[[deprecated]]`，**计划在 v3.0 移除**。

## 2. 核心语义（避免误用）

### 2.1 `ReplyState::Paused` 仅表达“用户显式传输级 pause”

- `QCNetworkReply::pauseTransport(...)` 进入 `ReplyState::Paused`。
- 下载 backpressure 与上传 source not-ready 造成的暂停属于 **内部流控**：
  - **不改变** `ReplyState`（避免与用户 pause 语义混淆）
  - 通过只读诊断面（getter/signal）提供可观测性（见 2.3、2.4）

### 2.2 `pauseTransport/resumeTransport` 的边界

- **仅 `ExecutionMode::Async` 生效**；Sync 模式调用为 `qWarning()` + no-op。
- 跨线程调用会自动 marshal 到 reply 线程（异步）。
- 仅状态机允许的转换生效：
  - `Running → Paused`（pause）
  - `Paused → Running`（resume）
  - 其他状态/重复调用为幂等 no-op
- `resumeTransport()` 成功后会触发一次 multi wakeup，避免“恢复后不推进”的边缘态。

### 2.3 backpressure 是 soft limit（高水位线），允许有界超限

- `backpressureLimitBytes` 表达 **高水位线（soft limit）**，不是 hard cap。
- 由于 libcurl write callback 无法部分消费，`bytesAvailable()` 可能 **短暂超过** `limitBytes`（通常最多一个 callback chunk）。
- 建议 `limitBytes` 不要过小（例如不小于 `16KiB`），否则会频繁 pause/resume，影响吞吐并增加调度开销。

### 2.4 上传发送方向 internal pause 的最小可观测面

流式上传（如 `QCNetworkRequest::setUploadDevice(...)`）在数据源 `QIODevice` 暂无数据但未 EOF 时，QCurl 可能对发送方向进行内部 pause。

推荐使用以下只读诊断面做排障与一致性验证：

- `QCNetworkReply::isUploadSendPaused() const noexcept`
- `QCNetworkReply::uploadSendPausedChanged(bool paused)`

说明：该状态仅表达内部流控，不包含用户显式 pause，也不改变 `ReplyState`。

## 3. 迁移示例（最小替换）

### 3.1 传输级暂停/恢复（替换旧 pause/resume）

```cpp
// 旧：
// reply->pause(QCurl::PauseMode::Recv);
// reply->resume();

// 新（推荐）：
reply->pauseTransport(QCurl::PauseMode::Recv);
reply->resumeTransport();
```

### 3.2 启用下载 backpressure（替换 asyncBodyBuffer*）

```cpp
// 旧：
// request.setAsyncBodyBufferLimitBytes(64 * 1024)
//        .setAsyncBodyBufferResumeBytes(32 * 1024);

// 新（推荐）：
request.setBackpressureLimitBytes(64 * 1024)
       .setBackpressureResumeBytes(32 * 1024);
```

### 3.3 观察内部流控状态（推荐用于排障）

```cpp
QObject::connect(reply, &QCurl::QCNetworkReply::backpressureStateChanged,
                 [](bool active, qint64 bufferedBytes, qint64 limitBytes) {
                     Q_UNUSED(bufferedBytes);
                     Q_UNUSED(limitBytes);
                     qDebug() << "backpressure active=" << active;
                 });

QObject::connect(reply, &QCurl::QCNetworkReply::uploadSendPausedChanged,
                 [](bool paused) { qDebug() << "upload send paused=" << paused; });
```

## 4. 相关文档

- 维护者设计/实现索引：`docs/arch/transport-pause-resume.md`
- 任务拆分与执行日志：`docs/TRANSPORT_PAUSE_RESUME_TASKS.md`
