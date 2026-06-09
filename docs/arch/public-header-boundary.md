# Public Header 边界与安装面

> 本文记录 QCurl 当前对外安装面的唯一合同：哪些头文件属于 public API、哪些内容必须留在 internal/private，以及 install/export/consumer smoke 的验收边界。

## 1. 安装面 SSOT

QCurl 的 install surface 只有两个来源：

- `src/CMakeLists.txt` 中的 `QCURL_INSTALL_HEADERS`
- 生成头 `QCurlConfig.h`

机器可读发布面清单位于 `tests/public_api/surface_manifest.json`。它记录每个 public header
当前属于 Core、Blocking Extras、Other Extras、Test Support 还是 Internal，以及 hard-break
完成后的目标安装面。`qcurl_public_api_surface_manifest` 会把这份清单同
`QCURL_INSTALL_HEADERS` / `QCURL_INSTALL_HEADERS_EXTRAS` 生成的 manifest 对齐，防止默认
Core 安装面新增未分层头文件。

其中：

- `QCPimpl.h` 已从默认安装面移除；`tests/public_api/run_public_api_checks.py scan` 会阻止 install headers 回流 `#include <QCPimpl.h>` 与 `QCURL_DECLARE_*` helper macro。
- `QCCookie.h` 与 `QCCookieAsyncResult.h` 进入默认稳定安装面，因为 `QCNetworkAccessManager` 的 cookie import/export、cookie async signal 与 `QFuture` API 对外依赖 `QCCookie`、`QCCookieOperationResult` 和 `QCCookieExportResult` 值类型。
- `QCNetworkLogger.h` 进入默认稳定安装面，因为 `QCNetworkAccessManager::setLogger()` / `logger()` / `setDebugTraceEnabled()` / `debugTraceEnabled()` 对外依赖该 Core contract。
- `QCNetworkCachePolicy.h` 进入默认稳定安装面，因为 `QCNetworkRequest::setCachePolicy()` / `cachePolicy()` 对外依赖该 Core type header。
- `QCNetworkRequestConfig.h` 进入默认稳定安装面，承载 `QCNetworkHttpAuthConfig`、`QCNetworkRedirectConfig`、`QCNetworkTransferConfig` 及相关枚举；`QCNetworkRequest.h` 只保留请求值对象与便捷转发 API。DNS / DoH / connect-to / resolve override / Happy Eyeballs / 本地端口绑定仍是 Advanced/Internal 候选，不属于默认 Core consumer contract。
- `QCNetworkDefaultLogger.h` 与 `QCNetworkCancelToken.h` 作为 P1 低风险 Core helper 进入默认稳定安装面。
- `QCNetworkCache.h`、`QCNetworkMemoryCache.h`、`QCNetworkDiskCache.h` 作为 P2 Cache lookup Core 能力进入默认稳定安装面，公开读取路径限定为 `lookup(url, ReadMode)`。
- `QCMultipartFormData.h` 作为 P2 Multipart builder 进入默认稳定安装面，内部字段和编码缓存已下沉到 shared-data 实现。
- `QCNetworkBody.h` 与 `QCNetworkMultipartBody.h` 作为 P2 body helper 进入默认稳定安装面，用于 JSON/form/multipart body 生成；发送仍统一走 `QCNetworkAccessManager::post()`。
- `QCNetworkTransferJob.h`、`QCNetworkDownloadToDeviceJob.h` 与 `QCNetworkResumableDownloadJob.h` 作为 P2 file-transfer job 进入默认稳定安装面；manager 不暴露文件传输 convenience。
- `QCNetworkMiddleware.h` 作为 P3 Middleware base 进入默认稳定安装面；内置 middleware 实现细节不得混入 base contract。
- `QCNetworkMiddlewareExtras.h` 属于显式 Other Extras，当前承载稳定通用的 `QCRedactingLoggingMiddleware` / `QCObservabilityMiddleware`；策略型、签名、统一重试和错误处理等强耦合 middleware 留在 internal/private。
- `QCNetworkMockHandler.h` 与 `QCNetworkTestSupport.h` 属于显式 Test Support，只通过 `TestSupportDevelopment` 安装；默认 Core 不安装 mock/capture/test-support 头。
- `QCNetworkConnectionPoolConfig.h` 与 `QCNetworkConnectionPoolManager.h` 作为 P3 ConnectionPool 管理面进入默认稳定安装面，config/statistics 使用 accessor / shared-data API。
- `QCNetworkHttpMethod.h` 现在是独立的 public type header，`HttpMethod` 不再由 `QCNetworkReply.h` 承载。
- `QCNetworkHttpVersion.h` 只暴露 `QCNetworkHttpVersion` 枚举；libcurl 常量映射函数 `detail::toCurlHttpVersion(...)` 仅存在于 internal header `src/private/QCNetworkHttpVersion_p.h`。
- `QCNetworkConnectionPoolManager.h` 对外只保留配置、统计和资源控制 contract；reply 与连接池之间的内部协作统一收口到 `src/QCNetworkConnectionPoolManager_p.h`。
- `QCBlockingNetworkClient.h`、`QCBlockingNetworkResult.h` 与 `QCBlockingCookieStore.h` 属于显式 Blocking Extras，不进入默认 Core；当前第一版合同覆盖受限内存 `body()`、`maxInMemoryBodyBytes`、`BodyTooLarge`、`downloadToDevice()`、`bytesReceived()` 与 curl code 诊断字段。
- `QCNetworkDiagnostics.h` 属于显式 Other Extras，只通过 `OtherExtrasDevelopment` 安装；默认 Core 不安装 diagnostics 头。
- WebSocket 相关头当前只会在 `QCURL_WEBSOCKET_SUPPORT` 打开时进入 `QCURL_INSTALL_HEADERS_EXTRAS` / `QCURL_INSTALL_HEADERS_OTHER_EXTRAS`；它们不属于默认 Core 安装面。
- `QCURL_INSTALL_HEADERS_EXTRAS` 明确属于 **非默认 Extras**：源码树 examples / benchmarks 可以使用；默认 Core install/export/public-api gate 不为其提供稳定 consumer contract，显式 Extras gate 单独验证 opt-in 安装和 default Core 负向 consumer。
- `QCNetworkLaneKey.h` 与 `QCNetworkSchedulerPolicy.h` 进入默认 Core 安装面；scheduler 配置入口收敛到 `QCNetworkAccessManager::setSchedulerPolicy()`，请求 lane 使用 typed key。`QCNetworkRequestScheduler.h` 仅保留既有观察/内部测试兼容面，不再作为下游配置 workflow。
- 旧的 manager-level 同步发送 API 已从 Core 移除；同步 value-result API 归属 Blocking Extras。

