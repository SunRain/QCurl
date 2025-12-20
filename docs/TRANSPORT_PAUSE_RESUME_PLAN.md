# 基于当前架构（QCCurlMultiManager + libcurl multi/socket 驱动）实现“真正的传输级 pause/resume”初始计划

## 1. 背景与目标

QCurl 当前的异步实现以 `QCCurlMultiManager` 统一持有 `CURLM*`，通过 `CURLMOPT_SOCKETFUNCTION/CURLMOPT_TIMERFUNCTION` 与 Qt 事件循环集成，驱动 `curl_multi_socket_action()` 执行多请求。

本计划旨在评估并规划：在该架构下提供“真正的传输级 pause/resume API”的可行路径，并为后续补齐 `tests/libcurl_consistency/tasks.md` 中 **LC-15（P2）**（对齐 `cli_hx_download -P` 的过程一致性）提供前置设计与实现抓手。

## 2. 范围（Scope / Non-goals）

### 2.1 本期（建议优先落地）

- 将现有 `QCNetworkReply::pause()/resume()` 从“仅调用 libcurl API”提升为可观测、可控、可验证的传输级能力：
  - 明确状态机（至少提供可查询的 paused 语义）。
  - 明确线程/时序约束（多句柄线程、回调线程）。
  - 在异步 multi 驱动下保证 resume 不会“卡死在无事件可唤醒”的边缘态。

### 2.2 明确不在本期承诺的目标

- **上传方向**真正 pause/resume（需要 `CURLOPT_READFUNCTION`/`CURL_READFUNC_PAUSE`/seek 能力），当前 QCurl 上传主要走 `CURLOPT_POSTFIELDS`，改动面较大，建议单独立项。
- 完整对齐 `cli_hx_download -P` 的“过程一致性”（回调边界暂停点/恢复点/部分写入边界）。本期只提供必要前置；过程一致性作为 LC-15/P2 在 QCurl 提供能力后再落地。

## 3. 现状快照（基于当前代码）

### 3.1 传输级 pause/resume 的雏形已存在

- `QCNetworkReply` 已暴露 `pause()/resume()`（`src/QCNetworkReply.h`），并直接调用：
  - `curl_easy_pause(handle, CURLPAUSE_ALL)`（`src/QCNetworkReply.cpp`）
  - `curl_easy_pause(handle, CURLPAUSE_CONT)`（`src/QCNetworkReply.cpp`）

但当前实现存在明显缺口：无状态可观测、无错误处理、无线程/时序保证，且与调度器的“pause”语义不一致（见 3.2）。

### 3.2 调度器的 pause/resume 不是传输级 pause

- `QCNetworkRequestScheduler::pauseRequest()` 对运行中请求直接 `reply->cancel()`（`src/QCNetworkRequestScheduler.cpp`）。
- 该实现属于“队列/执行层面的停止与重排”，并非 libcurl 的传输级暂停；会破坏“过程一致性”的可复现性。

### 3.3 异步下载路径缺乏背压（Backpressure）

- 异步 write callback 始终把数据 append 到内存 buffer 并发射 `readyRead()`（`src/QCNetworkReply.cpp`）。
- `QCByteDataBuffer` 无上限（`src/qbytedata_p.h`），上层不消费时会导致内存无限增长；这会让 pause/resume 难以工程化为可靠控制面。

### 3.4 多句柄线程/生命周期约束需要显式化

- `QCCurlMultiManager::instance()` 使用静态局部变量单例（`src/QCCurlMultiManager.cpp`），首次调用线程不受控；同时类注释又要求“必须在主线程创建和使用”（`src/QCCurlMultiManager.h`）。
- 这对 pause/resume 的线程语义提出要求：API 必须明确“在哪个线程调用、回调在哪个线程执行”，并在运行时做防御（必要时 marshal 到管理器线程）。

## 4. 与 libcurl/libtest 的差异：为什么仅 `curl_easy_pause()` 不够

`curl/tests/libtest/cli_hx_download.c` 的 `-P` 行为（过程一致性）是通过 **write callback 返回 `CURL_WRITEFUNC_PAUSE`** 来触发暂停的：

- 在接收回调中检测到跨越阈值则返回 `CURL_WRITEFUNC_PAUSE`（保证暂停点在 callback 边界，且避免“多吃一段数据”）。
- 恢复时调用 `curl_easy_pause(..., CURLPAUSE_CONT)`（可见其 `RESUMED` 路径）。

对比之下，QCurl 当前仅提供“外部调用 `curl_easy_pause`”，难以提供同样确定性的暂停点；若用 progress 信号触发 pause，可能产生字节级偏差（尤其在 HTTP/2/HTTP/3 的帧化/缓冲下更明显），导致 LC-15 类型测试变得不稳定。

