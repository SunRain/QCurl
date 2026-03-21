# Flow Control（传输级 pause/resume + backpressure + 上传 source pause）

> 本页面向 **QCurl 使用者（下游项目）**，总结当前对外 API 与 flow-control 语义。  
> **Ground Truth：** 如本文与代码不一致，以 `src/QCNetworkReply.h` / `src/QCNetworkRequest.h` 为准。

## 1. 速查：当前 API

### 1.1 `QCNetworkReply`：传输级 pause/resume（仅 Async）

| 当前 API | 说明 | 备注 |
| --- | --- | --- |
| `pauseTransport(PauseMode mode = PauseMode::All)` | 传输级暂停 | 仅 `Running → Paused` 生效，幂等 |
| `resumeTransport()` | 传输级恢复 | 仅 `Paused → Running` 生效，幂等 |

### 1.2 `QCNetworkRequest`：下载 backpressure 配置命名统一

| 当前 API | 说明 | 备注 |
| --- | --- | --- |
| `setBackpressureLimitBytes(qint64)` / `backpressureLimitBytes()` | 高水位线 | `bytes > 0` 启用；`bytes <= 0` 禁用 |
| `setBackpressureResumeBytes(qint64)` / `backpressureResumeBytes()` | 低水位线 | `0` 表示使用默认值（`limit/2`）；`resume >= limit` 会回退默认 |

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

## 3. 使用示例

### 3.1 传输级暂停/恢复

```cpp
reply->pauseTransport(QCurl::PauseMode::Recv);
reply->resumeTransport();
```

### 3.2 启用下载 backpressure

```cpp
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
- 当前状态页：`docs/TRANSPORT_PAUSE_RESUME_TASKS.md`
