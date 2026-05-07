# QCurl RC 成熟度审查与复核结论

> 日期：2026-05-06
> 范围：架构、代码、公共 API、ABI、Qt/libcurl/KDE 应用库实践和下游使用风险。
> 不讨论：CI/CL。

## 1. 最终结论

QCurl 不应以“源码树全部功能均成熟稳定”的口径进入 RC。

当前发布判断已经进入 **3.0 Core public surface reset**：

- **Core install surface 是本次 `3.0.0-rc.1` 的默认稳定候选。**
- **P0 `QCNetworkCachePolicy.h` 已作为 Core type header 独立提升。**
- **P1 `QCNetworkDefaultLogger.h` 与 `QCNetworkCancelToken.h` 已作为低风险 Core helper 提升。**
- **P2 Cache lookup concrete API 与 Multipart builder 已完成 public API 重塑并进入 Core。**
- **P3 Middleware base、MockHandler Core Test Support、ConnectionPool 管理面已完成 public API 重塑并进入 Core 默认安装面。**
- **WebSocket 与 Diagnostics 全量能力继续作为 Preview，不进入本次 Core reset。**

README 中展示的其他能力必须按 Core / Core Test Support / Preview 明确分层，避免把源码树能力误读为默认稳定承诺。

## 2. RC 范围

### 2.1 Core：RC 候选范围

Core 以 `src/CMakeLists.txt` 中的 `QCURL_INSTALL_HEADERS` 加生成头 `QCurlConfig.h` 为准。

当前 Core 头包括：

```text
QCGlobal.h
QCNetworkAccessManager.h
QCNetworkRequest.h
QCNetworkSslConfig.h
QCNetworkProxyConfig.h
QCNetworkTimeoutConfig.h
QCNetworkHttpMethod.h
QCNetworkHttpVersion.h
QCNetworkError.h
QCNetworkReply.h
QCNetworkRetryPolicy.h
QCNetworkRequestPriority.h
QCNetworkRequestScheduler.h
QCNetworkCachePolicy.h
QCNetworkCache.h
QCNetworkMemoryCache.h
QCNetworkDiskCache.h
QCMultipartFormData.h
QCNetworkLogger.h
QCNetworkDefaultLogger.h
QCNetworkCancelToken.h
QCNetworkConnectionPoolConfig.h
QCNetworkConnectionPoolManager.h
QCNetworkMiddleware.h
QCNetworkMockHandler.h
```

证据：

- `src/CMakeLists.txt:112` 明确默认稳定安装面只包含 Core contract。
- `src/CMakeLists.txt:115` 定义 `QCURL_INSTALL_HEADERS`。
- `docs/arch/public-header-boundary.md:7` 定义 install surface 的 SSOT。

### 2.2 Core 头中的 promoted 入口边界

Core 头中存在若干从 Stable Extras 候选提升而来的入口方法。进入默认安装面后，这些入口必须有对应 public header、public-api gate 和 installed consumer smoke 证据。

当前需要明确边界的入口包括：

- `QCNetworkRequest::setCachePolicy(...)` / `cachePolicy()` 暴露 `QCNetworkCachePolicy`，该轻量 type header 已独立进入 Core。
- `QCNetworkAccessManager::setCache(...)` / `cache()` 依赖的 `QCNetworkCache` 已进入 Core。
- `QCNetworkAccessManager::postMultipart(...)` 依赖的 `QCMultipartFormData` 已进入 Core。
- `QCNetworkAccessManager::addMiddleware(...)` / `middlewares()` 依赖的 `QCNetworkMiddleware` 已作为 Middleware base 进入 Core。
- `QCNetworkAccessManager::setMockHandler(...)` / `mockHandler()` 依赖的 `QCNetworkMockHandler` 已作为 Core Test Support 进入默认安装面。

发布含义：

- Core consumer 可以 include 默认安装头并构建。
- Core consumer 可以完整使用 Cache lookup concrete API、Multipart builder、Middleware base 和 ConnectionPool 管理面。
- MockHandler 是默认安装的 Core Test Support；文档不得把它表达为生产运行时 Core 能力。
- WebSocket 与 Diagnostics 仍是 Preview；它们不属于默认稳定 Core 合同。

### 2.3 Promotion 波次与剩余候选

以下能力按 breaking Core reset 分波次推进。已经完成的波次进入默认安装面；未完成的波次不得只靠移动头文件清单进入 Core。