任何不在上述清单中的头文件，都不属于对下游的稳定承诺。

## 2. Public Header 设计准则

- public header 只暴露 QCurl 自有 API、值类型和 Qt-only 公开依赖。
- library-facing 的 QObject / 服务类统一使用 `ClassPrivate` 作为 d-pointer 私有类命名；现有嵌套 `Private` 属于迁移债务，不属于允许长期保留的目标状态。
- 若多个 public header 共享同一个轻量类型，优先拆出独立 type header（例如 `QCNetworkHttpMethod.h`），而不是让某个“大头文件”成为事实上的转运站。
- 能用 forward declaration 的地方优先 forward declaration；调试输出、字符串化和实现细节优先放到 `.cpp`。
- libcurl 语义转换必须下沉到 `.cpp` 或 `src/private/*`，不得通过 public header 把 `CURL*`、`curl_*` 或 `<curl/...>` 透传给下游。

### 2.1 Include 热点收敛

当前已收敛的第一梯队热点头文件用于降低安装面 include 成本；第二梯队（例如 `QCNetworkRequest.h`、`QCNetworkAccessManager.h`）与长尾头文件按后续独立审计处理。

- `QCNetworkReply.h`：不再直接 `#include "QCNetworkRequest.h"`，改为前置声明并把完整依赖下沉到 `.cpp`，避免 reply 头成为 request 配置面的传递依赖中枢。
- `QCNetworkLaneKey.h` / `QCNetworkSchedulerPolicy.h`：typed lane 和 manager-level policy 独立成轻量 Core type header；reply 创建仍统一收口到 `QCNetworkAccessManager::head()/get()/post()/put()/patch()`，scheduler 队列 bookkeeping、host 计数、带宽窗口、定时器、互斥锁等实现细节留在 `.cpp`。
- `QCWebSocketPool.h`：公开头仅暴露连接池 contract；池内记录、映射、统计与定时器下沉到 `.cpp`，避免在安装头暴露 `QMap/QHash/QTimer/QDateTime/QMutex` 等重依赖（仍保留必要的 `QCNetworkSslConfig`/`QObject`/`QUrl` 依赖以表达对外 contract）。