## 5. 可行性结论（分层评估）

### 5.1 可行（推荐）：把现有 pause/resume 做成“可用 API”

在 `QCCurlMultiManager` + multi/socket 驱动下，传输级 pause/resume 是 libcurl 官方支持能力，且 QCurl 已有直接调用。只需补齐：

- 状态机与可观测性（Paused 语义）。
- 线程/时序防御（跨线程调用、resume 唤醒）。
- 与调度器语义隔离（避免“队列 pause”与“传输 pause”混淆）。

### 5.2 可行但需约束（对齐 -P）：在 write callback 边界 pause

要对齐 `cli_hx_download -P` 的过程一致性，需要在 QCurl 的写回调路径引入可选的“callback 触发 pause”（返回 `CURL_WRITEFUNC_PAUSE`），并与 `curl_easy_pause(CURLPAUSE_CONT)` 配合使用。

该能力建议先作为**内部/测试专用 hook**暴露（避免过早承诺稳定公共 API），用于驱动 LC-15。

### 5.3 高成本（暂缓）：上传 pause/resume

上传侧要真正 pause/resume，通常需要流式读（`CURLOPT_READFUNCTION`）+ `CURL_READFUNC_PAUSE` + 可 seek/可恢复的数据源抽象；当前 QCurl 以 `CURLOPT_POSTFIELDS` 为主，迁移成本与兼容性风险高，建议单独立项。

## 6. 初始设计建议（MVP：先把 pause/resume 做“可靠”）

### 6.1 API 设计（建议）

- `QCNetworkReply`：仅 **Async** 支持传输级 pause/resume；Sync 模式不支持（见 7.2 验收标准）。
- `PauseMode`：`Recv` / `Send` / `All`（映射到 `CURLPAUSE_RECV/CURLPAUSE_SEND/CURLPAUSE_ALL`）。
- 建议提供 `isPaused()`（或通过 `ReplyState::Paused` 暴露），以便上层与测试可靠观测。
- 状态机补齐：
  - 方案 A：`ReplyState` 增加 `Paused`
  - 方案 B：保持 `ReplyState` 不变，新增独立 `paused` 标记 + `pausedChanged(bool)`
  - 建议优先方案 A（对齐现有 `stateChanged` 语义，减少 API 面扩张）。

### 6.2 线程/时序语义（必须写清楚）

