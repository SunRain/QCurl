# 传输级 pause/resume 架构说明

本文描述 QCurl 当前实现下，传输级 pause/resume 的稳定架构边界。它是维护者视角的 contract 文档，不再保留“计划中/执行中”的历史叙事。

## 1. 目标

QCurl 需要同时区分两类语义：

- 传输级 pause/resume：同一次传输继续执行，依赖 libcurl 的 pause 能力
- 调度级 defer/undefer：请求让出调度槽位，之后按调度语义重新开始

两者不能混名，也不能混用。

## 2. 架构前提

- 异步请求由 `QCCurlMultiManager` 驱动 libcurl multi/socket 循环。
- `QCNetworkReply` 负责单请求的可观测状态与 pause/resume 入口。
- `QCNetworkRequestScheduler` 负责 pending/running/deferred 的调度管理，不负责保留传输现场。

## 3. `QCNetworkReply` 的合同

### 3.1 支持范围

- 只支持 `ExecutionMode::Async`
- `Sync` 模式调用 `pauseTransport()` / `resumeTransport()` 必须给出警告并保持 no-op

### 3.2 状态机

- 仅 `Running -> Paused` 生效
- 仅 `Paused -> Running` 生效
- 其他状态或重复调用必须保持幂等

### 3.3 线程语义

- 对外允许跨线程调用
- 实际执行必须 marshal 到 reply / multi manager 所在的线程
- 回调栈内需要暂停时，应走 callback 边界安全路径，而不是在错误线程或错误时机直接干预 libcurl

### 3.4 恢复推进

- 成功恢复后必须显式触发 `QCCurlMultiManager::wakeup()`
- 原因：恢复操作本身不保证马上伴随新的 socket/timer 事件

## 4. `QCNetworkRequestScheduler` 的合同

- `deferPendingRequest()` / `undeferRequest()` 表达的是调度层语义
- `deferPendingRequest()` **仅对 Pending 生效**：把请求从队列移入 deferred；对 Running/Deferred 返回 false
- 若调用方需要释放并发槽位，应显式 `cancelRequest()`（或按 lane/范围使用 `cancelLaneRequests()`）；不得被包装成“传输暂停”
- 因此调用方如果需要“真正继续同一次传输”，必须使用 reply 侧 pause/resume，而不是 scheduler API

## 5. backpressure 与传输 pause 的关系

- backpressure 是内部流控机制，不是额外的对外状态机
- 对调用方可见的是：
  - 是否激活
  - 上下水位
  - 生命周期峰值缓冲
- 其内部实现可以复用传输级 pause/resume，但语义上仍属于“缓冲控制”

## 6. 对一致性测试的影响

当测试需要验证 callback 边界、暂停点或恢复点时，不能只看“是否最终完成”，还必须看：

1. pause 是否发生在允许的边界
2. resume 后是否真正继续推进
3. scheduler 语义是否被错误混入

因此相关结论应以以下证据共同成立为准：

- `tests/qcurl/tst_QCNetworkReply.cpp`
- `tests/qcurl/tst_QCNetworkScheduler.cpp`
- `tests/libcurl_consistency/`

## 7. 维护约束

修改以下任一能力时，应同步更新本文与状态页：

- `PauseMode`
- `pauseTransport()` / `resumeTransport()`
- `QCCurlMultiManager::wakeup()`
- `deferPendingRequest()` / `undeferRequest()`
- 下载 backpressure

## 8. 非目标

本文不负责：

- 记录某次修复发生在哪一天
- 保存任务执行日志
- 取代测试报告或 CI 工件