### 2.2 Scheduler Core ABI 策略（typed lane / manager policy）

- `QCNetworkLaneKey` 是轻量值类型，用于替代裸 `QString` lane public 入口。
- `QCNetworkSchedulerPolicy` / `QCNetworkSchedulerStatistics` 使用 implicit-sharing 值类型（`QSharedDataPointer<Data>`）和 accessor API。
- policy validation 负责 default lane、lane 注册、权重、quantum、reservation 与 scheduler admission limit 的 fail-closed 校验。
- special members（析构/拷贝/移动/赋值）使用 out-of-line 定义，避免不完整类型删除与 ODR 风险。

允许的后续演进方式：

- 允许在 `Data` 内新增字段（不改变 public class 布局）。
- 新能力仅通过新增 accessor API 对外暴露，不回退到 public fields。

### 2.3 Scheduler 线程与 manager-level contract

- `QCNetworkAccessManager` 持有 manager-owned scheduler child object。
- 下游配置入口固定为 `setSchedulerPolicy()`；观察入口固定为 `schedulerStatistics()`；lane 取消入口固定为返回 `QCNetworkLaneCancelResult` 的 `cancelLaneRequests(QCNetworkLaneKey, SchedulerCancelScope)`。
- 上述 manager-level API 必须在 manager owner thread 调用；跨线程配置请显式投递到 owner thread。
- unknown lane 固定按 RequireRegistered fail-closed；请求使用 `QCNetworkLaneKey::fromName(name, &lane, &error)` 创建自定义 lane 后，必须先注册到 `QCNetworkSchedulerPolicy`。
- `cancelLaneRequests()` 语义固定为：
  - `PendingOnly`：仅清 pending + deferred
  - `PendingAndRunning`：pending + deferred + running 一并取消
  - invalid lane、未注册 lane、非 owner thread 和 scheduler 未启用均返回结构化失败，不执行取消副作用。

### 2.4 Core headers ABI 策略速查表（关键公开头）

