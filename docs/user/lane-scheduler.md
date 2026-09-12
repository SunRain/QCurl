# Lane scheduler：manager 正式入口

`QCNetworkAccessManager` 提供 lane 配置、调度通知、单请求控制、整 lane 取消和统计。
消费者不需要 scheduler 指针；scheduler 和 admission core 不安装，也不属于公共 ABI。
本页对应当前开发候选，不代表已发布版本或旧二进制兼容承诺。

## 配置与请求

适合控制请求、批量传输和后台流量混用的场景；少量同质请求使用 default lane 即可。
默认策略注册 default、Control、Transfer 和 Background。自定义名称先通过
`QCNetworkLaneKey::fromName()` 解析，再用 policy 显式注册。未注册或非法 lane 直接失败，
不自动注册、不映射到 default lane。

```cpp
QCurl::QCNetworkAccessManager manager;
manager.enableRequestScheduler(true);
auto policy = QCurl::QCNetworkSchedulerPolicy::defaultPolicy();
policy.setMaxConcurrentRequests(6);
policy.setMaxRequestsPerHost(2);
policy.setAdmissionByteBudget(0);

QCurl::QCNetworkSchedulerPolicy::LaneConfig control;
control.setWeight(3);
control.setReservedGlobal(1);
control.setReservedPerHost(1);
QString error;
if (!policy.setLaneConfig(QCurl::QCNetworkLaneKey::control(), control, &error)
    || !manager.setSchedulerPolicy(policy, &error)) {
    qWarning() << error;
    return;
}

QCurl::QCNetworkRequest request(QUrl(QStringLiteral("https://api.example.com/manifest")));
request.setLane(QCurl::QCNetworkLaneKey::control());
request.setPriority(QCurl::QCNetworkRequestPriority::High);
auto *reply = manager.get(request);
```

有效策略必须注册 `QCNetworkLaneKey::defaultLane()`；未设置 lane 的请求始终使用它。
`QCNetworkSchedulerPolicy()` 本身尚未注册 lane，不能直接作为有效配置使用。

| 参数 | 接受域与含义 |
|---|---|
| `maxConcurrentRequests` | 正 int；本 manager 已分配的 admission 槽位上限 |
| `maxRequestsPerHost` | 正 int；按 scheme、规范化 host、有效 port 组成的 origin 分组 |
| `LaneConfig::weight` | 1 至 INT_MAX；按启动次数计算的轮转份额，不是字节或带宽权重 |
| `reservedGlobal` / `reservedPerHost` | 非负 int；非抢占式 reservation，允许超配但不能突破硬上限 |
| `admissionByteBudget` | 非负 qint64；一秒观测窗口的上传与下载字节阈值，0 禁用 |

lane 配置校验失败不修改 policy；manager setter 失败也不改变有效策略、请求和计数。
移除存在 Pending、Deferred 或 Running 的 lane 必须失败。先等待排空或显式取消，再构造
不包含它的新策略。其他合法更新先一致提交、再通知或调度；同步回调内可读取新快照并嵌套
应用另一策略，外层返回不会覆盖内层提交。重复应用相同配置不重置公平轮次。

## 选路、并发与流量边界

依次满足 per-origin reservation、lane 全局 reservation，最后执行按启动次数加权的轮转；
同一 lane 内先看六档 priority，再按 origin 轮转，避免饱和 host 阻塞其他 host。

- 公平前提：相关 lane 持续存在可运行 Pending，且 reservation 没有持续耗尽全部容量。
  在此前提下 3:1 权重表示约 3:1 启动份额，不承诺任意负载绝不饥饿或严格延迟 QoS。
- 非抢占：提升优先级、修改权重或降低配额不终止已运行传输。降低并发上限后自然排空，
  未降到新上限以下前不再启动请求。
- scheduler Running 只表示已分配启动槽位，不代表 TCP 连接或已有传输字节。
- `QCNetworkConnectionPoolConfig` 的 libcurl multi 连接/host/stream 限额独立生效；
  同线程仍共享既有 multi。HTTP/2/3 可用一条连接承载多个传输。
- `admissionByteBudget` 只在已观测字节达到阈值时延迟新 admission，不暂停、限速或重新
  分配正在运行的传输。只有存在实际门控工作才设置一次性唤醒，不保持空转周期 timer。
- 单请求速率使用 `QCNetworkRequest::setMaxDownloadBytesPerSec()` /
  `setMaxUploadBytesPerSec()`，由 libcurl 执行。**不提供 manager 级聚合速率整形**；
  给所有请求设置相同速率也不等于聚合限速。

## 通知与生命周期

所有请求通知携带 `QCNetworkReply *`、typed lane、origin 和 priority：

| manager 信号 | 产生条件 |
|---|---|
| `schedulerRequestQueued` | 初次进入 Pending 或 undefer 恢复入队 |
| `schedulerRequestPriorityChanged` | Pending 优先级实际变化；不冒充入队 |
| `schedulerRequestAboutToStart` | owner-thread 同步启动前取消窗口 |
| `schedulerRequestStarted` | execute 已提交，回调后对象与本次启动仍有效；不是传输成功 |
| `schedulerRequestCancelled` | 调度取消已提交，最多一次；不表示 finished 已送达 |
| `schedulerPendingQueueEmpty` | Pending 非空变为空；不表示 Running/Deferred 为空或全部完成 |

