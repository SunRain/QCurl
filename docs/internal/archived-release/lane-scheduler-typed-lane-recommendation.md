# Lane scheduler hard-break recommendation for hundreds of pending requests

本文记录 QCurl 在 hard-breaking 前提下，针对“几十到几百个 pending 请求”规模的 lane-aware 调度推荐方案。它是维护者架构建议，不改变当前已发布用户文档的稳定 contract。

## 1. 结论

推荐采用 **Manager-owned typed lane scheduler, simple queue policy**。

也就是：hard-break public contract 和生命周期边界，但保留简单平铺队列实现。不要为几十到几百个 pending 请求引入复杂多级索引队列。

该方案优先解决四类实际问题：

1. scheduler 生命周期必须符合 Qt owner-thread 和 event loop 规则。
2. lane contract 必须从自由字符串收敛为稳定、可验证的请求分类。
3. scheduler admission limit 与 libcurl multi limit 必须有统一入口和清晰语义边界。
4. 调度策略实现应保持可读、可测、可维护，不为当前规模过度设计。

## 2. 适用范围

本方案适用于以下规模和场景：

- pending 请求通常为几十个到 100/200 个，偶发达到几百个。
- 请求类别主要分为控制面、传输面、后台任务等稳定 traffic class。
- 需要避免大下载、大上传长期挤压登录、刷新 token、manifest、心跳等控制类请求。
- 本轮架构决策接受 hard-breaking 变更 public API、ABI baseline、consumer smoke 和文档。

本方案不面向“长期数千到上万个 pending 请求”的高密度调度器场景。未来只有在 profiling 证明调度扫描成为瓶颈后，才另开独立架构评估索引化队列。

## 3. 设计原则

### 3.1 Qt6 网络编程边界

`QCNetworkRequestScheduler` 应归属于明确的 owner thread，并依赖该线程的 Qt event loop。不要让下游依赖 public `thread_local` scheduler 单例来隐式创建 `QObject`、`QTimer` 以及 queued invoke 状态。

推荐边界：

- `QCNetworkAccessManager` 显式创建并持有 scheduler。
- scheduler 是 manager 的子对象。
- scheduler、reply 和 libcurl multi manager 位于同一 owner thread。
- owner thread 没有 event dispatcher 时 fail-closed，并返回可诊断错误。
- `QCurl::initialize()` 继续只负责公共元类型注册，不承担 scheduler bootstrap。

### 3.2 libcurl Qt binding 边界

lane-aware 调度属于应用层 admission policy。`QCCurlMultiManager` 继续只负责 libcurl multi_socket 的 socket/timer 驱动、easy handle 生命周期和完成回调投递。

不把 lane 策略下沉到 `QCCurlMultiManager`，避免把调度策略、传输推进和 socket 生命周期混在一起。

### 3.3 Qt/KDE 应用库边界

public contract 应保持清晰、稳定、ABI 可治理：

- 用薄值类型表达 lane，而不是裸 `QString`。
- 用 shared-data 值类型表达 scheduler policy。
- public API 不暴露不必要的内部容器与策略细节。
- hard-break 后同步更新 ABI baseline、public API smoke、用户文档和维护者文档。

## 4. 推荐 public contract

### 4.1 manager-owned scheduler

移除 public thread-local scheduler accessor 依赖。下游只通过 `QCNetworkAccessManager` 配置和观察调度能力。

唯一 public control surface 是 manager-level API：

```cpp
manager.setSchedulerPolicy(policy);
manager.schedulerStatistics();
const QCNetworkLaneCancelResult result = manager.cancelLaneRequests(lane, scope);
```

`QCNetworkAccessManager` 显式创建并持有 scheduler。scheduler 是 manager 的子对象，保留为内部 `QObject` 实现，不作为下游主要 public control surface。执行时同步移除 public workflow 中的 manager scheduler getter + `setConfig()` 与 manager scheduler getter + `setLaneConfig()` 路径。

### 4.2 typed lane key

将请求 lane 从自由 `QString` 收敛为薄值类型：

```cpp
QCNetworkLaneKey
```

`QCNetworkLaneKey` 内部保存稳定名称，但 public API 不再直接把任意字符串作为核心路径。

推荐调用形态：

```cpp
QCurl::QCNetworkRequest request(url);
request.setLane(QCurl::QCNetworkLaneKey::control());
```

保留自定义 lane 的能力。`fromName()` 固定为 `bool + out + QString *error` 失败表达；admission 前必须已在 `QCNetworkSchedulerPolicy` 注册：

```cpp
QCurl::QCNetworkLaneKey imagePrefetch;
QString laneError;
if (!QCurl::QCNetworkLaneKey::fromName(QStringLiteral("ImagePrefetch"),
                                       &imagePrefetch,
                                       &laneError)) {
    qWarning() << laneError;
}
```

unknown lane 固定采用 `RequireRegistered` 语义。请求 lane 未注册时 fail-closed，返回可诊断错误；scheduler 不默默扩张长期运行态，也不静默映射到 default lane。

### 4.3 scheduler policy

新增顶层 manager-level 值类型：

```cpp
QCNetworkSchedulerPolicy
```

它集中描述：

- lane order
- default lane
- unknown lane mode：RequireRegistered
- lane weight
- lane quantum
- reservedGlobal
- reservedPerHost
- scheduler max concurrent replies
- scheduler max per-origin replies

