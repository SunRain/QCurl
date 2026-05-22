# QCurl RC 成熟度审查与复核结论

> 日期：2026-05-06
> 范围：架构、代码、公共 API、ABI、Qt/libcurl/KDE 应用库实践和下游使用风险。
> 不讨论：CI/CL。

## 1. 最终结论

QCurl 不应以“源码树全部功能均成熟稳定”的口径进入 RC。

当前发布判断已经进入 **3.0 Core public surface reset**：

- **Core install surface 是本次 `3.0.0-rc.1` 的默认稳定候选。**
- **`QCCookieAsyncResult.h` 已作为 manager cookie async signal / `QFuture` 值结果进入 Core。**
- **P0 `QCNetworkCachePolicy.h` 已作为 Core type header 独立提升。**
- **P1 `QCNetworkDefaultLogger.h` 与 `QCNetworkCancelToken.h` 已作为低风险 Core helper 提升。**
- **P2 Cache lookup concrete API 与 Multipart builder 已完成 public API 重塑并进入 Core。**
- **P3 Middleware base 与 ConnectionPool 管理面已完成 public API 重塑并进入 Core 默认安装面；MockHandler 已移入显式 Test Support。**
- **Diagnostics 与 WebSocket 作为显式 Other Extras / Preview，不进入默认 Core install surface。**

README 中展示的其他能力必须按 Core / Blocking Extras / Test Support / Other Extras / Preview 明确分层，避免把源码树能力误读为默认稳定承诺。

## 2. RC 范围

### 2.1 Core：RC 候选范围

Core 以 `src/CMakeLists.txt` 中的 `QCURL_INSTALL_HEADERS` 加生成头 `QCurlConfig.h` 为准。

当前 Core 头包括：

```text
QCGlobal.h
QCNetworkAccessManager.h
QCCookieAsyncResult.h
QCNetworkRequestConfig.h
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
QCNetworkBody.h
QCNetworkMultipartBody.h
QCNetworkTransferJob.h
QCNetworkDownloadToDeviceJob.h
QCNetworkResumableDownloadJob.h
QCNetworkMiddleware.h
```

证据：

- `src/CMakeLists.txt:112` 明确默认稳定安装面只包含 Core contract。
- `src/CMakeLists.txt:115` 定义 `QCURL_INSTALL_HEADERS`。
- `docs/arch/public-header-boundary.md:7` 定义 install surface 的 SSOT。

### 2.2 Core 头中的 promoted 入口边界

Core 头中存在若干从 Stable Extras 候选提升而来的入口方法。进入默认安装面后，这些入口必须有对应 public header、public-api gate 和 installed consumer smoke 证据。

当前需要明确边界的入口包括：

- `QCNetworkRequest::setCachePolicy(...)` / `cachePolicy()` 暴露 `QCNetworkCachePolicy`，该轻量 type header 已独立进入 Core。
- `QCNetworkRequestConfig.h` 承载 `QCNetworkHttpAuthConfig`、`QCNetworkRedirectConfig`、`QCNetworkTransferConfig` 与相关枚举，避免继续扩大 `QCNetworkRequest.h` 头文件职责。
- `QCNetworkAccessManager::importCookiesAsync(...)` / `exportCookiesAsync(...)` / `clearAllCookiesAsync()` 依赖 `QCCookieOperationResult` 与 `QCCookieExportResult`，`QCCookieAsyncResult.h` 已作为 Core 值结果头进入默认安装面。
- `QCNetworkAccessManager::setCache(...)` / `cache()` 依赖的 `QCNetworkCache` 已进入 Core。
- `QCNetworkBody`、`QCNetworkMultipartBody`、`QCMultipartFormData` 和 transfer job 进入 Core helper/job surface；manager 只负责 `send*()`。
- `QCNetworkAccessManager::addMiddleware(...)` / `middlewares()` 依赖的 `QCNetworkMiddleware` 已作为 Middleware base 进入 Core。
- Mock / capture 能力已从默认 Core consumer 迁出，改由显式 Test Support consumer 使用 `QCNetworkMockHandler.h` 与 `QCNetworkTestSupport.h`。