重复处理空队列不再次发清空通知；重新入队后再次清空会再次通知。优先级未变化不发任何变化通知。

reply 是非 owning 借用，不延长生命周期。lane/origin/priority 是事件产生时已提交的值快照，
复制后不随请求变化或销毁而变化。不要保存裸指针、容器引用或字符串 view 供异步使用。
queued 收件描述历史事件，不保证 reply 仍活着，也没有启动否决权。

需要延后操作时，应在对象有效的 owner-thread 同步回调中预先捕获 `QPointer`，真正执行时
回到 owner thread 重新检查；不能在 queued 槽收到可能悬空的裸指针后才构造 QPointer。
QPointer 不提供共享所有权或跨线程访问许可。

```cpp
QObject::connect(&manager, &QCurl::QCNetworkAccessManager::schedulerRequestAboutToStart,
                 &manager, [&manager](QCurl::QCNetworkReply *reply) {
    if (reply->url().path() == QStringLiteral("/cancel-this")) {
        const auto result = manager.cancelScheduledRequest(reply);
        if (result != QCurl::SchedulerCommandResult::Applied) {
            qWarning() << "取消被拒绝" << static_cast<int>(result);
        }
    }
});
```

私有 scheduler 到 manager 的转发保持同线程同步。上述取消生效后不会 execute，也不会发
该次 Started；调度器不等待 queued 槽或异步审批。回调可取消请求或销毁 reply/manager；
工厂期间发生销毁时返回 nullptr，后续代码应先检查返回值。

## 单请求命令

- `deferScheduledRequest(reply)`：仅 Pending → Deferred。
- `undeferScheduledRequest(reply)`：仅 Deferred → Pending，保留调度优先级和请求内容。
- `setScheduledRequestPriority(reply, priority)`：仅修改 Pending，不重排运行中的传输。
- `cancelScheduledRequest(reply)`：立即解除调度跟踪，安排 reply 取消；不等待网络清理。

defer/undefer 不等于传输 pause/resume，也不是任意改写请求后的重新提交。

| `SchedulerCommandResult` | 含义 |
|---|---|
| Applied | 本次调度变更已同步提交；重入后状态可能再次变化，不表示传输完成 |
| NoChange | 合法但没有实际变化，例如 Pending 设置同一优先级；不增加统计或通知 |
| WrongThread | 先于任何参数或调度状态访问拒绝错误调用线程 |
| NullReply | 空参数 |
| InvalidArgument | 非法优先级等参数 |
| NotTracked | 当前 manager 不跟踪它，包括其他 manager、从未跟踪或已解除跟踪的请求 |
| ThreadAffinityMismatch | 已跟踪的有效 reply 与 manager 亲和性不符 |
| InvalidState | 已跟踪但状态不允许；重复 defer/undefer 属于此类，而不是 NoChange |

拒绝不改变请求、队列或统计，不发业务通知，不偷偷 queued 重试。校验顺序是调用线程、
空参数/枚举、当前 manager 绑定、已跟踪对象亲和性、状态。归属不由 parent、线程或 lane
推断；其他 manager 的有效请求统一 NotTracked，不探查真实归属或转发。接口不承诺识别
任意悬空裸指针，调用方须保证参数对象有效。

## 整 lane 取消与统计

```cpp
const auto result = manager.cancelLaneRequests(QCurl::QCNetworkLaneKey::transfer(),
    QCurl::QCNetworkAccessManager::SchedulerCancelScope::PendingOnly);
if (!result.isSuccess()) {
    qWarning() << result.error();
}
const auto stats = manager.schedulerStatistics();
qInfo() << stats.pendingRequests() << stats.runningRequests() << stats.cancelledRequests();
```

`PendingOnly` 包含 Pending 和 Deferred；`PendingAndRunning` 额外取消已占槽请求。
有效、已注册且没有匹配请求的 lane 取消成功，数量为 0。非法 lane、非法 scope、未注册、
错误线程和调度器未启用分别返回结构化失败；不影响其他 lane 或其他 manager。

统计由同一核心记账：重复取消、晚到 finished/destroyed 不重复计数；完成耗时使用单调时间。
completed 表示已观察到的非取消终态，不等于 HTTP 成功；尚无终态就直接销毁只回收调度状态。
所有 manager 调度操作要求 owner thread；异步发送还需要可处理事件的 Qt dispatcher。

## 旧接口退出

此开发候选允许 lane scheduler 范围内的 API/ABI hard-break，需要修改下游并重新构建：

- 不再安装或导出旧 scheduler 类及其 Config/Statistics/LaneConfig；改用 manager 入口与
  `QCNetworkSchedulerPolicy` / `QCNetworkSchedulerStatistics`。
- quantum 与可配置 default lane 退出；仅使用正 weight 和固定的显式 default lane。
- 单值 UnknownLaneMode 查询退出，RequireRegistered 是固定规则。
- 旧带宽名称与开关改为一个 `admissionByteBudget` 阈值，0 禁用。
- 不提供别名、兼容重载、过渡壳或新旧并行路径。完整可编译用法见 `examples/SchedulerDemo`。