| Core header | ABI 策略 | 允许的后续演进方式 |
|------------|----------|--------------------|
| `QCCookie.h` | `QCCookie` 使用 implicit-sharing 值类型 + accessor API；承载 Core manager cookie import/export 和 Blocking Extras cookie snapshot/delta 的 public cookie model | 可在 Data 内扩展字段并新增 accessor；不得暴露 QtNetwork、libcurl cookie line 或 public fields |
| `QCCookieAsyncResult.h` | `QCCookieOperationResult` / `QCCookieExportResult` 使用 implicit-sharing 值类型 + accessor API；承载 manager cookie async signal 与 `QFuture` 结果 | 可在 Data 内扩展字段并新增 accessor；不得恢复 public fields 或把 blocking cookie store 行为混入 Core |
| `QCNetworkLaneKey.h` / `QCNetworkSchedulerPolicy.h` | typed lane + manager-level policy/statistics 使用值类型 + accessor API；配置通过 `QCNetworkAccessManager::setSchedulerPolicy()` | 可在 Data 内扩展字段并新增 accessor；不得恢复裸 `QString` lane 或 `manager scheduler getter + setLaneConfig` public workflow |
| `QCNetworkAccessManager.h`（scheduler 入口） | manager-owned scheduler；公开配置/观察/取消入口为 `setSchedulerPolicy()`、`schedulerStatistics()`、`cancelLaneRequests()` | 不得恢复 scheduler 指针配置 workflow 或透明跨线程阻塞 getter |
| `QCNetworkAccessManager.h`（share/hsts 配置） | `ShareHandleConfig` / `HstsAltSvcCacheConfig` 使用 implicit-sharing 值类型 + accessor API；manager 的 cookie/scheduler/cache/share/hsts 状态已下沉到 `QCNetworkAccessManagerPrivate`，不再占用导出类布局 | 可在 `Data` 内扩展字段并新增 accessor；不得恢复 public fields 或新增 layout allowlist |
| `QCNetworkRequest.h`（lane/priority 入口） | lane 使用 `QCNetworkLaneKey`，priority 保持非抢占语义；重定向/传输便捷 API 委托到 `QCNetworkRequestConfig.h` 的配置族 | 允许新增 typed 便捷 API；不得恢复 `QString` lane setter compatibility overload；Advanced 网络路径/DNS API 不进入默认 Core |
| `QCNetworkRequestConfig.h` | `QCNetworkHttpAuthConfig` / `QCNetworkRedirectConfig` / `QCNetworkTransferConfig` 使用 implicit-sharing 值类型 + accessor API；聚合认证、重定向与传输配置自然边界 | 可在 Data 内扩展字段并新增 accessor；不得为单字段拆出 public config class，也不得暴露 `CURLOPT_*` |
| `QCNetworkCachePolicy.h` | 独立轻量 enum type header；作为 `QCNetworkRequest` 的 Core 配置类型进入默认安装面 | 可新增策略枚举值，但不得把 concrete cache 实现类型或读取 API 混入该头 |
| `QCNetworkCache.h` | `QCNetworkCacheMetadata` / `QCNetworkCacheLookupResult` 使用 implicit-sharing 值类型 + accessor API；canonical read API 为 `lookup(url, ReadMode)` | 可在 Data 内扩展字段；不得恢复 public fields、`contains()` / `data()` / `metadata()` 旧读路径或新增 layout allowlist |
| `QCNetworkMemoryCache.h` / `QCNetworkDiskCache.h` | QObject cache 实现使用 private data；缓存容器、锁、路径和文件系统细节留在 `.cpp` | 仅通过 accessor / virtual API 扩展行为；不得在 public header 暴露 `QCache`、`QMutex`、`QDir` 等实现依赖 |
| `QCNetworkBody.h` | JSON / form-url-encoded body helper；不依赖 manager，不暴露 public fields | 可新增编码 helper；不得把发送、文件 I/O 或 manager 状态混入该类型 |
| `QCMultipartFormData.h` | builder 使用 implicit-sharing 值类型；字段列表、boundary 缓存和 MIME/file helper 留在 `.cpp` | 可新增 builder API；不得恢复 nested public field layout 或把文件系统 / MIME 依赖暴露到 public header |
| `QCNetworkMultipartBody.h` | multipart body helper；owning body 返回 QByteArray，streaming body 持有独占 QIODevice 并通过 `post()` 发送；single-file streaming 构造要求显式 `ownerThread` 且调用线程/source device/reply 后续发送线程一致 | 可新增 multipart body 变体；不得恢复 manager-level multipart convenience |
| `QCNetworkTransferJob.h` / `QCNetworkDownloadToDeviceJob.h` / `QCNetworkResumableDownloadJob.h` | QObject job + d-pointer；`QCNetworkDownloadToDeviceJob` 与 `QCNetworkResumableDownloadJob` 构造不启动，调用方 connect 后显式 `start()`；底层 reply 只在启动成功后可用 | 可新增 job 状态 accessor；不得绕过 manager 管线或暴露 QFile/QSaveFile 内部状态 |
| `QCNetworkDefaultLogger.h` | 继承 `QCNetworkLogger`，自身使用 private data；文件输出、内存缓存和 mutex 留在 `.cpp` | 可新增 setter/accessor；不得把文件轮转状态或锁对象暴露为 public fields |
| `QCNetworkCancelToken.h` | QObject service + private data；只暴露 reply-level attach/cancel/timeout 合同 | 仅通过 reply-level API 扩展取消语义；不得新增 request-level cancel shortcut |
| `QCNetworkMiddleware.h` | base class 使用 private data 管理注册 manager；公开面只保留 virtual hook 与 manager 注册合同 | 可新增 hook 或 helper；不得把内置 middleware 的状态、容器或锁暴露到 base header |
| `QCNetworkMockHandler.h` / `QCNetworkTestSupport.h` | 显式 Test Support 安装面；`QCNetworkCapturedRequest` 使用 implicit-sharing 值类型 + accessor API；manager 绑定通过 `QCurl::TestSupport` 命名空间表达 | 不得回到默认 Core 安装面；不得恢复 `MockData` / `CapturedRequest` nested public field layout；不得把生产运行时能力写入 Core 文档 |
| `QCNetworkConnectionPoolConfig.h` / `QCNetworkConnectionPoolManager.h` | config/statistics 使用 implicit-sharing 值类型 + accessor API；manager 使用 private data；内部 curl handle 协作位于 `_p.h` | 可在 Data 内新增字段并增加 accessor；不得恢复 public fields、`QMutex`、`QHash` 或内部 helper 到 public header |

## 3. 不属于安装面的 internal/private 头

以下头文件属于库内实现细节，不安装给 consumer：