发布含义：

- Core consumer 可以 include 默认安装头并构建。
- Core consumer 可以使用 `QCCookieOperationResult` / `QCCookieExportResult` accessor API，覆盖 manager cookie async 结果的值语义。
- Core consumer 可以完整使用 Cache lookup concrete API、Multipart builder、Middleware base 和 ConnectionPool 管理面。
- MockHandler 不是默认 Core 安装面能力；只能通过显式 Test Support 安装和 consumer smoke 使用。
- Diagnostics 与 WebSocket 不属于默认稳定 Core 合同；Diagnostics 当前具备 Other Extras opt-in install / consumer smoke，WebSocket 仍按条件 Extras / Preview 管理。

### 2.3 Promotion 波次与剩余候选

以下能力按 breaking Core reset 分波次推进。已经完成的波次进入默认安装面；未完成的波次不得只靠移动头文件清单进入 Core。

| 波次 | 能力 | 头文件 | 当前判断 | 后续前置 |
| --- | --- | --- | --- |
| Core 已完成 | Cookie async result | `QCCookieAsyncResult.h` | 已进入 Core；manager cookie async signal 与 `QFuture` API 使用 `QCCookieOperationResult` / `QCCookieExportResult` accessor 值结果 | 后续不得把 Blocking Extras cookie store 或同步阻塞语义混入该值结果头 |
| P0 已完成 | Cache policy type header | `QCNetworkCachePolicy.h` | 已进入 Core；installed consumer 覆盖 `QCNetworkRequest::setCachePolicy()` / `cachePolicy()` | 后续不得把 concrete cache API 混入该轻量 type header |
| P1 已完成 | Default logger | `QCNetworkDefaultLogger.h` | 已进入 Core helper；consumer smoke 覆盖默认 logger 实例化、manager 绑定和 entries accessor | 后续扩展不得暴露文件轮转状态、锁对象或 public fields |
| P1 已完成 | Cancel token | `QCNetworkCancelToken.h` | 已进入 Core helper；consumer smoke 覆盖 reply-level `attach(QCNetworkReply *)` / `attachMultiple(QList<QCNetworkReply *>)` / timeout / cancel | 不得新增 request-level cancel shortcut 作为本轮验收替代 |
| P2 已完成 | Cache lookup concrete API | `QCNetworkCache.h`、`QCNetworkMemoryCache.h`、`QCNetworkDiskCache.h` | 已进入 Core；`lookup(url, ReadMode)`、`Miss/FreshHit/StaleHit` 进入 installed consumer smoke，`Metadata` / `LookupResult` 已改为 accessor-only shared-data 值类型 | 不得恢复旧 `contains()` / `data()` / `metadata()` 读路径，不得新增 layout allowlist |
| P2 已完成 | Body / Multipart helpers | `QCNetworkBody.h`、`QCMultipartFormData.h`、`QCNetworkMultipartBody.h` | 已进入 Core helper/builder；installed consumer 覆盖 JSON/form body、multipart owning body 和 streaming body API | streaming 大文件路径通过 `QCNetworkMultipartBody::fromSingleFileDevice(device, ...)` + `takeDevice(parent, error)` + `sendPost()` 表达，构造失败用 `std::optional + error`，发送阶段要求 source device/wrapper/reply 同线程 |
| P2 已完成 | File transfer jobs | `QCNetworkTransferJob.h`、`QCNetworkDownloadToDeviceJob.h`、`QCNetworkResumableDownloadJob.h` | 已进入 Core job surface；manager 不再暴露 download/file-transfer convenience | `QCNetworkDownloadToDeviceJob` 与 `QCNetworkResumableDownloadJob` 必须 connect-before-start，构造不启动，底层 reply 只在 `start()` 进入内部启动流程后可用 |
| P3 已完成 | Middleware base | `QCNetworkMiddleware.h` | 已进入 Core；manager 执行链路和 installed consumer smoke 覆盖 add/remove/middlewares 路径；具体通用 middleware 已移入 Other Extras / internal | 后续不得把内置 middleware 实现细节混入 base contract |
| Core 已完成 | Request 配置族 | `QCNetworkRequestConfig.h` | 已进入 Core；`QCNetworkRedirectConfig` / `QCNetworkTransferConfig` 聚合重定向和传输配置，installed consumer smoke 覆盖配置族 | 后续不得为单字段新增 public config class，Advanced 网络路径/DNS API 不随默认 Core 暴露 |
| P3 Test Support 已完成 | Mock handler | `QCNetworkMockHandler.h`、`QCNetworkTestSupport.h` | 已移入显式 Test Support 安装面；`CapturedRequest` 已改为 accessor-only shared-data 值类型，Test Support consumer smoke 覆盖请求捕获、mock response 和 manager 绑定 | 不得回流默认 Core；文档必须持续标注 Test Support，不得表达为生产运行时 Core 能力 |
| P3 已完成 | Connection pool 管理面 | `QCNetworkConnectionPoolConfig.h`、`QCNetworkConnectionPoolManager.h` | 已进入 Core；config/statistics 改为 accessor/shared-data，manager 成员布局下沉到 private data | 后续扩展只能新增 accessor 或 private data 字段，不得恢复 public fields |

