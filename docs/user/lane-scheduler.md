# Lane Scheduler（manager-owned typed lane）

> 本页面面向 QCurl 使用者，说明 1.0.0 lane scheduler public contract。代码事实以 `src/QCNetworkRequest.h`、`src/QCNetworkLaneKey.h`、`src/QCNetworkSchedulerPolicy.h`、`src/QCNetworkAccessManager.h` 和对应测试为准。

## 1. 核心结论

QCurl 现在采用 **manager-owned typed lane scheduler**：

- `QCNetworkAccessManager` 持有自己的 scheduler，调用方不再通过 `manager scheduler getter` 或 thread-local scheduler 配置长期策略。
- 请求 lane 使用 `QCNetworkLaneKey`，不是裸 `QString`。
- 调度策略使用 `QCNetworkSchedulerPolicy` 一次性配置到 manager。
- 自定义 lane 名称必须通过 `QCNetworkLaneKey::fromName(name, &lane, &error)` 显式解析；解析失败不会生成可流动的 invalid sentinel。
- 未注册 lane 固定按 `RequireRegistered` fail-closed，错误文本会包含未注册 lane 名称。
- `schedulerStatistics()` 和返回 `QCNetworkLaneCancelResult` 的 `cancelLaneRequests()` 是 manager-level 观察与控制入口。

## 2. 什么时候使用 lane

如果应用同时存在以下请求类型，就适合拆 lane：

- 控制类：登录、刷新 token、拉配置、拉 manifest、心跳。
- 传输类：下载、上传、分片传输。
- 后台类：埋点、预热、非关键同步。

请求量很少且重要性接近时，可以直接使用 default lane。

## 3. 请求侧 API

`QCNetworkRequest` 只接受 typed lane：

```cpp
QCurl::QCNetworkRequest request(QUrl(QStringLiteral("https://api.example.com/manifest")));
request.setLane(QCurl::QCNetworkLaneKey::control())
       .setPriority(QCurl::QCNetworkRequestPriority::High);
```

内置 lane：

- `QCNetworkLaneKey::defaultLane()`：默认 lane，名称为空。
- `QCNetworkLaneKey::control()`：控制面请求。
- `QCNetworkLaneKey::transfer()`：传输面请求。
- `QCNetworkLaneKey::background()`：后台请求。

自定义 lane 仍然支持，但必须先注册到 policy：

```cpp
QCurl::QCNetworkLaneKey imagePrefetch;
QString laneError;
if (!QCurl::QCNetworkLaneKey::fromName(QStringLiteral("ImagePrefetch"),
                                       &imagePrefetch,
                                       &laneError)) {
    qWarning() << laneError;
}
```

## 4. 配置 scheduler policy

```cpp
QCurl::QCNetworkAccessManager manager;
manager.enableRequestScheduler(true);

QCurl::QCNetworkSchedulerPolicy policy = QCurl::QCNetworkSchedulerPolicy::defaultPolicy();
policy.setMaxConcurrentRequests(6);
policy.setMaxRequestsPerHost(2);
policy.setMaxBandwidthBytesPerSec(0);
policy.setThrottlingEnabled(false);
QString error;

QCurl::QCNetworkSchedulerPolicy::LaneConfig control;
control.setWeight(3);
control.setQuantum(1);
control.setReservedGlobal(1);
control.setReservedPerHost(1);
if (!policy.setLaneConfig(QCurl::QCNetworkLaneKey::control(), control, &error)) {
    qWarning() << error;
}

QCurl::QCNetworkSchedulerPolicy::LaneConfig transfer;
transfer.setWeight(1);
transfer.setQuantum(1);
if (!policy.setLaneConfig(QCurl::QCNetworkLaneKey::transfer(), transfer, &error)) {
    qWarning() << error;
}

if (!manager.setSchedulerPolicy(policy, &error)) {
    qWarning() << error;
}
```

`QCNetworkSchedulerPolicy::validate()` 会拒绝：