| 波次 | 能力 | 头文件 | 当前判断 | 后续前置 |
| --- | --- | --- | --- |
| P0 已完成 | Cache policy type header | `QCNetworkCachePolicy.h` | 已进入 Core；installed consumer 覆盖 `QCNetworkRequest::setCachePolicy()` / `cachePolicy()` | 后续不得把 concrete cache API 混入该轻量 type header |
| P1 已完成 | Default logger | `QCNetworkDefaultLogger.h` | 已进入 Core helper；consumer smoke 覆盖默认 logger 实例化、manager 绑定和 entries accessor | 后续扩展不得暴露文件轮转状态、锁对象或 public fields |
| P1 已完成 | Cancel token | `QCNetworkCancelToken.h` | 已进入 Core helper；consumer smoke 覆盖 reply-level `attach(QCNetworkReply *)` / `attachMultiple(QList<QCNetworkReply *>)` / timeout / cancel | 不得新增 request-level cancel shortcut 作为本轮验收替代 |
| P2 已完成 | Cache lookup concrete API | `QCNetworkCache.h`、`QCNetworkMemoryCache.h`、`QCNetworkDiskCache.h` | 已进入 Core；`lookup(url, ReadMode)`、`Miss/FreshHit/StaleHit` 进入 installed consumer smoke，`Metadata` / `LookupResult` 已改为 accessor-only shared-data 值类型 | 不得恢复旧 `contains()` / `data()` / `metadata()` 读路径，不得新增 layout allowlist |
| P2 已完成 | Multipart | `QCMultipartFormData.h` | 已进入 Core builder；class layout 已改为 shared-data，installed consumer 覆盖 builder API | 后续 streaming 大文件路径仍通过 `postMultipartDevice()` 维持独立合同 |
| P3 已完成 | Middleware base | `QCNetworkMiddleware.h` | 已进入 Core；manager 执行链路和 installed consumer smoke 覆盖 add/remove/middlewares 路径 | 后续不得把内置 middleware 实现细节混入 base contract |
| P3 Core Test Support 已完成 | Mock handler | `QCNetworkMockHandler.h` | 已进入默认安装面；`CapturedRequest` 已改为 accessor-only shared-data 值类型，consumer smoke 覆盖请求捕获和 mock response | 文档必须持续标注 Test Support，不得表达为生产运行时 Core 能力 |
| P3 已完成 | Connection pool 管理面 | `QCNetworkConnectionPoolConfig.h`、`QCNetworkConnectionPoolManager.h` | 已进入 Core；config/statistics 改为 accessor/shared-data，manager 成员布局下沉到 private data | 后续扩展只能新增 accessor 或 private data 字段，不得恢复 public fields |

证据：

- `tests/qcurl/CMakeLists.txt` 已注册 Cache、Multipart、Middleware、CancelToken、Mock、Diagnostics local 等测试。
- `qcurl_cache_extras_header_self_compile` 已为 Cache first-include 提供补充证据。
- `public-api` / `public-api-slow` 已覆盖 Cache、Multipart、Middleware、MockHandler、ConnectionPool 的 Core installed consumer 路径。
- `docs/arch/public-header-boundary.md` 明确 Preview 不进入默认 public-api / consumer smoke 合同。

### 2.4 Preview：不进入本次稳定承诺

除上节 Core / Core Test Support 能力外，以下能力应继续归为 Preview：

- WebSocket：已有实现和测试资产，但缺少与 Core 等价的独立 install surface、consumer smoke 和 ABI 承诺；连接阶段仍存在阻塞语义，接收侧存在 polling fallback。
- Diagnostics 全量能力：`resolveDNS()`、local HTTP probe 等有 deterministic gate；但 `ping()`、`traceroute()` 依赖外部命令或权限，`DiagResult::details` 也不是稳定 schema。
- 未绑定 benchmark 版本、依赖版本、网络环境和日期的性能数字。

证据：

- `src/CMakeLists.txt:132` 将这些头归入非默认 Extras。
- `docs/arch/public-header-boundary.md:20` 明确 WebSocket 不属于默认稳定安装面。
- `README.md` 已按 Core / Core Test Support / Preview 说明 WebSocket、Multipart、Cache 等能力的发布层级。

## 3. 复核修正

初次审查结论的大方向成立，但复核后需要调整几处措辞。

### 3.1 WebSocket

原表述偏强：“WebSocket 不能 RC”。

修正后：

> WebSocket 当前不应随 Core 一起作为默认稳定合同发布；可以作为 Extras / Preview 发布。

原因：

