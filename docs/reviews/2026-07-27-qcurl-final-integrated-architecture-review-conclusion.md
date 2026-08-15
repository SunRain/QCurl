# QCurl 最终综合架构审核结论

> 日期：2026-07-27
>
> 性质：只读架构审核结论
>
> 状态：最终综合版
>
> 范围：Qt6 网络编程、libcurl Qt6 绑定、Qt6/KDE 库工程，以及 Python 到 Qt6/C++ 的移植质量。
> 说明：本文综合初审和逐代码反证复核，取代此前两轮相互独立的结论。本文不代表任何源码、测试或发布门禁已经完成修复。
> 修订说明：覆盖性复核补齐了 HTTP 入口校验、缓存状态资格与重验证、重试退避边界、WebSocket 关闭与输入边界，以及对应的具名发布门禁；优先级和评分不变。

## 一、最终裁决

QCurl 已经具备合格的 Qt6/C++ 网络库基础：RAII、PIMPL、隐式共享、QObject 生命周期、线程检查、libcurl option 适配、功能分层和测试覆盖均已形成体系。它不是简单的 Python 语法翻译。

但当前仍有一个 P0 生命周期缺陷，以及协议默认值、缓存、重试和 WebSocket 的发布级问题。因此：

1. QCurl 不能以当前状态作为无条件稳定版本发布。
2. HTTP Core 不得继续使用未限制的 libcurl 协议默认值。
3. WebSocket 必须保持 Preview，不得晋升为稳定能力。
4. 后续修复只能遵循本文的单一路线，不保留并行所有权模型、第二套网络后端或伪压缩兼容路线。

综合评分为 **70/100**。初审的总体方向正确，但缓存和重试的默认暴露面此前被写得过重；P0 reply/multi 生命周期结论经反证后仍然成立。

## 二、审查基线与限制

- 审查快照为当前 QCurl dirty worktree；其中包含与本结论无关的既有未提交实现和文档，本文不对其作 Git 或源码修改。
- 知识图谱当前记录 517 个文件、3157 个节点、25549 条关系和 208 个测试节点。图谱的无嵌入关键词检索仅用于导航，最终判断以源码与测试代码为准。
- 本轮进行了逐路径源码与测试静态核验，未重新运行构建、CTest、ASan、UBSan 或真实网络环境测试。
- 本文中的“确认”表示源码路径已经闭合；“风险”表示 API 合同允许其发生，但未将调用方违约误报为库内必现故障。

## 三、P0：reply 与 libcurl multi 所有权断裂

### 3.1 已确认的故障链

`src/QCNetworkReply.cpp:147-155` 的 `QCNetworkReply` 析构函数会在 `Running` 或 `Paused` 状态调用 `cancel()`。

`src/private/QCNetworkReplyControls.cpp:34-48` 的 `cancel()` 不直接从 multi 移除 easy handle，而是将 `removeReply()` 排入事件循环，并由 `QPointer<QCNetworkReply>` 决定任务是否执行。析构开始后，该指针会失效，排队清理因此被跳过。

同一调用把状态改为 `Cancelled`。随后 `QCNetworkReplyPrivate` 析构函数在 `src/QCNetworkReply.cpp:52-64` 只处理 `Running` 或 `Paused`，因此不会同步移除。`QCCurlHandleManager` 继而析构并释放 easy handle，而 `QCCurlMultiManager` 仍以该地址作为 `m_activeReplies` 的键。后续 `cleanupEasyHandleLocked()` 仍可能对该失效地址调用 `curl_multi_remove_handle()`。

这构成可达的 invalid-handle/use-after-free 路径。`tests/qcurl/tst_QCNetworkReply.cpp:1172` 的中途销毁测试走 mock，不覆盖真实 multi 传输。

### 3.2 最终所有权合同

成功进入 `curl_multi_add_handle()` 后，easy handle 必须由 multi 传输上下文持有，直到 `curl_multi_remove_handle()` 完成。reply 只能观察该上下文，不能在 multi detach 前销毁 easy handle 或 callback userdata。

不能只移除 `QPointer` 判断，也不能仅把析构中的 queued remove 改为同步 remove。后者在 libcurl callback 栈内仍可能重入。唯一正确路线是让 multi 上下文持有传输资源，并使 reply 析构进入可延迟完成的 detach 状态。

### 3.3 首个证明点与证伪条件

首个证明点：真实 multi 请求在 `readyRead` 或 `downloadProgress` 回调中销毁 reply 后，active handle 数归零，running request 数归零，且 ASan/UBSan 无错误。

证伪条件：任何执行路径仍允许 easy handle 在完成 `curl_multi_remove_handle()` 前析构，或仍允许 libcurl callback userdata 指向已销毁的 reply/private。

