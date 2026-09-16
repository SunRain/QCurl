# 常见配置

本页说明请求与管理器的常用配置、失败边界和使用前提。先按[Quickstart](quickstart.md)准备 manager、事件循环及独立 consumer；示例片段中的 `manager` 均在其 owner thread 使用。更细的参数与生命周期以公开头注释为准。

## 1. 请求级配置（推荐从 `QCNetworkRequest` 开始）

`QCNetworkRequest` 是当前唯一的 public request 配置入口，覆盖常用请求参数配置（详见 `src/QCNetworkRequest.h`）：

- 请求头：`setRawHeader(...)` 按大小写无关字段名替换，`rawHeader(...)` 也忽略字段名大小写；
  多次设置同名字段只保留最后一个值，不表达有序重复字段。
- 超时：`setTimeout(...)` / `setTimeoutConfig(...)`
- 代理：`setProxyConfig(...)`
- TLS/SSL：`setSslConfig(...)`
- HTTP 版本：`setHttpVersion(...)`
- 优先级：`setPriority(...)`
- 重试策略：`setRetryPolicy(...)`
- 认证：`setHttpAuth(...)`
- 上传：
  - raw body：`QCNetworkAccessManager::post(..., QIODevice *, sizeBytes)` / `put(..., QIODevice *, sizeBytes)`
  - 单文件 multipart：`QCNetworkMultipartBody::fromSingleFileDevice(device, ...)` + `post(..., QIODevice *, sizeBytes)`

上传入口的关键合同：

- raw-body `QIODevice *` 是借用对象，调用方持有所有权，并负责保持到 reply 结束。
- device overload 必须从 manager 所在线程调用，且 `device->thread()` 必须与 manager/reply 线程一致；异步请求还需要该线程存在 Qt event loop。
- raw-body 从调用时的当前 `pos()` 开始读；需要重定向、重试或认证协商重发 body 时，设备必须能 seek 回起点。
- POST 未知长度只支持 HTTP/1.1 chunked raw-body；PUT 必须有已知长度或可从 seekable device 推导。
- async raw-body 可以通过 `readyRead()` 从 source-not-ready 恢复；sync raw-body 遇到 `read() == 0 && !atEnd()` 会失败。
- `QCNetworkMultipartBody::fromSingleFileDevice(device, ...)` 在构造阶段只校验 source device、长度和 seek 能力；返回空值表示构造失败并通过 `error` 给出原因。发送阶段仍要求 source device、wrapper device、manager/reply 在同一线程，并拒绝 unknown-size/sequential source。

代理默认行为：

- 未设置 `setProxyConfig(...)` 或显式设置 `QCNetworkProxyConfig::ProxyType::None` 时，请求不会继承 libcurl 的 `HTTP_PROXY` / `HTTPS_PROXY` 等环境代理。
- 需要代理时，必须显式设置 `QCNetworkProxyConfig` 的类型、地址、端口和认证信息。

### 内存 body 与 Multipart

`QCNetworkBody` 生成 JSON/form-urlencoded body，并保存匹配的 Content-Type。传给
manager 的 post/put/patch 时，仅在请求未显式设置 Content-Type 的情况下自动补齐，
不会覆盖调用方设置。form-urlencoded 可保留同名参数：

```cpp
#include <QCNetworkBody.h>

auto body = QCurl::QCNetworkBody::fromFormUrlEncoded(
    QList<QPair<QString, QString>>{
        {QStringLiteral("tag"), QStringLiteral("one")},
        {QStringLiteral("tag"), QStringLiteral("two")},
    });
auto *formReply = manager.post(request, body);
```

Core 的 `QCMultipartFormData` 负责字段构建，再通过 `QCNetworkMultipartBody` 生成内存 body，
发送仍走 manager；下面片段使用纯文本字段，不依赖本机文件：