- WebSocket 已有实现和测试资产。
- WebSocket 当前不属于默认稳定安装面。
- WebSocket 还缺少与 Core 等价的独立 install surface、consumer smoke 和 ABI 承诺。
- 当前连接阶段在 `QTimer::singleShot` 回调中执行阻塞的 `curl_easy_perform()`；接收侧存在 50ms polling fallback。

证据：

- `src/QCWebSocket.cpp:413` 调用 `curl_easy_perform()` 建立连接。
- `src/QCWebSocket.cpp:754` 尝试启用事件驱动接收。
- `src/QCWebSocket.cpp:783` polling fallback 每 50ms 轮询。

### 3.2 公开 config struct

原表述偏强：“`ShareHandleConfig` / `HstsAltSvcCacheConfig` 是 RC blocker”。

修正后：

> 它们是 ABI 风险，但不是绝对 RC blocker。

可接受条件：

- 若 3.x 周期承诺不增删字段、不重排字段，可以带着风险进入 Core RC。
- 若后续仍计划扩展字段，应在 RC 前改为 shared-data / accessor-only 值类型。

证据：

- `src/QCNetworkAccessManager.h:119` 暴露 `ShareHandleConfig`。
- `src/QCNetworkAccessManager.h:132` 暴露 `HstsAltSvcCacheConfig`。
- `tests/public_api/public_api_layout_allowlist.txt:8` 临时放行这两个 nested struct 的 ABI/layout 暴露。

### 3.3 DefaultLogger / CancelToken

复核结论：

> `QCNetworkDefaultLogger.h` 与 `QCNetworkCancelToken.h` 已作为 P1 低风险 Core helper 纳入默认安装面。

原因：

- 二者已经从 `QCURL_INSTALL_HEADERS_EXTRAS` 移入 `QCURL_INSTALL_HEADERS`。
- `QCNetworkDefaultLogger.h` 使用 private data，文件输出、内存缓存和 mutex 均留在 `.cpp`。
- `QCNetworkCancelToken.h` 是 QObject service + private data，公开合同限定为 reply-level attach / cancel / timeout。
- installed consumer smoke 覆盖 DefaultLogger 与 CancelToken 的最小使用路径。

### 3.4 Cache

复核结论：

> Cache lookup concrete API 已完成 P2 promotion，当前属于 Core。

原因：

- `QCNetworkCachePolicy.h` 已作为 P0 Core type header 独立提升。
- `lookup(url, ReadMode)` 已成为新的缓存读取方向。
- `FreshOnly` / `AllowStale` 与 `Miss` / `FreshHit` / `StaleHit` 已进入测试。
- `QCNetworkCacheMetadata` / `QCNetworkCacheLookupResult` 已改为 accessor-only shared-data 值类型。
- `QCNetworkMemoryCache.h` / `QCNetworkDiskCache.h` 已把 `QCache`、`QMutex`、`QDir` 等实现依赖下沉到 `.cpp`。
- installed consumer smoke 覆盖 `QCNetworkCache`、`QCNetworkMemoryCache`、`QCNetworkDiskCache` 和 canonical lookup accessor API。

### 3.5 Multipart

复核结论：

> `QCMultipartFormData.h` 已完成 P2 promotion，当前属于 Core builder。

原因：

- `QCMultipartFormData` 已改为 shared-data 值类型，字段列表、boundary 缓存、MIME 与文件系统 helper 都留在 `.cpp`。
- `QCMultipartFormData.h` 已从 `QCURL_INSTALL_HEADERS_EXTRAS` 移入 `QCURL_INSTALL_HEADERS`。
- installed consumer smoke 覆盖 `setBoundary()`、`addTextField()`、`addFileField()`、`contentType()`、`size()`、`toByteArray()` 和 `fieldCount()`。

## 4. 风险清单

### 4.1 发布口径风险

README 之前强调“企业级能力”和“完整协议支持”，包括 Middleware、Diagnostics、Mock 和 WebSocket。

当前 README 已按 Core / Core Test Support / Preview 分层。后续风险来自新文档或示例再次把 Preview 能力写成默认稳定 API。

建议：

- README 保持 Core / Core Test Support / Preview 分区。
- 快速开始默认只展示 Core API。
- WebSocket、Diagnostics 等示例统一标注 Preview；MockHandler 示例统一标注 Core Test Support。

### 4.2 ABI 风险

Core 已经采用较多 ABI 友好策略：值类型大多使用 `QSharedDataPointer`，QObject / runtime service 使用 d-pointer，public header boundary 有自动扫描与 consumer smoke。