证据：

- `tests/qcurl/CMakeLists.txt` 已注册 Cache、Multipart、Middleware、CancelToken、Mock、Diagnostics local 等测试。
- `qcurl_cache_extras_header_self_compile` 已为 Cache first-include 提供补充证据。
- `public-api` / `public-api-slow` 已覆盖 Cookie async result、Cache、Multipart、Middleware、ConnectionPool 的 Core installed consumer 路径，并覆盖 Blocking Extras / Test Support / Other Extras 的显式启用和 default Core 负向 consumer。
- `docs/arch/public-header-boundary.md` 明确非默认 surface 不进入 default Core consumer smoke；Blocking Extras、Test Support、Other Extras 用独立 opt-in gate 验证。

### 2.4 非默认 surface：显式启用，不进入默认 Core 承诺

除上节 Core 能力外，以下能力不进入默认 Core：

- WebSocket：已有实现和测试资产；当前随 `QCURL_WEBSOCKET_SUPPORT` 进入条件 Other Extras，但仍缺少稳定化所需的连接阶段异步语义、ABI 检查和性能/延迟合同；连接阶段仍存在阻塞语义，接收侧存在 polling fallback。
- Diagnostics：已进入显式 Other Extras opt-in 安装面并有 consumer smoke；但 `ping()`、`traceroute()` 依赖外部命令或权限，`DiagResult::details` 仍不是默认 Core 稳定 schema。
- Middleware Extras：`QCNetworkMiddlewareExtras.h` 已进入显式 Other Extras opt-in 安装面；默认 Core 只承诺 base class。
- 未绑定 benchmark 版本、依赖版本、网络环境和日期的性能数字。

证据：

- `src/CMakeLists.txt` 将 Diagnostics / WebSocket 归入非默认 Extras，并为 Diagnostics / 条件 WebSocket 生成 Other Extras manifest。
- `docs/arch/public-header-boundary.md` 明确 Diagnostics / WebSocket 不属于默认 Core 安装面。
- `README.md` 已按 Core / Blocking Extras / Test Support / Other Extras / Preview 说明 WebSocket、Multipart、Cache 等能力的发布层级。

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

### 3.2 share/hsts 配置值类型

复核结论：

> `ShareHandleConfig` / `HstsAltSvcCacheConfig` 已完成 ABI 友好化处理，可进入 Core stable 合同。

原因：

- 两个配置类型现在使用 `QSharedDataPointer` 管理私有 `Data`。
- 对外只暴露 accessor API，不再公开配置字段布局。
- `tests/public_api/public_api_layout_allowlist.txt` 不再放行这两个类型。

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

当前 README 已按 Core / Blocking Extras / Test Support / Other Extras / Preview 分层。后续风险来自新文档或示例再次把 Preview 能力写成默认稳定 API。

建议：

- README 保持 Core / Blocking Extras / Test Support / Other Extras / Preview 分区。
- 快速开始默认只展示 Core API。
- WebSocket、Diagnostics 等示例统一标注 Other Extras / Preview；MockHandler 示例统一标注 Test Support。

