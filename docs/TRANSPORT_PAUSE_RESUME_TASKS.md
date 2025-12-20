# 传输级 pause/resume（MVP）任务拆分与执行日志

来源：`docs/TRANSPORT_PAUSE_RESUME_PLAN.md`（以第 7 章“语义契约与验收标准（冻结）”为单一事实来源）。

> 说明：本任务清单只修改 QCurl 自身代码（`src/`、`tests/`、`docs/`），不触碰 `curl/` 目录。

| ID | 任务描述 | 优先级 | 依赖关系 | 状态 | 执行日志 |
| --- | --- | --- | --- | --- | --- |
| PR-MVP-0 | 对外可观测“Paused”语义：`ReplyState` 增加 `Paused`（或等价 `isPaused()`），并补齐 `cancel()/~QCNetworkReply()/~QCNetworkReplyPrivate()` 在 Paused 下的清理路径。 | 高 | - | 已完成 | 2025-12-17：为 `ReplyState` 增加 `Paused`，新增 `QCNetworkReply::isPaused()`，并确保 `cancel()`/析构在 Paused 状态下也会从 `QCCurlMultiManager` 移除 easy handle。 |
| PR-MVP-1 | 引入 `PauseMode`（Recv/Send/All）并实现 `QCNetworkReply::pause(PauseMode)`/`resume()`：仅 Async 生效；Sync 必须 `qWarning()` + no-op；pause/resume 不得影响调度（不得 removeReply/cancel/出队）。 | 高 | PR-MVP-0 | 已完成 | 2025-12-17：新增 `PauseMode`，补齐 `QCNetworkReply::pause(PauseMode)` 并保留 `pause()` 作为 `All` 的便捷入口；Sync 模式调用直接告警并 no-op。 |
| PR-MVP-2 | 状态机与幂等：仅 `Running→Paused` 生效；仅 `Paused→Running` 生效；其他状态重复调用必须 no-op；`stateChanged` 行为与现有语义一致。 | 高 | PR-MVP-1 | 已完成 | 2025-12-17：实现 pause/resume 的状态门控与幂等（仅 Running 可 pause、仅 Paused 可 resume），并通过 `setState()` 统一发射 `stateChanged`。 |
| PR-MVP-3 | 线程约束：libcurl `curl_easy_pause()` 必须在 `QCCurlMultiManager` 所在线程执行；跨线程调用需 marshal（或明确禁止并告警）。 | 高 | PR-MVP-1 | 已完成 | 2025-12-17：`QCNetworkReply::pause/resume` 增加线程防御：若不在 reply 线程调用则先 marshal 回 reply 线程；`curl_easy_pause()` 通过 `Qt::BlockingQueuedConnection` 保证在 `QCCurlMultiManager` 线程执行。 |
| PR-MVP-4 | resume kick：`QCCurlMultiManager` 提供一次性 wakeup/kick（优先 `curl_multi_wakeup()`；退化为触发一次 `curl_multi_socket_action(CURL_SOCKET_TIMEOUT,0)`）；任何成功 `resume()` 后必须触发。 | 高 | PR-MVP-3 | 已完成 | 2025-12-17：新增 `QCCurlMultiManager::wakeup()`（支持跨线程调用），resume 成功后强制触发；wakeup 采用 `curl_multi_wakeup()`（可用时）+ `QTimer::singleShot(0)` kick `curl_multi_socket_action(CURL_SOCKET_TIMEOUT,0)` 避免同步重入。 |
| PR-MVP-5 | 调度层语义隔离：`QCNetworkRequestScheduler::pauseRequest/resumeRequest` 改名 `deferRequest/undeferRequest`，并将 `m_pausedRequests` 改为 `m_deferredRequests`；同步更新 `tests/tst_QCNetworkScheduler.cpp`。 | 高 | - | 已完成 | 2025-12-17：按冻结语义将 Scheduler “pause/resume” 全面更名为 `defer/undefer`，并在注释中明确其为调度层延后（running 场景通过 cancel 终止并延后，undefer 后从头请求）。同步更新 Qt Test 与 `examples/SchedulerDemo/main.cpp`，修复构建因旧 API 引用导致的编译失败。 |
| PR-MVP-6 | Qt Test 覆盖：pause/resume（状态转换、幂等、Sync 告警 no-op、resume 后继续推进）+ defer/undefer（pending/running、幂等）。 | 高 | PR-MVP-2、PR-MVP-4、PR-MVP-5 | 已完成 | 2025-12-17：在 `tests/tst_QCNetworkReply.cpp` 新增本地 `QTcpServer` 自建 HTTP/1.1 流式响应用例，覆盖 Async 传输级 pause/resume（含跨线程调用）与 Sync 模式告警 no-op；同时更新 `tests/tst_QCNetworkScheduler.cpp` 的用例为 defer/undefer。问题：全量跑 `ctest -R tst_QCNetworkReply` 时 `testStateCancellation()` 触发 QtTest 300s 超时并 `QFATAL`（进程 abort）。定位：`QCCurlMultiManager::removeReply()` 持锁调用 `curl_multi_remove_handle()`，libcurl 同步触发 socket callback（`what=CURL_POLL_REMOVE`）→ `cleanupSocket()` 再次尝试加锁导致死锁。解决：将 `QCCurlMultiManager` 的互斥锁切换为可重入 `QRecursiveMutex`（`src/QCCurlMultiManager.h`）。验证：`./build/tests/tst_QCNetworkReply testSyncPauseResumeNoOp testAsyncTransferPauseResumeCrossThread`、`./build/tests/tst_QCNetworkScheduler` 通过；`ctest -R ^tst_QCNetworkReply$ --output-on-failure` 通过（约 25s）。 |
| PR-P2-0 | （后续）为 LC-15 提供 callback 边界 pause hook：write callback 返回 `CURL_WRITEFUNC_PAUSE` + resume cont + wakeup，用于对齐 `cli_hx_download -P` 的过程一致性。 | 低 | PR-MVP-6 | 未开始 | - |
| PR-P2-1 | （后续）下载 backpressure：为 `QCByteDataBuffer` 引入上限并与 pause/resume 联动，避免异步下载无限增长。 | 低 | PR-MVP-6 | 未开始 | - |
| PR-P2-2 | （后续）上传方向传输级 pause/resume（`CURLOPT_READFUNCTION` + `CURL_READFUNC_PAUSE` + 可 seek 数据源抽象）。 | 低 | PR-P2-1 | 未开始 | - |