剩余主要风险是 `QCNetworkAccessManager::ShareHandleConfig` 和 `QCNetworkAccessManager::HstsAltSvcCacheConfig`。它们目前是公开 struct。KDE C++ 库实践中，公开数据成员会限制后续二进制兼容演进。

若未来扩大 Preview 稳定范围，还需要额外处理 `DiagResult` 中 `QVariantMap details` 的 schema 承诺。

建议：

- RC 前改成 accessor-only shared-data 值类型。
- 或明确写入 3.x ABI 冻结规则：字段布局不再变化。

### 4.3 WebSocket 成熟度风险

HTTP Core async 路径采用 libcurl multi socket 模式，方向正确。

WebSocket 路径不同：使用 `CURLOPT_CONNECT_ONLY` + `curl_easy_perform()` 建立连接；建连后通过 socket notifier 或 timer polling 接收；fallback 到 50ms polling 时延迟较高。

这说明 WebSocket 可以作为可用功能发布，但还不适合作为默认稳定网络栈合同。

建议：

- 保留 WebSocket 为 Extras / Preview。
- 若要把 WebSocket 提升为 Stable，需要建立独立 public-api gate、consumer smoke、ABI 检查和性能/延迟合同。

### 4.4 Core Test Support 交界风险

`QCNetworkMockHandler` 已随默认安装面发布，但它的身份是 Core Test Support。下游可以在测试程序中使用它，但生产运行时文档不应把 mock 能力写成 Core 网络栈能力。

建议：

- README 和架构文档持续标注 MockHandler 的 Test Support 身份。
- 新增 mock 示例时放在测试支持章节，避免出现在生产请求路径示例中。

### 4.5 错误语义风险

`QCNetworkAccessManager::downloadFile()` 的文档说明，写文件失败只输出 `qWarning()`，不会把 reply 转为错误状态。

下游可能只检查 `reply->error()`，误以为文件已经成功保存。

建议：

- RC 前调整为可观测错误。
- 或将该 API 标注为 convenience API，并要求下游自行验证文件结果。

## 5. 正面证据

### 5.1 Core 下游安装面验证通过

复核运行：

```bash
ctest --test-dir build -L "^public-api$" --output-on-failure
ctest --test-dir build -L "^public-api-slow$" --output-on-failure
```

结果：

- `public-api`：2/2 通过。
- `public-api-slow`：4/4 通过。

覆盖：public header 自编译、安装头规则扫描、staging install、installed headers 集合校验、CMake export contract、隔离 consumer smoke、private header negative consumer 检查。

### 5.2 Core HTTP 架构方向正确

`QCCurlMultiManager` 使用 per-thread `CURLM`，避免同一 multi handle 跨线程并发使用。它设置 `CURLMOPT_SOCKETFUNCTION`、`CURLMOPT_SOCKETDATA`、`CURLMOPT_TIMERFUNCTION`、`CURLMOPT_TIMERDATA`，并通过 `QSocketNotifier` 与 `QTimer` 接入 Qt 事件循环。

证据：

- `src/QCCurlMultiManager.cpp:229` 使用 thread-local multi manager。
- `src/QCCurlMultiManager.cpp:250` 设置 socket callback。
- `src/QCCurlMultiManager.cpp:261` 设置 timer callback。
- `src/QCCurlMultiManager.cpp:1419` 使用 `QSocketNotifier` 监听 socket I/O。

### 5.3 libcurl share handle 有锁回调

share handle 初始化时设置 `CURLSHOPT_USERDATA`、`CURLSHOPT_LOCKFUNC`、`CURLSHOPT_UNLOCKFUNC`。

证据：

- `src/QCCurlMultiManager.cpp:480`
- `src/QCCurlMultiManager.cpp:487`
- `src/QCCurlMultiManager.cpp:494`

这符合 libcurl 对 share interface 的线程安全要求。

### 5.4 线程合同有明确文档

Scheduler 和 manager 线程约束已经写入公开 contract：scheduler / reply / multi manager 必须位于同一 owner thread；async 请求要求 owner thread 有 Qt 事件循环；跨线程返回值接口使用 `BlockingQueuedConnection`；`manager.scheduler()` 是 owner-thread only。

证据：

- `src/QCNetworkRequestScheduler.h:43`
- `src/QCNetworkAccessManager.h:423`

## 6. 对照实践

### 6.1 Qt6 网络与线程实践

Qt 文档要求网络对象遵循 QObject 线程归属；queued signal 和 posted event 在对象所属线程执行；timer、socket notifier 和 `deleteLater()` 依赖线程事件循环。

QCurl Core 的线程 contract 与这些原则一致。

参考：