### 4.2 ABI 风险

Core 已经采用较多 ABI 友好策略：值类型大多使用 `QSharedDataPointer`，QObject / runtime service 使用 d-pointer，public header boundary 有自动扫描与 consumer smoke。

`QCNetworkAccessManager::ShareHandleConfig` 和 `QCNetworkAccessManager::HstsAltSvcCacheConfig` 已从公开字段布局迁移为 accessor-only shared-data 值类型。后续新增配置字段时，应只扩展 `Data` 并新增 accessor。

若未来扩大 Preview 稳定范围，还需要额外处理 `DiagResult` 中 `QVariantMap details` 的 schema 承诺。

建议：

- 继续通过 public API scan 阻止字段布局和 layout allowlist 回流。
- 新增 share/hsts 配置项时同步 consumer smoke。

### 4.3 WebSocket 成熟度风险

HTTP Core async 路径采用 libcurl multi socket 模式，方向正确。

WebSocket 路径不同：使用 `CURLOPT_CONNECT_ONLY` + `curl_easy_perform()` 建立连接；建连后通过 socket notifier 或 timer polling 接收；fallback 到 50ms polling 时延迟较高。

这说明 WebSocket 可以作为可用功能发布，但还不适合作为默认稳定网络栈合同。

建议：

- 保留 WebSocket 为 Extras / Preview。
- 若要把 WebSocket 提升为 Stable，需要建立独立 public-api gate、consumer smoke、ABI 检查和性能/延迟合同。

### 4.4 Test Support 交界风险

`QCNetworkMockHandler` 与 `QCNetworkTestSupport` 已从默认 Core 安装面移出，只能通过显式 Test Support 安装面使用。下游测试程序可以 opt-in 使用它们，但生产运行时文档不应把 mock/capture 能力写成 Core 网络栈能力。

建议：

- README 和架构文档持续标注 MockHandler 的 Test Support 身份，并保持 default Core negative consumer。
- 新增 mock 示例时放在测试支持章节，避免出现在生产请求路径示例中。

### 4.5 错误语义风险

旧 `QCNetworkAccessManager` file-transfer convenience 已从 Core public surface 移除；文件传输通过独立 job 表达，写入失败会转成可观测错误。

Blocking Extras 第一版已补齐同步 value-result 的基础错误边界：小响应走受限内存 `body()`，超过 `maxInMemoryBodyBytes` 返回 `BodyTooLarge`；大响应走 `downloadToDevice()`，输出设备写入失败返回 `OutputDeviceError`，成功时 `body()` 为空但 `bytesReceived()` 保留实际接收字节数。curl code 只保留在 `diagnosticCurlCode()`，不作为 public 主判断。

后续仍不得把 `QCNetworkReply` 同步执行模式、libcurl callback typedef 或 live `QCNetworkAccessManager` QObject 状态重新混入 Core public contract。

### 4.6 Static library 发布风险

`QCURL_BUILD_STATIC` 已提供 static opt-in 构建形态，static export 可公开传播必要的 `CURL::libcurl` 与 `ZLIB::ZLIB`，并由 `QCurlConfig.cmake` 补齐 `find_dependency()`。

这只说明 static 路径具备可验证入口，不等于 whole project static library ready。static `public-api` / `public-api-slow`、static consumer smoke、pkg-config 和 release gate 证据归档后，只能声明 Core static library ready；RC/Stable 报告仍不得把 whole project 写成 Stable。

## 5. 正面证据

### 5.1 Core 下游安装面验证通过

复核运行：

```bash
ctest --test-dir build -L "^public-api$" --output-on-failure
ctest --test-dir build -L "^public-api-slow$" --output-on-failure
```

结果：

- `public-api`：4/4 通过。
- `public-api-slow`：13/13 通过。

覆盖：public header 自编译、安装头规则扫描、surface manifest 校验、default Core staging install、installed headers 集合校验、CMake export contract、隔离 Core consumer smoke、private header negative consumer 检查，以及 Blocking Extras / Test Support / Other Extras 的 opt-in install、consumer smoke 和 default Core 负向 consumer。