```cpp
#include <QCMultipartFormData.h>
#include <QCNetworkMultipartBody.h>
#include <QCNetworkHttpHeaders.h>

QCurl::QCMultipartFormData form;
form.addTextField("userId", "12345");
const auto body = QCurl::QCNetworkMultipartBody::fromFormData(form);
request.setRawHeader(QCurl::httpheaders::kContentType, body.contentType());
auto *uploadReply = manager.post(request, body.data());
```

内存 builder 也可添加文件字段；大文件改用上述借用设备或单文件 streaming body，
不要把整个文件装入内存。reply 的连接、读取和释放沿用 Quickstart。

### 值语义与 `operator==`

- `QCNetworkRequest` 是值语义配置对象，但 `operator==` 当前只比较 URL、follow redirect、raw headers、Range、HTTP version 与 `lane`。
- `sslConfig()` / `proxyConfig()` / `timeoutConfig()` / `retryPolicy()` / `httpAuth()` 以及 `priority`、cache 等执行配置族 **不参与** `operator==`。
- 如果你需要判断“完整执行配置是否一致”，请分别读取这些 config family，而不要把 `operator==` 当作全量 diff。

### 调度

请求的 `setPriority()` / `setLane()` 与 manager 的 policy 配合使用；优先级不抢占运行中的请求。lane、reservation、通知及取消统一见[调度合同](lane-scheduler.md)。

## 2. 管理器级配置（统一策略与复用）

当你需要在全局层面统一策略（如日志、中间件、连接池/调度器等），建议通过 `QCNetworkAccessManager` 管理与注入（入口定义见 `src/QCNetworkAccessManager.h`）。

日志器使用 opaque handle 注入。共享控制块与最终删除留在 QCurl 库内；manager 和已经创建的
reply 会各自保留 handle 快照，因此替换 manager 当前 logger 不会改变既有请求的日志目标：

```cpp
#include <QCNetworkDefaultLogger.h>

QCurl::QCNetworkDefaultLogger *implementation = nullptr;
auto logger = QCurl::QCNetworkLoggerHandle::createWithBorrow(&implementation);
implementation->enableConsoleOutput(true);
manager.setLogger(logger);
```

`implementation` 是非 owning 借用，只能在至少一个同控制块 handle 存活期间使用。文件输出配置和
日志写入均返回 `[[nodiscard]] QCNetworkLogResult`，调用方必须检查失败或显式声明丢弃。

内建 Qt 日志、请求调试输出、内建 logger 与 trace 统一隐藏敏感查询值；URL 用户信息也不进入
常规 URL 诊断。重定向地址与 Basic-over-HTTP 警告同样脱敏，不依赖是否注入 logger。

## 3. HTTP、重试和缓存边界

<a id="http-version"></a>
### HTTP 版本与 HTTP/3

HTTP 版本是请求级偏好，QCurl 将其交给 libcurl，不保证每个环境都使用 HTTP/3。

| 配置 | 协商与拒绝边界 |
| --- | --- |
| `HttpAny` | 不表达偏好，由 libcurl 自动协商 |
| `Http3` | 优先尝试 HTTP/3，允许按 libcurl 能力和服务端协商降级 |
| `Http3Only` | 只接受 HTTP/3；能力或服务端不满足时失败，不静默降级 |

最低 libcurl 仍为 7.85.0；`Http3Only` 额外要求编译头与运行时均达到 7.88.0，且运行时带 HTTP/3/QUIC 支持。缺少 `CURL_HTTP_VERSION_3ONLY` 不能改用可降级的 Http3。能力预检查拒绝时，**Core 返回 InvalidRequest，Blocking Extras 返回 UnsupportedCapability，均保留诊断且不发送请求**。协商阶段的传输失败另按相应执行器报告。

在 Quickstart 的请求创建后、`manager.get()` 之前设置：

```cpp
#include <QCNetworkHttpVersion.h>

request.setHttpVersion(QCurl::QCNetworkHttpVersion::Http3); // 业务允许降级。
```