- invalid default lane。
- default lane 未注册。
- `maxConcurrentRequests <= 0`。
- `maxRequestsPerHost <= 0`。
- `maxBandwidthBytesPerSec < 0`。
- lane `weight <= 0` 或 `quantum <= 0`。
- lane reservation 小于 0。

## 5. 调度语义

调度器仍保留 simple queue 策略，适合几十到几百个 pending 请求：

1. 优先满足 per-host reservation。
2. 再满足 lane global reservation。
3. 最后按 DRR best-effort 和 lane 内 priority 选择请求。

`priority` 是非抢占式：已 Running 的请求不会被更高优先级请求中断；priority 只影响 pending 出队顺序。

## 6. 未注册 lane fail-closed

```cpp
QCurl::QCNetworkLaneKey custom;
QString laneError;
if (!QCurl::QCNetworkLaneKey::fromName(QStringLiteral("ImagePrefetch"), &custom, &laneError)) {
    qWarning() << laneError;
}
QCurl::QCNetworkRequest request(QUrl(QStringLiteral("https://cdn.example.com/prefetch")));
request.setLane(custom);

// 如果 custom 没有写入 manager.schedulerPolicy()，启用 scheduler 后该请求会失败，
// 不会静默映射到 default lane。
auto *reply = manager.get(request);
```

需要自定义 lane 时，先把它写入 policy：

```cpp
QString error;
if (!policy.setLaneConfig(custom, QCurl::QCNetworkSchedulerPolicy::LaneConfig{}, &error)
    || !manager.setSchedulerPolicy(policy, &error)) {
    qWarning() << error;
}
```

## 7. 观察统计与取消 lane

```cpp
const auto stats = manager.schedulerStatistics();
qDebug() << "pending=" << stats.pendingRequests()
         << "running=" << stats.runningRequests();

const auto cancelResult = manager.cancelLaneRequests(
    QCurl::QCNetworkLaneKey::transfer(),
    QCurl::QCNetworkAccessManager::SchedulerCancelScope::PendingOnly);
if (!cancelResult.isSuccess()) {
    qWarning() << cancelResult.error();
}
```

取消范围：

- `PendingOnly`：只清理 pending + deferred，不打断 running。
- `PendingAndRunning`：pending + deferred + running 一并取消，用于整条 lane 排空。

`cancelLaneRequests()` 会区分 invalid lane、未注册 lane、非 owner thread 和 scheduler 未启用；这些失败状态不会执行副作用，也不会取消 default lane。

## 8. 线程边界

manager-level scheduler API 必须在 manager owner thread 调用。跨线程配置时，把调用显式投递到 owner thread：

```cpp
QMetaObject::invokeMethod(manager, [manager, policy]() {
    QString error;
    if (!manager->setSchedulerPolicy(policy, &error)) {
        qWarning() << error;
    }
}, Qt::QueuedConnection);
```

异步发送路径仍要求 manager owner thread 存在 Qt event loop；没有 event dispatcher 时会 fail-closed，返回可诊断错误。

## 9. scheduler limit 与 libcurl multi limit

两层 limit 不是同一件事：

- `QCNetworkSchedulerPolicy` 管 reply admission：pending 里哪些 reply 可以进入 running。
- `QCNetworkConnectionPoolConfig` / multi limit 管传输层连接、host、stream 和连接缓存。

不要把 scheduler 并发数当作 libcurl 连接池上限，也不要把 libcurl multi limit 当作 lane fairness 或 reservation。

## 10. 迁移旧代码

迁移原则：把“字符串 lane + scheduler 指针配置”收敛为“typed lane + manager policy”。

```cpp
request.setLane(QCurl::QCNetworkLaneKey::control());
QString error;
if (!policy.setLaneConfig(QCurl::QCNetworkLaneKey::control(), laneConfig, &error)) {
    qWarning() << error;
}
manager.setSchedulerPolicy(policy);
```