## 四、P1：HTTP Core 默认协议没有收口

`src/QCNetworkTransferConfig.cpp:43-44` 默认将 `allowedProtocols` 与 `allowedRedirectProtocols` 保持为 `nullopt`。`src/private/QCNetworkReplyCurlBaseOptions.cpp:263-296` 和 `src/private/QCBlockingCurlRequestSetup.cpp:289-315` 仅在调用方显式设置时才写入 `CURLOPT_PROTOCOLS_STR` 与 `CURLOPT_REDIR_PROTOCOLS_STR`。

项目内 libcurl 文档 `curl/docs/libcurl/opts/CURLOPT_PROTOCOLS_STR.md` 明确：初始请求默认允许全部已编译协议；`CURLOPT_REDIR_PROTOCOLS_STR.md` 明确：重定向默认允许 HTTP、HTTPS、FTP 和 FTPS。

对于 HTTP Core，这不是合理默认值。最终合同为：初始请求与重定向默认都只允许 `http,https`。非 HTTP 协议必须属于独立扩展能力，不能继承 Core 请求入口的默认策略。

Core 请求入口还必须在创建 easy handle 前显式校验 URL scheme，只接受 HTTP/HTTPS；`CURLOPT_PROTOCOLS_STR` 和 `CURLOPT_REDIR_PROTOCOLS_STR` 是第二道防线，不能代替入口合同。

验收至少覆盖：`file`、`ftp`、`ftps` 的初始 URL 在入口层被拒绝；HTTP 到 FTP/FTPS 的重定向被 libcurl 白名单拒绝；HTTP 与 HTTPS 正常工作。

## 五、P1：WebSocket Preview 的异步与协议合同不成立

### 5.1 异步打开实际阻塞 owner thread

`src/QCWebSocket.h:140-151` 将 `open()` 公开为异步接口。`src/private/QCWebSocketLifecycle.cpp:294-297` 只用零延迟定时器排入同一线程，最终在 `performOpen()` 中调用阻塞的 `curl_easy_perform()`。这不是异步连接模型。

### 5.2 手工 permessage-deflate 实现不合法

`finishOpen()` 在 `src/private/QCWebSocketLifecycle.cpp:169-172` 只要本地启用压缩就设定 `compressionNegotiated=true`，没有验证服务端 `Sec-WebSocket-Extensions` 响应。

当前 libcurl WebSocket flags 不包含 RSV1。`src/private/QCWebSocketFrames.cpp:185-218` 却压缩 payload 后仍以普通 `CURLWS_TEXT` 或 `CURLWS_BINARY` 发送，因此不能表示 RFC 7692 的 permessage-deflate 消息。

最终合同：在底层无法表达 RSV1 前，删除手工压缩配置、压缩统计和协商承诺；不以“simplified”形式保留错误协议语义。

### 5.3 partial I/O、关闭和重连不完整

`sendFrame()` 没有处理 `curl_ws_send()` 的 partial success 或 `CURLE_AGAIN`，却直接返回原始长度。libcurl 要求调用方保留剩余 payload 并等待 socket 事件后继续发送。

接收路径在 `src/private/QCWebSocketFrames.cpp:176-179` 对单个 receive chunk 先解压，再由消息拼装逻辑处理。frame 未完整时，这一顺序错误。

`cleanupConnection()` 在 `src/private/QCWebSocketLifecycle.cpp:251-278` 只清 notifier、buffer 和 header，没有关闭或重建连接上下文。远端 close 后重连可能保留 `Closing`，而 `QCWebSocket::open()` 会拒绝该状态。

完整协议合同还必须要求：主动关闭发送 close frame 并等待对端响应或有界超时；文本消息在交付前通过 UTF-8 校验；单 frame、单 message、发送队列和接收缓冲都有明确上限；违反协议或上限时执行可诊断的协议关闭。

最终路线：WebSocket 保持 Preview；连接建立迁移到 multi/socket 驱动或隔离 worker；发送使用可恢复且有界的队列；接收先重组完整 frame，再校验 UTF-8 和处理消息；关闭流程完成 close handshake 或有界超时后重建连接上下文；状态变为可重连状态后才安排实际 reopen。

## 六、P1-high：缓存为 opt-in，但语义不符合 HTTP 缓存合同

缓存必须通过 `QCNetworkAccessManager::setCache()` 显式注入，因此默认影响范围有限。此前将其定为 P0 并建议移出 Core 过重；正确结论是保留 API、修正语义，并把它作为启用缓存时的 Core release blocker。

已确认的问题如下：