若业务只接受 HTTP/3，改为 `QCNetworkHttpVersion::Http3Only`。服务端也必须支持 QUIC，网络须允许 UDP/443；只看到 curl 命令行工具宣称 HTTP3 还不能证明 QCurl 加载的 libcurl 是同一个构建。能力验证和严格 HTTP/3 专题见[构建与测试](../dev/build-and-test.md#libcurl-consistency)。

HTTP/3 不一定更快，首次握手、UDP 可达性和服务端实现都会影响结果；性能判断需同环境多次采样，见[性能回归](../dev/performance.md)。

### TLS 与代理

默认保持证书链与主机名验证。通过 `QCNetworkSslConfig` 设置 CA、客户端证书与 TLS 范围，再交给 `request.setSslConfig()`；不要用关闭校验作为生产修复。

代理通过 `QCNetworkProxyConfig` 显式设置，支持 HTTP/HTTPS/SOCKS；HTTPS proxy 的 TLS 配置与目标站点 TLS 配置分开，清除代理 TLS 使用 `clearTlsConfig()`。默认 None 禁止隐式环境代理。

### HTTP 方法、重试与缓存

Core 请求入口只接受 `http` 和 `https` scheme；初始请求与重定向都会使用 `http,https` 协议白名单。`file`、`ftp`、`ftps` 不属于 Core HTTP 路径。

Core 与 Blocking Extras 的 `sendCustomRequest()` 都原样发送合法 HTTP method token，
不把大小写转换当作校验。Core 在所属线程且有事件循环时排队启动或报告快速失败；返回后
连接 `finished()` / `error()` 即可观察终态，reply 由 manager 持有。无事件循环、错误线程调用
则同步返回可查询的失败结果；错误线程返回的无 parent reply 由调用线程释放。

重试默认关闭（`maxRetries = 0`）。启用后，GET/HEAD 是默认允许自动重试的方法；其他 method 需要显式选择
`QCNetworkRetryMethodPolicy::AllowExplicitIdempotencyKey`，并在 immutable request snapshot 中提供稳定的
`Idempotency-Key`。网络错误和 HTTP 状态错误共用该门禁。负数、`NaN`、无穷值以及会溢出的 setter 输入会被拒绝并保留旧值，退避使用有界 equal-jitter。

响应体一旦被 `readAll()` 或通用设备下载任务消费，失败后不再透明重试；可 seek 的借用设备也
没有回滚保证。文件下载任务只在独占 writer 能恢复本次输出时重试：失败覆盖不替换已有文件，
追加先截回原长度；恢复失败则终止。续传 `206` 必须使用 identity 表示，范围、响应长度与最终
文件长度一致；服务端忽略 Range 的 `200` 安全覆盖和匹配本地长度的 `416` 完成语义保持不变。

缓存仍是显式 `setCache()` 注入的 Core 能力，不默认启用。缓存请求键包含 method、规范化 URL、参与 `Vary` 的请求头和
调用方提供的 `cachePartitionKey`；带认证身份但没有分区的响应不得存储。当前稳定存储范围是 GET 200，HEAD 只更新已有 GET
条目的元数据；`no-store` 不落盘，`no-cache` / `max-age=0` 必须先条件重验证。磁盘缓存使用当前有界格式；旧版或未知格式不复用为有效命中。`clear()` 返回结构化状态、删除/失败
计数和残留字节，调用方不得忽略部分失败。

无缓存或 `OnlyNetwork` 路径不保留已消费响应的缓存副本。允许缓存的 GET 响应使用现有
`maxCacheSize()` 约束收集量；超过容量即放弃缓存，但网络数据交付不受影响。

### 连接池配置与请求统计

`QCNetworkConnectionPoolManager::setConfig()` 只提交线程安全的进程模板。每个线程的 multi
在下一次网络请求加入前应用完整快照，不表示其他线程立即生效，也不打断已有传输。
`multiMaxTotalConnections` / `multiMaxHostConnections` 是每线程原生连接上限，
`multiMaxConcurrentStreams` 是每连接并发流上限，`multiMaxConnects` 是连接缓存容量；清除后
分别恢复 libcurl 默认值 `0`、`0`、`100`、`0`，不是进程级总连接配额。

`maxIdleTime` / `maxConnectionLifetime` 限制可复用连接的空闲年龄与总年龄；
TCP keepalive 固定启用，空闲 60 秒后探测，间隔 30 秒。`multiplexingEnabled` 控制原生
多路复用，不改写请求 HTTP 版本。无效的重复上限、HTTP/1.1 pipelining 与 warming 开关已移除。

统计中的 `activeRequests()` 是进入 multi 且未结束的 Core 请求数（包含重试退避），
`totalRequests()` 是已结束请求数（含失败、取消、销毁，不含缓存或 mock）；
`reusedConnections()` 只统计已取得响应且可确认复用连接的请求，不提供推算的空闲连接数。
`resetStatistics()` 清空历史累计量，但保留仍在运行的活动请求数。

## 4. WebSocket 配置

WebSocket 的 public 配置入口集中在以下头文件注释：

- `src/QCWebSocket.h`：连接生命周期、`QCWebSocketOptions`、`close(...)` / `ping(...)` / `pong(...)`
- `src/QCWebSocketReconnectPolicy.h`：重连次数、指数退避参数、可重连 close code 集合

常见边界可先关注：

- `QCWebSocket` 构造时必须传入 `QCWebSocketOptions`；连接超时、TLS、重连、自动 Pong、关闭超时和缓冲上限都通过 options 配置
- `QCWebSocket::open()` 在 `Closed` 状态下允许再次调用；只有 `Connecting / Connected / Closing` 状态会拒绝并发 `open()`
- `close(reason)` 的 `reason` 最大 123 字节
- `ping(...)` / `pong(...)` 的 payload 最大 125 字节
- `setOptions(...)` 必须在下一次 `open()` 前调用；`Closed` 状态可为重连前重新配置，`Connecting / Connected / Closing` 阶段会拒绝修改并保留旧值
- `maxFrameBytes`、`maxMessageBytes`、`maxPendingSendBytes`、`maxReceiveBufferBytes` 必须为正值且不超过 256 MiB；非法设置返回 `false` 并保留旧值
- `closeHandshakeTimeout` 必须为正值；超时后连接会执行有界 teardown
- WebSocket 属于 Other Extras 组件并标记为 Preview，默认关闭；握手复用现有 multi 驱动，不使用 owner-thread 阻塞传输或独立 worker transport
- 发送队列、frame/message/receive buffer 都有硬上限，partial send 和 `CURLE_AGAIN` 会等待后续事件继续；文本消息在交付前完成 UTF-8 校验
- WebSocket Preview 不提供 `permessage-deflate`；底层无法表达 RSV1 时不会模拟压缩语义

示例目录（`examples/WebSocketDemo/`、`examples/WebSocketPoolDemo/`）只演示典型用法，不定义参数合同。

## 5. Blocking Extras 的响应边界

显式启用 BlockingExtras 后，`QCBlockingRequestOptions::maxInMemoryBodyBytes()` 限制内存响应，
超限返回 BodyTooLarge。大响应使用 `downloadToDevice()` 写入调用方设备，此时 body 为空，
bytesReceived 记录实际字节。InputDeviceError、OutputDeviceError、ReplayNotSupported 等错误
应作为主判断；diagnosticCurlCode 只辅助定位，不能代替结果状态。

同步 cookie 使用 `QCBlockingCookieStore` 的 snapshot/delta，不跨线程访问 live manager
cookie store。设备线程、重放能力与详细结果约束见[Blocking 公开头](../../src/QCBlockingNetworkClient.h)。

## 6. 排障与常见问题

建议优先：

1. 开启/查看日志（如你已启用日志或中间件）
2. 对照 [性能回归](../dev/performance.md) 与相关模块文档
3. 复现最小样例后再上报问题（见[支持与反馈](../../SUPPORT.md)）