- `_p.h` / private：`QCNetworkAccessManager_p.h`、`QCNetworkReply_p.h`、`QCNetworkConnectionPoolManager_p.h`、`QCWebSocket_p.h`、`qbytedata_p.h`、`src/private/CurlGlobalConstructor_p.h`、`src/private/QCNetworkLogRedaction_p.h`
- internal curl plumbing：curl handle / multi manager、feature probe 与 utility helper 头文件
- internal pipeline / adapters：`src/private/*`

当前 install surface 审计已确认以下收口对象：

- `QCNetworkConnectionPoolManager::{configureCurlHandle, recordRequestCompleted}` → 已迁移到 `QCNetworkConnectionPoolManager_p.h`
- `CurlGlobalConstructor` → 已迁移到 `src/private/CurlGlobalConstructor_p.h`
- `QCNetworkLogRedaction` → 已迁移到 `src/private/QCNetworkLogRedaction_p.h`

这些文件可以在库内自由演进，但不应出现在安装前缀，也不应被对外文档当作 public API 引用。

## 4. 安装头禁止项

对 `QCURL_INSTALL_HEADERS` 中的头文件，以下内容一律视为违约：

- `#include <curl/...>`
- `CURL*`、`curl_*`
- Qt private include（如 `Qt.../private/...`）
- 任意 `*_p.h`
- `<tuple>`、`std::tuple`

这些规则由 `tests/public_api/run_public_api_checks.py scan` 执行；出现违约项时必须以“文件 + 行号 + 规则名”失败。

## 5. Install / Export 合同

### 5.1 安装集合

- `cmake --install` 到 staging prefix 后，`<staging>/include/qcurl/` 的头文件集合必须与 `QCURL_INSTALL_HEADERS + QCurlConfig.h` **完全一致**。
- 不允许多装 internal/private 头，也不允许漏装 manifest 中的 public 头。
- `QCPimpl.h` 不得再出现在 staging include 目录或默认 manifest 中。
- `QCURL_INSTALL_HEADERS_EXTRAS` 不进入默认 staging contract；Blocking Extras、Test Support 与 Other Extras 各自通过独立 manifest、install component、opt-in consumer smoke 和 default Core 负向 consumer gate 验证。

### 5.2 导出目标

- 安装后的 package 对默认 Core 暴露 `QCurl::QCurl`；Blocking Extras、Test Support 与 Other Extras 通过 `QCurl::BlockingExtras`、`QCurl::TestSupport`、`QCurl::OtherExtras` opt-in target 暴露。
- `QCurlTargets*.cmake` 不得定义或引用 `QCurl::libcurl_shared`。
- shared 构建下，`QCurl::QCurl` 的公开接口不得泄漏 `CURL::libcurl` 或 `ZLIB::ZLIB`。
- static 构建下，`QCurl::QCurl` 只能通过 public link interface 暴露 Core 必需的 `CURL::libcurl`；`QCurlConfig.cmake` 必须提供 `find_dependency(CURL ...)`，但默认 Core export 不再暴露 `ZLIB::ZLIB`。
- 当 `QCURL_BUILD_LIBCURL_CONSISTENCY=ON` 时，bundled `libcurl_shared` 只允许作为 staging/runtime 细节存在，不进入 public package/export 合同。

### 5.3 Consumer Smoke