1. `src/QCNetworkMemoryCache.cpp:29` 与磁盘缓存只使用 URL 作为 key，忽略 method、`Vary` 与认证分区。
2. `src/private/QCNetworkReplyState.cpp:109-145` 对所有成功 method 自动写入缓存，没有限制 GET/HEAD。
3. `src/QCNetworkCache.cpp:176-215` 把 `max-age=0` 落入“无过期时间”，从而永久 fresh。
4. header 查找只兼容少量固定大小写，不能满足 HTTP header 全面大小写不敏感的要求。
5. `src/QCNetworkDiskCache.cpp:237-273` 用普通 `QFile` 分别写 data 与 metadata，不是原子提交；覆盖同一 key 时又会把新 body 大小直接累计，导致容量统计漂移。
6. 写入与命中流程没有形成完整的可缓存 status、禁止存储指令、stale 条件重验证合同，不能只以“请求成功”替代 RFC 9111 的存储和复用资格判断。

已被反证的表述：缓存写入会在读取 body 后将数据重新 append 回缓冲区，故不会导致调用方 `readAll()` 丢失响应体。

最终合同：仅考虑缓存 GET/HEAD，并继续按 status、请求/响应缓存指令判断是否允许存储；key 包含 method、规范化 URL、`Vary` 维度和认证分区；认证响应默认不得跨认证上下文复用；header 统一大小写不敏感；`max-age=0` 立即 stale；stale 条目只能依据 validator 完成条件重验证后复用；磁盘以 `QSaveFile` 或等价事务式写入 data/meta；覆盖容量按旧条目差额计算。

## 七、P1 条件性：启用重试后缺少幂等保护

`QCNetworkRetryPolicy` 默认 `maxRetries=0`，因此“默认重放 POST/PATCH”不准确。风险来自 `standardRetry()`、`aggressiveRetry()` 和调用方显式启用的策略。

`src/private/QCNetworkReplyRuntime.cpp:81-113` 仅在 `retryHttpStatusErrorsForGetOnly` 被显式开启时限制 HTTP 状态错误；网络错误不受该限制。非幂等 POST/PATCH 因而可以在网络错误下重放。

正面事实：非 seekable body 在 `src/private/QCNetworkReplyCurlBaseOptions.cpp:321-328` 已 fail-fast，`tests/qcurl/tst_QCNetworkStreamUpload.cpp:640-658` 也覆盖该合同。

最终合同：所有错误类型统一经过 method/idempotency gate。GET/HEAD 默认可自动重试；非幂等请求只有在显式允许重放并携带稳定幂等键时才能自动重试。现有仅限 HTTP 状态错误的 GET-only 开关应收敛为统一门禁，而不是继续扩展例外。

退避必须加入有界 jitter，且最终延迟不得超过配置上限。次数与时长 setter 必须拒绝负值，浮点倍率必须拒绝 `NaN` 和无穷值，时长换算及指数退避计算必须检测溢出；非法配置不能静默截断或回绕。

## 八、P2 与 P3：边界明确的工程问题

### 8.1 借用服务生命周期

`src/QCNetworkAccessManager_p.h:87` 保存裸 `QCNetworkCache *`，`logger` 也为裸借用指针。cache 的 public API 已要求调用方保证生命周期，因此这不是库内无条件 UAF；但 cache 是 QObject，应使用 `QPointer` 自动失效。logger 应建立明确的共享所有权入口或严格的调用方生命周期合同。

### 8.2 Blocking Extras 的 short write

`src/private/QCBlockingCurlAdapter.cpp:72-119` 对输出 `QIODevice` 只调用一次 `write()`。短写被立即当作失败，但 API 接受任意 writable device。应循环写入或明确仅支持同步全量写设备，并加入自定义 short-write 设备测试。

该问题只影响 Blocking Extras。`QCNetworkDownloadToDeviceJob` 已经使用循环写满逻辑，不能泛化为全部下载路径缺陷。

### 8.3 全局初始化与统计

`src/CurlGlobalConstructor.cpp:9-21` 只记录 `curl_global_init()` 失败，不向 manager 或 handle 创建路径暴露可查询根因。应提供初始化状态与诊断，但没有证据支持“全局 cleanup 冲突”。

`src/QCWebSocket.cpp:202-227` 使用 `%.1f` 作为 `QString::arg()` 占位符，导致压缩统计文本与参数映射错误。这是明确 P2 bug。

### 8.4 死代码与文件规模

`QCSigningMiddleware` 仅位于 private 内部头和实现，未发现产品调用方。它应删除或标识为测试示例，不应作为主要安全漏洞处理。