- <https://doc.qt.io/qt-6/qnetworkaccessmanager.html>
- <https://doc.qt.io/qt-6/threads-qobject.html>
- <https://doc.qt.io/qt-6/qobject.html>

### 6.2 libcurl Qt binding 实践

libcurl 的事件驱动 multi socket 模式依赖 `CURLMOPT_SOCKETFUNCTION`、`CURLMOPT_TIMERFUNCTION` 和 `curl_multi_socket_action()`。

libcurl thread safety 文档要求同一个 handle 不被多个线程同时使用。share interface 需要使用者提供 lock/unlock callback。

QCurl Core 的 `QCCurlMultiManager` 满足这些方向。

参考：

- <https://curl.se/libcurl/c/curl_multi_socket_action.html>
- <https://curl.se/libcurl/c/CURLMOPT_SOCKETFUNCTION.html>
- <https://curl.se/libcurl/c/CURLMOPT_TIMERFUNCTION.html>
- <https://curl.se/libcurl/c/threadsafe.html>

### 6.3 Qt6 / KDE 应用库开发实践

KDE Binary Compatibility 文档强调 C++ 库不能随意新增数据成员；d-pointer 是维护二进制兼容的重要技术；public/protected 访问应通过 getter/setter，而不是公开数据字段；优先使用 `FooPrivate` 形式的 private class。

QCurl Core 大体符合该方向，但两个公开 config struct 仍需明确冻结或迁移。

参考：

- <https://community.kde.org/Policies/Binary_Compatibility_Issues_With_C%2B%2B>

## 7. RC 前建议

### 7.1 必做

[x] 保持 README 的 Core / Core Test Support / Preview 三段式发布说明。

[x] 明确 `3.0.0-rc.1` 只承诺 Core install surface。

[x] 给每个默认安装头增加 Core / Core Test Support 状态标签，Preview 头继续留在非默认 Extras。

[ ] 决定 `ShareHandleConfig` / `HstsAltSvcCacheConfig` 的处理方式：迁移为 ABI 友好值类型，或写入 3.x 布局冻结承诺。

[ ] 调整或标注 `downloadFile()` 的文件写入错误语义。

[x] 明确 promoted 类型入口与 Preview 的 consumer 边界。

### 7.2 应做

[ ] 为 WebSocket 建立独立 consumer smoke。

[ ] 若后续恢复 Stable Extras，为 Extras 建立独立 install manifest，避免和 Core 混用。

[x] 为本轮 promoted 候选补充 self-compile、staging install 与 isolated consumer smoke。

[ ] 对 WebSocket 建立连接阶段阻塞语义说明，或调整到更一致的异步模型。

[ ] 对 Core API 生成下游迁移指南，说明 3.0.0 的 breaking changes。

### 7.3 可后续处理

[ ] 拆分过大的核心实现文件，例如 `QCNetworkReply.cpp`、`QCNetworkRequestScheduler.cpp`、`QCCurlMultiManager.cpp`。

[ ] 将文档中的性能数字与具体 benchmark 版本、环境和日期绑定。

[ ] 为 Preview / 未来 Extras 的稳定化建立分阶段计划。

## 8. 建议发布口径

```text
QCurl 3.0.0-rc.1 发布 Core public surface reset 候选版本。

本 RC 的稳定承诺范围仅包含默认安装面：
QCNetworkAccessManager、QCNetworkRequest、QCNetworkReply、TLS/proxy/timeout/retry 配置、scheduler、cache policy type header、Cache lookup API、Multipart builder、logger、default logger、cancel token、Middleware base、ConnectionPool 管理面与相关 Core type headers。

QCNetworkMockHandler 作为 Core Test Support 随默认安装面发布，不作为生产运行时 Core 能力表述。

WebSocket、Diagnostics 全量能力和未绑定证据的性能数字作为 Preview 对待。
```

## 9. 审查结论

审查结论：当前项目的 **Core public surface reset 可以进入分阶段 RC 候选推进**。

审查结论：当前项目的 **全部功能集合不应整体进入成熟库 RC 阶段**。

审查结论：上次 RC 成熟度审查结论主体准确；复核后将 WebSocket 与公开 config struct 的判断从“阻塞”调整为“范围和 ABI 风险控制项”。

审查结论：`QCNetworkCachePolicy.h`、Cache concrete headers、`QCMultipartFormData.h`、`QCNetworkDefaultLogger.h`、`QCNetworkCancelToken.h`、Middleware base、MockHandler Core Test Support 和 ConnectionPool 管理面已具备默认 Core 安装面证据。