- 正向 consumer：独立工程只能通过 staging prefix 执行 `find_package(QCurl CONFIG REQUIRED)`，随后 include public headers 并链接 `QCurl::QCurl` 成功。
- cookie value 与 async result 作为 Core 值结果时，正向 consumer fixture 必须持续覆盖 `<QCCookie.h>`、`<QCCookieAsyncResult.h>`、`QCCookie` accessor、`QCCookieOperationResult::success()/failure()`、`QCCookieExportResult::success()/failure()`、`isSuccess()`、`error()` 和 `cookies()`，且不得要求 default consumer 链接 QtNetwork。
- 正向 Core consumer fixture 覆盖 `<QCNetworkLaneKey.h>`、`<QCNetworkSchedulerPolicy.h>`、typed lane、manager-level `setSchedulerPolicy()` / `schedulerPolicy()` / `schedulerStatistics()` 和 policy/lane accessor API；不得覆盖或恢复 `manager scheduler getter` 配置 workflow。
- logger 作为 Core 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkLogger.h>`、`NetworkLogEntry` accessor API、以及 `manager.setLogger()` / `logger()` / `setDebugTraceEnabled()` / `debugTraceEnabled()`。
- cache policy 作为 Core type header 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkCachePolicy.h>` 以及 `QCNetworkRequest::setCachePolicy()` / `cachePolicy()`。
- Cache lookup 作为 Core 能力时，正向 consumer fixture 必须持续覆盖 `<QCNetworkCache.h>`、`<QCNetworkMemoryCache.h>`、`<QCNetworkDiskCache.h>`、`QCNetworkCacheMetadata` accessor API、`lookup(url, QCNetworkCacheReadMode::FreshOnly)` 和 `QCNetworkCacheLookupResult` accessor API。
- Body / Multipart / transfer job 作为 Core 能力时，正向 consumer fixture 必须持续覆盖 `<QCNetworkBody.h>`、`<QCMultipartFormData.h>`、`<QCNetworkMultipartBody.h>`、`<QCNetworkDownloadToDeviceJob.h>`、`<QCNetworkResumableDownloadJob.h>`、JSON/form body helper、multipart `fromFormData()`、`contentType()`、`data()`、`size()`、`toByteArray()`、`fieldCount()` 和 job type 可见性。
- default logger 作为 Core helper 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkDefaultLogger.h>`、`enableConsoleOutput(false)`、`setMinLogLevel()`、`entries()` 和 `manager.setLogger(&defaultLogger)`。
- cancel token 作为 Core helper 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkCancelToken.h>`、`attach(QCNetworkReply *)`、`attachMultiple(QList<QCNetworkReply *>)`、`setAutoTimeout()`、`cancel()` 和 `isCancelled()`。
- Middleware base 作为 Core 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkMiddleware.h>`、继承 base class、`manager.addMiddleware()`、`middlewares()` 和 `removeMiddleware()`。
- Test Support opt-in consumer fixture 覆盖 `<QCNetworkMockHandler.h>`、`<QCNetworkTestSupport.h>`、`QCNetworkCapturedRequest` accessor API、`recordRequest()`、`takeCapturedRequests()`、`mockResponse()` 和 `QCurl::TestSupport` manager 绑定；default Core 负向 consumer 必须证明这些头不能隐式 include。
- Blocking Extras opt-in consumer fixture 覆盖 `<QCBlockingNetworkClient.h>`、`<QCBlockingNetworkResult.h>`、`QCBlockingRequestOptions::setMaxInMemoryBodyBytes()`、method-specific `get()` / `post()` / `put()` / `deleteResource()`、受校验的 `sendCustomRequest()`、`downloadToDevice()`、`rawHeaders()`、`bytesReceived()` 与 `diagnosticCurlCode()`；default Core 负向 consumer 必须证明 Blocking Extras 头不能隐式 include。
- Other Extras opt-in consumer fixture 覆盖 `<QCNetworkDiagnostics.h>` 与 `<QCNetworkMiddlewareExtras.h>` 的最小 value/API 可见性；default Core 负向 consumer 必须证明 diagnostics / middleware extras 头不能隐式 include。
- ConnectionPool 管理面作为 Core 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkConnectionPoolConfig.h>`、`<QCNetworkConnectionPoolManager.h>`、config accessor API、manager `setConfig()` / `config()` 和 statistics accessor API。
- 反向断言：独立 consumer 尝试 `#include <QCNetworkReply_p.h>` 或 `#include <QCNetworkConnectionPoolManager_p.h>` 必须编译失败。
- consumer smoke 不允许回落到源码树 include path。

## 6. 验证口径

public header 边界的最低验收口径如下：

- 快路径：`ctest --test-dir build -L '^public-api$' --output-on-failure`
- 慢路径：`ctest --test-dir build -L '^public-api-slow$' --output-on-failure`

其中：

- `public-api`：逐头 self-compile + 规则扫描
- `qcurl_public_api_surface_manifest`：校验机器可读发布面清单与 CMake 生成的 Core / Extras manifest 对齐
- `public-api-slow`：default Core staging install、安装集合校验、导出合同校验、isolated Core consumer smoke，以及 Blocking Extras / Test Support / Other Extras 的 opt-in install / consumer smoke 与 default Core 负向 consumer
- static opt-in 路径还必须在 `QCURL_BUILD_SHARED_LIBS=OFF` 构建目录中重跑上述 `public-api` 与 `public-api-slow`，验证 static install/export/consumer contract。

> 说明：CTest 的 label 参数是正则；为了避免 `public-api` 误匹配 `public-api-slow`，本仓库文档统一使用带锚点的写法。