- 异步模式下，pause/resume 的执行应发生在 multi 驱动线程（通常为 `QCCurlMultiManager` 所在线程）。
- 若用户在其他线程调用：
  - 要么明确禁止并 `qWarning()` + no-op；
  - 要么通过 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` marshal 到管理器线程执行（推荐，工程可用性更好）。
- 同步模式下：`pause/resume` 直接 `qWarning()` + no-op（不尝试干预阻塞的 `curl_easy_perform()`）。

### 6.3 resume 的“唤醒”策略（避免挂起）

resume 后如果没有新的 socket 事件/定时器事件，multi 可能短时间不被驱动。建议提供“kick”：

- 优先：如果 libcurl 支持 `curl_multi_wakeup()`，由 `QCCurlMultiManager` 封装调用；
- 退化：调用 `handleSocketAction(CURL_SOCKET_TIMEOUT, 0)` 触发一次 `curl_multi_socket_action()`。

### 6.4 与调度器的关系（命名与语义隔离）

- 明确区分：
  - `QCNetworkRequestScheduler::deferRequest()/undeferRequest()`：调度层“延后/恢复调度”（可能停止正在进行的传输；不等价传输级 pause）
  - `QCNetworkReply::pause()/resume()`：传输层暂停/恢复（不取消、不销毁、不重排）

## 7. MVP 语义契约与验收标准（冻结）

本节内容用于后续实现与测试的“单一事实来源”（acceptance criteria）。除非明确评审变更，否则实现应严格遵守以下契约。

### 7.1 术语定义

- **传输级暂停（Transfer Pause）**：通过 libcurl `curl_easy_pause()` 暂停收/发；不取消请求、不出队；恢复后继续同一次传输。
- **调度级延后（Scheduler Defer）**：从调度系统中暂时移出/恢复；不等价传输暂停；对 running 请求允许“停止执行并延后”，恢复后一般会从头重新请求。

### 7.2 `QCNetworkReply`：传输级 pause/resume（仅 Async 支持）

- [ ] **支持范围**：仅 `ExecutionMode::Async` 支持传输级 pause/resume；`Sync` 模式调用 `pause/resume` 必须 `qWarning()` 且 no-op。
- [ ] **pause 生效条件**：仅当 `state == Running` 时生效；其他状态（`Idle/Finished/Cancelled/Error`）为幂等 no-op。
- [ ] **pause 行为**：必须调用 `curl_easy_pause(easy, flags)`；`flags` 由 `PauseMode` 映射（Recv/Send/All）。
- [ ] **可观测性**：pause 后必须进入“可观测的 Paused 语义”（建议 `ReplyState::Paused` 或 `isPaused()==true`），并对外发出状态变化信号（与现有 `stateChanged` 语义一致）。
- [ ] **不影响调度**：pause 不得调用 `QCCurlMultiManager::removeReply()`，不得触发 scheduler 出队/入队，不得等价 `cancel()`。
- [ ] **resume 生效条件**：仅当处于 Paused 语义时生效；其他状态为幂等 no-op。
- [ ] **resume 行为**：必须调用 `curl_easy_pause(easy, CURLPAUSE_CONT)`，并在恢复后回到 Running（或 `isPaused()==false`）。

### 7.3 `QCNetworkRequestScheduler`：调度级 defer/undefer（与传输 pause 严格区分）

- [ ] **API 命名**：使用 `deferRequest(QCNetworkReply*)` / `undeferRequest(QCNetworkReply*)`，不使用 `pauseRequest/resumeRequest` 以避免与传输级 pause 混淆。
- [ ] **defer（pending）**：当 reply 在 pending queue 时，必须从队列移除并加入 deferred 列表；不得触发传输级 pause。
- [ ] **defer（running）**：当 reply 在 running 列表时，为释放并发槽位允许停止执行并转入 deferred；当前实现可以是 `reply->cancel()`（或等价停止机制）+ 从 running 结构移除 + 维护 host 计数 + `processQueue()`；必须在文档中明确：这不是传输级暂停，`undefer` 后会从头重新请求。
- [ ] **undefer**：当 reply 在 deferred 列表中时，必须移出并重新入队等待调度执行；不保证保留原优先级（除非后续单独实现），但必须行为可预测且可测试。
- [ ] **幂等性**：对已 deferred 的重复 defer、对不在 deferred 的 undefer，必须是幂等 no-op。

### 7.4 resume kick（避免“恢复后卡死”）

- [ ] **wakeup 能力**：`QCCurlMultiManager` 必须提供一次“推进/唤醒 multi”能力（优先 `curl_multi_wakeup()`，否则触发一次等价 `curl_multi_socket_action()` 的 kick）。
- [ ] **强制调用点**：任何成功的 `QCNetworkReply::resume()` 执行后必须触发 wakeup/kick（避免因缺少 socket/timer 事件导致长时间不推进）。

## 8. 对齐 `-P` 的计划（LC-15 前置）

为支撑 LC-15 的“过程一致性”，建议增加内部 hook（先不公开）：

- 在 `QCNetworkReplyPrivate::curlWriteCallback` 中，在 append 之前检查：
  - `pauseAtBytes` 是否设置；
  - 若本次回调跨越阈值，则返回 `CURL_WRITEFUNC_PAUSE`；
  - 记录“已进入 paused”并对外发射状态。
- resume 时调用 `curl_easy_pause(..., CURLPAUSE_CONT)` 并触发 multi 唤醒。

该能力可通过：

- 仅测试可用的 `QCNetworkRequest` 扩展字段（例如 `setPauseAtBytes()`），或
- 仅内部使用的 environment hook（用于 libcurl_consistency Qt Test 驱动）。

## 9. 测试策略（建议）

### 9.1 单元/组件测试（Qt Test）

- 新增用例覆盖：
  - pause/resume 状态转换（Running→Paused→Running→Finished）
  - 多次 pause/resume 的幂等性
  - cancel 与 pause 的交互（Paused 状态 cancel 是否能及时终止）
  - 跨线程调用（若选择 marshal）不会崩溃且行为可预期

### 9.2 一致性测试（pytest + Qt Test 混合）

- 在 `tests/libcurl_consistency/` 下新增 LC-15 对齐用例：
  - baseline：复用 `cli_hx_download -P` 行为
  - QCurl：使用“callback 触发 pause”的内部 hook，做过程事件对齐（暂停点/恢复点/文件边界）

## 10. 风险与缓解

- **内存风险**：异步下载无限 buffer → 建议后续引入背压（buffer 上限触发 pause，消费后 resume）。
- **HTTP/2/HTTP/3 多路复用**：pause 一个 stream 可能影响连接层调度与缓存；需要在实现中避免全局阻塞（优先使用 `CURLPAUSE_RECV`/`CURLPAUSE_SEND` 精准控制）。
- **线程风险**：multi manager 单例线程不确定 → 在文档/代码中显式化约束，并在运行时做防御/强制初始化时机。

## 11. 后续工作（与 tasks.md 对齐）

- LC-15（P2）：待 QCurl 暴露/稳定传输级 pause/resume（含 callback 边界 pause）后，再补齐对齐 `cli_hx_download -P` 的过程一致性回归用例。