policy 应先验证，再以快照形式交给 scheduler 使用。业务代码不得继续零散调用 `setLaneConfig()` 配置长期运行态。

## 5. 推荐内部实现

### 5.1 保留简单队列

继续保留当前平铺队列模型：

- `pendingRequests`
- `deferredRequests`
- `runningRequests`
- host counters
- lane counters
- DRR deficit
- reservation cursor

继续使用当前调度顺序：

1. per-host reservation
2. lane global reservation
3. DRR best-effort
4. lane 内 priority + host rotation

这套模型对几百个 pending 请求足够清晰，也更容易保持当前测试 contract。

### 5.2 只做轻量热路径优化

执行以下局部优化：

- 在一次 `processQueue()` 调度轮内缓存 active lane 集合。
- 用当前轮索引替代重复 `laneOrder.indexOf()`。
- 避免 `activeLanes.contains()` 的重复线性查找。
- 将 `wouldViolateReservation()` 改为轻量 delta 计算，减少整份 `QHash` 复制。
- 预计算 runnable host / reservation demand，避免每个候选重复全量扫描。
- 在 finalize 后回收未配置且无 pending/deferred/running 的临时 lane。

这些优化不改变外部语义，也不引入复杂索引失效问题。

### 5.3 拆出纯策略核心

把策略选择拆成内部纯逻辑类：

```cpp
LaneSchedulingPolicy
```

它只接收只读 state view 和 policy/config，返回下一条要启动的稳定内部 `SchedulerRequestId`。它不依赖 `QObject`，不发信号，不持有 reply 生命周期，也不返回会受 `QList` 删除影响的瞬时 index。

职责分层：

- `QCNetworkRequestScheduler`：owner thread、信号、reply 生命周期、queue handoff。
- `SchedulerQueues`：pending/deferred/running 容器和计数器。
- `LaneSchedulingPolicy`：选择下一条请求。
- `LaneRuntimePruner`：回收未配置空闲 lane。

这样符合 SOLID，也能让核心调度规则通过纯单元测试覆盖。

## 6. limits 语义

建议提供统一配置入口，但不要把 scheduler limit 和 libcurl multi limit 混成同一个概念。

### 6.1 scheduler admission limits

scheduler limit 描述“允许多少 reply 从 pending 进入 running”：

- global concurrent replies
- per-origin concurrent replies
- lane reservation
- lane fairness

### 6.2 libcurl multi limits

libcurl multi limit 描述传输层资源阀门：

- `CURLMOPT_MAX_TOTAL_CONNECTIONS`
- `CURLMOPT_MAX_HOST_CONNECTIONS`
- `CURLMOPT_MAX_CONCURRENT_STREAMS`
- `CURLMOPT_MAXCONNECTS`

### 6.3 推荐关系

manager 暴露两个明确配置面，文档必须明确两层语义不同：

- scheduler 决定“哪个 reply 可以启动”。
- libcurl multi 决定“传输层怎样复用、限制和推进连接”。

`QCNetworkSchedulerPolicy` 管 admission；`QCNetworkConnectionPoolConfig` 及 multi limit 配置管 `CURLMOPT_*`。manager-level preset profile 仅作为批量写入 helper，同步写入这两类配置；它不得把两层 limit 合并成同一语义字段。这样既满足 libcurl binding 最佳实践，也避免下游误以为两层 limit 完全等价。

## 7. 明确排除项

本方案明确不做以下内容：

- 不做 per-lane / per-priority / per-host 复杂多级索引队列。
- 不做 heap、tree、intrusive queue 与复杂增量索引维护。
- 不做 preemptive scheduler；running 请求不被更高优先级请求隐式打断。
- 不把 lane 策略下沉到 `QCCurlMultiManager`。
- 本 hard-break 不新增 public `QCNetworkEngine`；后续需要拆分 manager 职责时，另开独立架构文档。
- 不把 `deferPendingRequest()` 包装成传输级 pause/resume。

## 8. 测试要求

hard-break 执行时至少补充并更新以下测试：

- owner-thread scheduler 生命周期。
- 无 event dispatcher fail-closed。
- typed lane key 的默认 lane、内置 lane、自定义 lane。
- unknown lane policy：RequireRegistered fail-closed。
- policy validation：非法 weight、quantum、reservation、limit。
- 200 pending 请求下的 fairness 和 reservation 顺序。
- per-host head-of-line avoidance。
- reservation fallback progress。
- lane cancel 覆盖 pending/deferred/running。
- scheduler limit 与 libcurl multi limit 的边界证明。
- public API consumer smoke 与 ABI baseline。

## 9. 迁移边界

hard-breaking 迁移应一次性收敛，不保留并行长期路线：

1. 旧 `QString` lane setter 迁移到 `setLane(QCNetworkLaneKey)`。
2. thread-local scheduler accessor 不再作为推荐 public 入口。
3. `setLaneConfig()` 从常用 public workflow 收敛到 manager-level `QCNetworkSchedulerPolicy`。
4. 文档同步更新 `docs/user/lane-scheduler.md`、public API manifest、consumer smoke 和 ABI baseline。
5. 当前实现的 `QList + DRR + reservation` 保留，只做轻量热路径优化。

## 10. 最终推荐

对几十到几百个 pending 请求，最佳方案是：

**hard-break contract，保留 simple queue。**

也就是修正 ownership、typed lane contract 和 limits 语义，而不是为当前规模重写复杂调度容器。
