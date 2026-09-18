# 从 QCurl 1.0.0 迁移到 2.0.0

1.0.0 是已发布历史，2.0.0 是有意打破 v1 源码/API/ABI 的开发候选。下游需按新头文件和库重新构建；采用 2.0 后，每次 QCurl 更新也必须重新编译和链接。

本指南对照 `v1.0.0@12822251efb04ea78d97f33ed441a85660e72bbf` 与当前 2.0.0 候选的真实公开头差异。Retry/TLS/Proxy/Timeout 的 accessor、typed lane 和 QCCookie 在 v1 已存在，不是本次新增迁移。现行实现写法见[PIMPL 规范](../dev/pimpl-and-shared-data-style.md)，用户可见变化见[CHANGELOG](../../CHANGELOG.md#2.0.0)。

## 1. 依赖、请求构造与可失败配置

- 最低依赖是 Qt **6.10.3**、libcurl 7.85.0，源码构建、安装包发现和公开头均执行同一下限。Qt 6.10.0–6.10.2 不受支持。
- `QCNetworkRequest(const QUrl &)` 改为 explicit；依赖 QUrl 隐式转换的调用改为 `QCNetworkRequest{url}`。
- Request redirect/transfer 配置的可失败 setter 返回 `QCNetworkConfigUpdateResult`。检查 Applied/InvalidArgument，拒绝时保留旧状态，不再依赖负值清除配置或非法水位回退默认值。
- RetryPolicy 的带参构造改用 `create()` validated factory，setter 返回显式更新结果。TLS/Proxy accessor 本身不是本次新增。

## 2. 缓存 key、有序头与清理结果

自定义缓存实现将 URL-only lookup/insert 改为 `QCNetworkCacheRequestKey`，按 method、规范化 URL、Vary 请求头和认证分区区分条目。带认证的请求须提供 cachePartitionKey 才能存储；clear 返回 `QCNetworkCacheClearResult`，检查状态、成功/失败计数与残留容量，不能假设全部删除成功。

移除以下 v1 的有损缓存策略 overload：

- `parseExpirationDate(const QMap<QByteArray, QByteArray> &)`
- `isCacheable(const QMap<QByteArray, QByteArray> &)`
- `varyHeaderNames(const QMap<QByteArray, QByteArray> &)`

改传 `QList<QCurl::RawHeaderPair>`，保留响应头的接收顺序和重复字段。metadata 的 headers/setHeaders 仍是观察与存储 API，不是另一条策略求值路径。当前缓存与续传行为见[配置](configuration.md)。

## 3. Logger、cookie 与进程期关闭

- 使用 `QCNetworkLoggerHandle::create()` / `createWithBorrow()` 创建 opaque handle 后传给 manager，不再注入裸 owning logger 或 owning smart pointer。manager 和已创建 reply 保留各自快照。
- DefaultLogger 文件配置和写入返回 `QCNetworkLogResult`，检查状态与错误，不依赖隐藏 last-error。
- cookie 异步操作移除并行结果信号，改为消费每次调用返回的 QFuture；QCCookie 在 v1 已是 Core 模型。
- 需要手动关闭 libcurl 时使用应用拥有的 QCurlRuntime：先停止外部 libcurl 使用者、排空 handle/callback 并 join worker，再在 owner loops 仍运行时 beginShutdown，最后在协调线程 shutdownAndWait。完整前提见[公开合同](../../src/QCurlRuntime.h)；这不提供 QCurl 动态卸载。

## 4. Lane scheduler

已有 typed lane 与 manager policy 的调用保留；移除对公开 scheduler 实现和旧配置/统计类型的依赖，改用 manager 通知与单请求控制。移除 quantum、可配置 default lane、UnknownLaneMode 和旧带宽开关，按单一启动 weight 配置。

命令返回 SchedulerCommandResult，Applied 是同步提交而非网络完成；拒绝无副作用，NoChange 不发变化通知。admissionByteBudget 只控制新 admission，不替代请求级限速。[调度正文](lane-scheduler.md)统一定义 reservation、取消、通知与借用生命周期。

## 5. Multipart 设备与 Blocking 响应头

将 Multipart body 的旧 device 查询改为 `takeDevice(parent)`；这是在源设备 owner thread 创建并转移 wrapper 所有权的动作，不是 getter。源设备仍由调用方拥有，必须在请求结束前保持存活、可读、可 seek 且不改变线程；构造返回的 optional 失败和 takeDevice 失败都应检查。

Blocking result 的旧 rawHeaderList 别名改为 `headers()`，保持接收顺序与重复字段。rawHeaders 是会折叠重复项的便捷 map，不能替代有序结果。Request 的 rawHeaderList 仍返回请求头名称，不是被移除的 Blocking alias。

## 6. HTTP、连接池、重试与续传

- 合法自定义 HTTP method token 现在按原字节发送，不转成大写；请求头按大小写无关字段名替换，不再因大小写差异生成重复字段。
- manager 所属线程有事件循环时，拿到 reply 后再连接完成信号即可观察终态，包括校验失败；错误线程或无事件循环拒绝仍可同步查询。
- 未配置代理或显式 None 时不继承环境代理；需要代理的调用方须显式设置 QCNetworkProxyConfig。
- 将 maxConnectionsPerHost/maxTotalConnections 改为对应的 multiMaxHostConnections/multiMaxTotalConnections。移除 pipeliningEnabled、connectionWarmingEnabled 及其 setter，不提供兼容开关。
- 连接池配置是进程模板，由每线程 multi 在下一次网络 admission 时应用，不是立即生效的进程总连接上限。使用 activeRequests 而不是 activeConnections，移除 idleConnections 消费；统计描述 Core 请求，不是物理连接库存。
- maxIdleTime 映射原生连接空闲年龄，与 TCP keepalive 时序独立。
- 响应数据被通用 consumer 消费后不能透明重试；只有文件下载 job 可先恢复自己的输出再重试。续传成功要求 identity 表示、完整 Range 及匹配的接收/最终文件长度。

相关参数、失败和重放边界见[配置](configuration.md)，传输暂停不等于 scheduler defer，见[流控](flow-control.md)。

## 7. 协议限制与命名 HTTP 常量

setAllowedProtocols/setAllowedRedirectProtocols 不再接收字符串列表，改为 `QCNetworkProtocols`（QCNetworkProtocol 的 QFlags），getter 返回 `std::optional<QCNetworkProtocols>`，不保留字符串兼容 overload：

```cpp
request.setAllowedProtocols(QCurl::QCNetworkProtocol::Http | QCurl::QCNetworkProtocol::Https);
request.setAllowedRedirectProtocols(QCurl::QCNetworkProtocol::Https);
request.setAllowedProtocols({}); // 清除显式设置，恢复 HTTP 与 HTTPS。
```

未设置或空集合恢复 HTTP+HTTPS 默认值，不表示禁用所有协议。强制 cast 产生的非法 bits 保留到网络访问前诊断拒绝，不静默过滤；Core/Blocking 都通过 libcurl 强制初始与重定向限制，安装必需限制失败不能继续发送。

- QCNetworkHttpHeaders.h 在 QCurl::httpheaders 下提供 owning QByteArray 头名，例如 kContentType、kAuthorization；现有 raw-header API 直接接受它们。
- QCNetworkContentEncoding.h 在 QCurl::contentencoding 下提供 QString 名称，包括 kGzip/kDeflate/kBrotli/kZstd/kIdentity。自定义头和编码字符串仍开放，头名查找/替换仍忽略大小写，有序响应头仍保留重复字段。
- setAcceptedEncodings 的空列表禁用解码；无编码列表时显式启用 auto-decompression 请求 libcurl 支持的全部算法。raw Accept-Encoding 优先，但不因手写该头自动启用解码；异步冲突警告仍保留。命名常量不保证某个 libcurl 构建实际支持解码。
- Http3Only 额外要求编译头与运行时 libcurl 7.88.0+ 及 HTTP/3 特性，不做降级；Http3 允许回退。能力预检查与无请求边界见[HTTP 版本配置](configuration.md#http-version)。

## 8. WebSocket 与 pool（Other Extras / Preview）

WebSocket 命令检查结构化接受结果；pool 的 acquire/preWarm 消费逐调用 QFuture。使用 LeaseId，在 owner thread 通过 resolveLease 临时取得借用 socket，再用 release(leaseId) 归还；不要跨线程保存 socket 指针。clearPool/setConfig 返回 bool 和可选 QString 错误输出，拒绝不改变池状态。具体结果类型与线程边界见[公开头](../../src/QCWebSocketPool.h)。

## 9. 链接与 ABI

部署从 SOVERSION 1 改为 SOVERSION 2，Core shared artifact 为 libQCurl.so.2.0.0。Blocking Extras 仍通过显式 COMPONENTS BlockingExtras / QCurl::BlockingExtras 消费，但 target 已是由 Core 承载实现的 INTERFACE；不再部署独立 libQCurlBlockingExtras。分组件安装在 Core Runtime/Development 之外只需 BlockingExtrasDevelopment。

Core 在 2.x 内保证源码兼容，**不保证二进制兼容**。不能因为都加载 libQCurl.so.2 就让旧下游二进制使用不同 QCurl 构建；每次更新都重新编译和链接。不提供 v1 alias、wrapper、shim 或迁移窗口。

2.0 不建立 v2 ABI baseline；历史 v1 baseline 不能复制或重命名为 v2 证据。维护者候选验收见[发布操作](../dev/release/release-procedure.md)与[正式合同](../dev/release/2.0.0-hard-break-release-contract.md)，本地 readiness 不创建 tag 或远端发布。