超过 400 行的 `QCNetworkReplyExecution.cpp`、`QCNetworkAccessManagerSend.cpp`、`QCNetworkRequestScheduler.h`、`QCBlockingCurlAdapter.cpp`、`QCNetworkSchedulerPolicy.cpp` 与 `QCWebSocket.h` 是职责审查信号，不是单独缺陷。只有在一个文件承载多个独立变化原因时才拆分。

## 九、两轮结论的最终修订

| 分类 | 最终处理 |
|---|---|
| 保留 | reply/multi 生命周期 P0、HTTP 默认协议 P1、WebSocket Preview P1 |
| 降级 | 缓存 P0 改为 P1-high；借用指针改为 P2 条件风险；文件规模改为 P3 维护项 |
| 改写 | 默认重放非幂等请求，改为启用重试后缺少幂等门禁 |
| 删除 | 缓存导致 body 丢失、缓存必须移出 Core、签名中间件是主要安全漏洞、global cleanup 冲突 |
| 新增 | WebSocket 统计格式错误、Blocking Extras short-write、HTTP 入口 scheme 校验、缓存 status/revalidation、重试数值边界、WebSocket close/UTF-8/缓冲上限 |
| 未纳入 | “删除旧 manager multi 字段”未被逐代码复核重新确认，不作为当前整改合同；只有独立证明字段无调用方且无 ABI/行为职责后才能删除 |

## 十、四个架构视角评分

| 视角 | 得分 | 最终判断 |
|---|---:|---|
| Qt6 网络编程 | 64/100 | 事件循环、线程边界和 QPointer 基础较好，但 reply/multi 所有权存在 P0 |
| libcurl Qt6 绑定 | 63/100 | option/RAII 基础可靠，multi 所有权、协议默认值和 WebSocket 尚未闭合 |
| Qt6/KDE 库工程 | 78/100 | PIMPL、隐式共享、导出和功能分层较成熟，借用服务与职责治理仍需收敛 |
| Python 到 Qt6/C++ 移植 | 76/100 | 已采用 Qt/C++ 模型，但缓存与 WebSocket 仍有“先翻功能、后补不变量”的痕迹 |
| **综合** | **70/100** | **基础可继续演进，当前存在发布阻断项** |

## 十一、唯一整改顺序与发布门禁

1. 先修复 reply/multi 所有权，并补真实 multi 中途销毁回归与 sanitizer 证据。
2. 再收紧 HTTP Core 入口与 libcurl 默认协议，并建立统一幂等重试门禁、有界 jitter 和非法配置拒绝规则。
3. 随后修复缓存的 method/status 资格、key、freshness、revalidation、header、认证隔离、持久化原子性和容量统计。
4. 最后重构 WebSocket Preview 的异步、partial I/O、close handshake、重连、UTF-8 和缓冲上限；删除伪 permessage-deflate。
5. 收尾处理 short write、全局初始化状态、服务生命周期、统计格式、死代码和职责型拆分。

在 P0 生命周期门禁通过前，不扩展缓存功能，不提升 WebSocket 状态，不新增第二套 transport abstraction。真实 multi 生命周期测试、跨认证缓存隔离测试、非幂等网络失败测试未通过前，不发布 Core Stable。WebSocket 若不能以真实握手、partial I/O、close handshake、UTF-8、缓冲上限与重连证据证明正确性，就继续保持 Preview 且默认关闭。

发布前必须同时证明：

- reply 销毁不会早于 multi detach 释放 easy handle；真实 multi 回归和 sanitizer 通过。
- Core 入口拒绝非 HTTP/HTTPS scheme，初始 URL 与重定向的 libcurl 协议白名单默认为 HTTP/HTTPS。
- 重试不会自动重放未获显式授权的非幂等请求；退避具有有界 jitter，非法数值和溢出配置被拒绝。
- 缓存只存储合格 method/status，并只复用正确 `Vary` 与认证分区中的新鲜响应；stale 响应经过条件重验证，崩溃中断不会留下半条目。
- 跨认证缓存隔离测试证明不同认证上下文不会复用同一受保护响应。
- WebSocket 仍为 Preview，或其协议、线程、partial I/O、close handshake、UTF-8、缓冲上限与重连测试全部通过。
- 修改后的 GCC/Clang、Qt Test、public API、ABI、consumer smoke、changed-lines format 和 `git diff --check` 全部通过。

## 十二、结论

QCurl 已具备值得继续投入的 Qt6/libcurl 库基础，但尚未达到稳定完成态。后续必须先关闭 reply/multi 生命周期 P0，再处理协议、重试与缓存。WebSocket 不得提前晋升。本文是后续架构修复、测试设计和发布判定的唯一综合基线。