Blocking Extras opt-in consumer smoke 现在还覆盖 `QCBlockingRequestOptions::setMaxInMemoryBodyBytes()`、bounded body、`downloadToDevice()`、`rawHeaders()`、`bytesReceived()` 与 `diagnosticCurlCode()`。Static opt-in gate 需在 `QCURL_BUILD_STATIC=ON` 构建目录中单独运行同一组 public-api / public-api-slow 检查。

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

[x] 保持 README 的 Core / Blocking Extras / Test Support / Other Extras / Preview 三段式发布说明。

[x] 明确 `3.0.0-rc.1` 只承诺 Core install surface。

[x] 给每个默认安装头增加 Core 状态标签；Blocking Extras / Test Support / Other Extras 头继续留在非默认安装面。

[x] `ShareHandleConfig` / `HstsAltSvcCacheConfig` 已迁移为 ABI 友好值类型。

[x] 移除旧 file-transfer convenience，改由 transfer job 暴露可观测错误语义。

[x] 明确 promoted 类型入口与非默认 surface 的 consumer 边界。

### 7.2 应做

[ ] 为 WebSocket 建立更完整的独立 consumer smoke 与运行语义 gate。

[x] 为 Blocking Extras / Test Support / Other Extras 建立独立 install manifest 和 consumer gate，避免和 Core 混用。

[x] 为本轮 promoted 候选补充 self-compile、staging install 与 isolated consumer smoke。

[x] 为 Blocking Extras 第一版补齐 bounded body / `downloadToDevice()` / Blocking 子错误 / `bytesReceived()` 合同与 focused QtTest。

[ ] 为 static opt-in 路径归档 static public-api / public-api-slow、pkg-config、consumer smoke 和 release gate 证据；完成前不声明 static ready。

[ ] 对 WebSocket 建立连接阶段阻塞语义说明，或调整到更一致的异步模型。

[ ] 对 Core API 生成下游迁移指南，说明 3.0.0 的 breaking changes。

### 7.3 可后续处理

[ ] 拆分过大的核心实现文件，例如 `QCNetworkReply.cpp`、`QCNetworkRequestScheduler.cpp`、`QCCurlMultiManager.cpp`。

[ ] 将文档中的性能数字与具体 benchmark 版本、环境和日期绑定。

[ ] 为 WebSocket / 未来 Extras 的稳定化建立分阶段计划。

## 8. 建议发布口径

```text
QCurl 3.0.0-rc.1 发布 Core public surface reset 候选版本。

本 RC 的稳定承诺范围仅包含默认安装面：
QCNetworkAccessManager、QCCookieAsyncResult、QCNetworkRequest、QCNetworkReply、TLS/proxy/timeout/retry 配置、scheduler、cache policy type header、Cache lookup API、Multipart builder、logger、default logger、cancel token、Middleware base、ConnectionPool 管理面与相关 Core type headers。

QCNetworkMockHandler 与 QCNetworkTestSupport 作为显式 Test Support 发布，不随默认 Core 安装面发布，也不作为生产运行时 Core 能力表述。

WebSocket、Diagnostics 和未绑定证据的性能数字不进入默认 Core；Diagnostics 作为显式 Other Extras，WebSocket 作为条件 Other Extras / Preview 对待。
```

## 9. 审查结论

审查结论：当前项目的 **Core public surface reset 可以进入分阶段 RC 候选推进**。

审查结论：当前项目的 **全部功能集合不应整体进入成熟库 RC 阶段**。

审查结论：上次 RC 成熟度审查结论主体准确；复核后将 WebSocket 与公开 config struct 的判断从“阻塞”调整为“范围和 ABI 风险控制项”。

审查结论：`QCCookieAsyncResult.h`、`QCNetworkCachePolicy.h`、Cache concrete headers、`QCMultipartFormData.h`、`QCNetworkDefaultLogger.h`、`QCNetworkCancelToken.h`、Middleware base 和 ConnectionPool 管理面已具备默认 Core 安装面证据；MockHandler 已具备显式 Test Support 安装面证据。
