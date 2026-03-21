# 传输级 pause/resume 当前状态

本文是传输级 pause/resume 的状态页，只保留当前可依赖的 contract、证据入口和未决边界；历史执行日志不再在此累计。

## 1. 当前合同

### 1.1 `QCNetworkReply` 传输级 pause/resume

- 公开入口：`QCNetworkReply::pauseTransport(PauseMode)` / `resumeTransport()`
- 支持范围：仅 `ExecutionMode::Async`
- `Sync` 模式：`qWarning()` + no-op
- 生效状态：
  - 仅 `Running -> Paused`
  - 仅 `Paused -> Running`
  - 其他状态重复调用为幂等 no-op
- pause/resume 不得等价于调度器层面的 defer，也不得触发 remove/cancel/重新入队

### 1.2 调度器层 defer/undefer

- 调度层语义使用 `deferRequest()` / `undeferRequest()`
- 该语义只表示“延后调度”，不表示“保留同一次传输继续执行”
- 对 running 请求的 defer 允许释放并发槽位；后续恢复时按调度语义重新开始，而不是继续原传输

### 1.3 恢复推进

- 成功 `resumeTransport()` 后必须触发一次 multi wakeup/kick
- 该合同用于避免“恢复后因为没有新 socket/timer 事件而长时间不推进”

### 1.4 接收方向 backpressure

- 请求级配置位于 `QCNetworkRequest`
- reply 侧可观察：
  - `backpressureActive()`
  - `backpressureLimitBytes()`
  - `backpressureResumeBytes()`
  - `backpressureBufferedBytesPeak()`
  - `backpressureStateChanged(...)`

## 2. 证据入口

- 实现：
  - `src/QCNetworkReply.h`
  - `src/QCNetworkReply.cpp`
  - `src/QCNetworkRequest.h`
  - `src/QCNetworkRequest.cpp`
  - `src/QCCurlMultiManager.h`
  - `src/QCCurlMultiManager.cpp`
  - `src/QCNetworkRequestScheduler.h`
  - `src/QCNetworkRequestScheduler.cpp`
- 回归：
  - `tests/qcurl/tst_QCNetworkReply.cpp`
  - `tests/qcurl/tst_QCNetworkScheduler.cpp`

## 3. 当前已覆盖的关键场景

- Sync 模式 pause/resume 的 no-op 合同
- Async 模式跨线程 pause/resume
- 下载方向 backpressure 激活与自动恢复
- 流式上传方向 pause/resume
- scheduler defer/undefer 与传输级 pause 的语义隔离

## 4. 未决边界

- callback 边界与字节级暂停点是否满足特定一致性场景，仍应以 `tests/libcurl_consistency/` 的实际 gate 结果为准
- 上传/下载 pause 对更复杂业务语义的映射，应由上层场景定义，不在本文扩展

## 5. 相关文档

- `docs/arch/transport-pause-resume.md`
- `docs/dev/build-and-test.md`
