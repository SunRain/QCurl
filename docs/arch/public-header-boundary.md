# Public Header 边界与安装面

> 本文记录 QCurl 当前对外安装面的唯一合同：哪些头文件属于 public API、哪些内容必须留在 internal/private，以及 install/export/consumer smoke 的验收边界。

## 1. 安装面 SSOT

QCurl 的 install surface 只有两个来源：

- `src/CMakeLists.txt` 中的 `QCURL_INSTALL_HEADERS`
- 生成头 `QCurlConfig.h`

其中：

- `QCPimpl.h` 已从默认安装面移除；`tests/public_api/run_public_api_checks.py scan` 会阻止 install headers 回流 `#include <QCPimpl.h>` 与 `QCURL_DECLARE_*` helper macro。
- `QCNetworkLogger.h` 进入默认稳定安装面，因为 `QCNetworkAccessManager::setLogger()` / `logger()` / `setDebugTraceEnabled()` / `debugTraceEnabled()` 对外依赖该 Core contract。
- `QCNetworkCachePolicy.h` 进入默认稳定安装面，因为 `QCNetworkRequest::setCachePolicy()` / `cachePolicy()` 对外依赖该 Core type header。
- `QCNetworkDefaultLogger.h` 与 `QCNetworkCancelToken.h` 作为 P1 低风险 Core helper 进入默认稳定安装面。
- `QCNetworkCache.h`、`QCNetworkMemoryCache.h`、`QCNetworkDiskCache.h` 作为 P2 Cache lookup Core 能力进入默认稳定安装面，公开读取路径限定为 `lookup(url, ReadMode)`。
- `QCMultipartFormData.h` 作为 P2 Multipart builder 进入默认稳定安装面，内部字段和编码缓存已下沉到 shared-data 实现。
- `QCNetworkMiddleware.h` 作为 P3 Middleware base 进入默认稳定安装面；内置 middleware 实现细节不得混入 base contract。
- `QCNetworkMockHandler.h` 作为 P3 Core Test Support 进入默认安装面；它用于测试支持和请求捕获，不作为生产运行时 Core 能力表述。
- `QCNetworkConnectionPoolConfig.h` 与 `QCNetworkConnectionPoolManager.h` 作为 P3 ConnectionPool 管理面进入默认稳定安装面，config/statistics 使用 accessor / shared-data API。
- `QCNetworkHttpMethod.h` 现在是独立的 public type header，`HttpMethod` 不再由 `QCNetworkReply.h` 承载。
- `QCNetworkHttpVersion.h` 只暴露 `QCNetworkHttpVersion` 枚举；libcurl 常量映射函数 `detail::toCurlHttpVersion(...)` 仅存在于 internal header `src/private/QCNetworkHttpVersion_p.h`。
- `QCNetworkConnectionPoolManager.h` 对外只保留配置、统计和资源控制 contract；reply 与连接池之间的内部协作统一收口到 `src/QCNetworkConnectionPoolManager_p.h`。
- WebSocket 相关头当前只会在 `QCURL_WEBSOCKET_SUPPORT` 打开时进入 `QCURL_INSTALL_HEADERS_EXTRAS`；它们不属于默认稳定安装面。
- `QCURL_INSTALL_HEADERS_EXTRAS` 明确属于 **非默认 Extras**：源码树 examples / benchmarks 可以使用，但默认 install/export/public-api gate 不为其提供稳定 consumer contract。

任何不在上述清单中的头文件，都不属于对下游的稳定承诺。

## 2. Public Header 设计准则

- public header 只暴露 QCurl 自有 API、值类型和 Qt-only 公开依赖。
- library-facing 的 QObject / 服务类统一使用 `ClassPrivate` 作为 d-pointer 私有类命名；现有嵌套 `Private` 属于迁移债务，不属于允许长期保留的目标状态。
- 若多个 public header 共享同一个轻量类型，优先拆出独立 type header（例如 `QCNetworkHttpMethod.h`），而不是让某个“大头文件”成为事实上的转运站。
- 能用 forward declaration 的地方优先 forward declaration；调试输出、字符串化和实现细节优先放到 `.cpp`。
- libcurl 语义转换必须下沉到 `.cpp` 或 `src/private/*`，不得通过 public header 把 `CURL*`、`curl_*` 或 `<curl/...>` 透传给下游。

### 2.1 Include 热点收敛（第一梯队，2026-03）

本轮只处理第一梯队热点头文件的“安装面 include 成本”，不扩展到第二梯队（例如 `QCNetworkRequest.h`、`QCNetworkAccessManager.h`）与长尾头文件。

- `QCNetworkReply.h`：不再直接 `#include "QCNetworkRequest.h"`，改为前置声明并把完整依赖下沉到 `.cpp`，避免 reply 头成为 request 配置面的传递依赖中枢。
- `QCNetworkRequestScheduler.h`：公开头仅保留 Config/Statistics 与调度控制 API；reply 创建统一收口到 `QCNetworkAccessManager::send*()`，队列 bookkeeping、host 计数、带宽窗口、定时器、互斥锁等实现细节下沉到 `.cpp`，避免在安装头暴露 `QMap/QHash/QQueue/QTimer/QDateTime/QMutex` 等重依赖。
- `QCWebSocketPool.h`：公开头仅暴露连接池 contract；池内记录、映射、统计与定时器下沉到 `.cpp`，避免在安装头暴露 `QMap/QHash/QTimer/QDateTime/QMutex` 等重依赖（仍保留必要的 `QCNetworkSslConfig`/`QObject`/`QUrl` 依赖以表达对外 contract）。

### 2.2 Scheduler Core ABI 策略（`QCNetworkRequestScheduler`）

- `Config` / `Statistics` / `LaneConfig` 已收敛为 implicit-sharing 值类型（`QSharedDataPointer<Data>`）。
- public header 只保留 accessor API，不再暴露 public fields。
- special members（析构/拷贝/移动/赋值）使用 out-of-line 定义，避免不完整类型删除与 ODR 风险。

允许的后续演进方式：

- 允许在 `Data` 内新增字段（不改变 public class 布局）。
- 新能力仅通过新增 accessor API 对外暴露，不回退到 public fields。

### 2.3 Scheduler 线程与信号 contract（Core 对外承诺）

- `QCNetworkAccessManager::scheduler()` 是 **owner-thread only**：
  - 只能在 manager owner thread 调用
  - 跨线程误用会 warning + fail-closed 返回 `nullptr`
- `QCNetworkAccessManager::schedulerOnOwnerThread()` 用于跨线程取回 owner scheduler：
  - 前置条件：owner thread 已具备 `QAbstractEventDispatcher`
  - 跨线程路径使用 `BlockingQueuedConnection`，可能阻塞；在持锁状态、析构路径或 UI 高频热路径调用有死锁风险
  - 推荐把“配置动作”marshal 到 owner thread 执行，而不是在任意线程同步取指针
- scheduler 信号签名固定为：
  - `requestQueued(reply, lane, hostKey, priority)`
  - `requestAboutToStart(reply, lane, hostKey)`
  - `requestStarted(reply, lane, hostKey)`
  - `requestFinished(reply, lane, hostKey)`
  - `requestCancelled(reply, lane, hostKey)`
- 信号语义固定为：
  - 全部在 scheduler owner thread 发射
  - `requestAboutToStart` 是最后可拦截点；若 direct slot 中 `cancelRequest()` 生效，不会再 `execute()` / `requestStarted`
  - `requestStarted` 表示已完成 `reply->execute()` 调用（启动提交点）
- `cancelLaneRequests()` 语义固定为：
  - `PendingOnly`：仅清 pending + deferred
  - `PendingAndRunning`：pending + deferred + running 一并取消

### 2.4 Core headers ABI 策略速查表（关键公开头）

| Core header | ABI 策略 | 允许的后续演进方式 |
|------------|----------|--------------------|
| `QCNetworkRequestScheduler.h` | `Config/Statistics/LaneConfig` 使用 implicit-sharing 值类型 + accessor API；special members out-of-line；线程/signals contract 固化 | 仅在 `Data` 新增字段 + 对外新增 accessor；禁止回退到 public fields |
| `QCNetworkAccessManager.h`（scheduler 入口） | `scheduler()` owner-thread only（跨线程 fail-closed）；`schedulerOnOwnerThread()` 跨线程回 owner thread（需 event dispatcher，`BlockingQueuedConnection` 有阻塞风险） | 通过新增 API 扩展能力；现有线程 contract 与失败语义保持兼容 |
| `QCNetworkAccessManager.h`（share/hsts 配置） | `ShareHandleConfig` / `HstsAltSvcCacheConfig` 当前按 **冻结布局** 管理；manager 的 cookie/scheduler/cache/share/hsts 状态已下沉到 `QCNetworkAccessManagerPrivate`，不再占用导出类布局 | 仅通过新增 manager API 扩展行为；不在现有 struct 上做字段增删/重排。若未来需扩展字段，升级为新的 ABI 友好值类型（implicit-sharing + accessor）后再开放 |
| `QCNetworkRequest.h`（lane/priority 入口） | 保持 lane + priority 公开行为合同稳定，调度细节留在 scheduler 实现层 | 允许新增便捷 API，但不破坏既有 lane/priority 语义 |
| `QCNetworkCachePolicy.h` | 独立轻量 enum type header；作为 `QCNetworkRequest` 的 Core 配置类型进入默认安装面 | 可新增策略枚举值，但不得把 concrete cache 实现类型或读取 API 混入该头 |
| `QCNetworkCache.h` | `QCNetworkCacheMetadata` / `QCNetworkCacheLookupResult` 使用 implicit-sharing 值类型 + accessor API；canonical read API 为 `lookup(url, ReadMode)` | 可在 Data 内扩展字段；不得恢复 public fields、`contains()` / `data()` / `metadata()` 旧读路径或新增 layout allowlist |
| `QCNetworkMemoryCache.h` / `QCNetworkDiskCache.h` | QObject cache 实现使用 private data；缓存容器、锁、路径和文件系统细节留在 `.cpp` | 仅通过 accessor / virtual API 扩展行为；不得在 public header 暴露 `QCache`、`QMutex`、`QDir` 等实现依赖 |
| `QCMultipartFormData.h` | builder 使用 implicit-sharing 值类型；字段列表、boundary 缓存和 MIME/file helper 留在 `.cpp` | 可新增 builder API；不得恢复 nested public field layout 或把文件系统 / MIME 依赖暴露到 public header |
| `QCNetworkDefaultLogger.h` | 继承 `QCNetworkLogger`，自身使用 private data；文件输出、内存缓存和 mutex 留在 `.cpp` | 可新增 setter/accessor；不得把文件轮转状态或锁对象暴露为 public fields |
| `QCNetworkCancelToken.h` | QObject service + private data；只暴露 reply-level attach/cancel/timeout 合同 | 仅通过 reply-level API 扩展取消语义；不得新增 request-level cancel shortcut |
| `QCNetworkMiddleware.h` | base class 使用 private data 管理注册 manager；公开面只保留 virtual hook 与 manager 注册合同 | 可新增 hook 或 helper；不得把内置 middleware 的状态、容器或锁暴露到 base header |
| `QCNetworkMockHandler.h` | Core Test Support；`QCNetworkCapturedRequest` 使用 implicit-sharing 值类型 + accessor API；handler runtime state 使用 private data | 可新增测试支持 API；不得恢复 `MockData` / `CapturedRequest` nested public field layout；不得把生产运行时能力写入 Core 文档 |
| `QCNetworkConnectionPoolConfig.h` / `QCNetworkConnectionPoolManager.h` | config/statistics 使用 implicit-sharing 值类型 + accessor API；manager 使用 private data；内部 curl handle 协作位于 `_p.h` | 可在 Data 内新增字段并增加 accessor；不得恢复 public fields、`QMutex`、`QHash` 或内部 helper 到 public header |

## 3. 不属于安装面的 internal/private 头

以下头文件属于库内实现细节，不安装给 consumer：

- `_p.h` / private：`QCNetworkAccessManager_p.h`、`QCNetworkReply_p.h`、`QCNetworkConnectionPoolManager_p.h`、`QCWebSocket_p.h`、`qbytedata_p.h`、`src/private/CurlGlobalConstructor_p.h`、`src/private/QCNetworkLogRedaction_p.h`
- internal curl plumbing：`QCCurlHandleManager.h`、`QCCurlMultiManager.h`、`CurlFeatureProbe.h`、`QCUtility.h`
- internal pipeline / adapters：`src/private/*`

本轮 install surface 审计已确认以下收口对象：

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
- `QCURL_INSTALL_HEADERS_EXTRAS` 不进入这套默认 staging contract；若未来需要单独发布 Extras，必须建立独立 manifest 与 consumer gate，而不是混入当前 Core 包。

### 5.2 导出目标

- 安装后的 package 只对下游暴露 `QCurl::QCurl`。
- `QCurlTargets*.cmake` 不得定义或引用 `QCurl::libcurl_shared`。
- `QCurl::QCurl` 的公开接口不得泄漏 `CURL::libcurl`。
- 当 `QCURL_BUILD_LIBCURL_CONSISTENCY=ON` 时，bundled `libcurl_shared` 只允许作为 staging/runtime 细节存在，不进入 public package/export 合同。

### 5.3 Consumer Smoke

- 正向 consumer：独立工程只能通过 staging prefix 执行 `find_package(QCurl CONFIG REQUIRED)`，随后 include public headers 并链接 `QCurl::QCurl` 成功。
- scheduler 作为 Core 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkRequestScheduler.h>`、`manager.scheduler()` / `schedulerOnOwnerThread()`、以及 `Config/LaneConfig` accessor API（禁止 direct field 依赖）。
- logger 作为 Core 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkLogger.h>`、`NetworkLogEntry` accessor API、以及 `manager.setLogger()` / `logger()` / `setDebugTraceEnabled()` / `debugTraceEnabled()`。
- cache policy 作为 Core type header 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkCachePolicy.h>` 以及 `QCNetworkRequest::setCachePolicy()` / `cachePolicy()`。
- Cache lookup 作为 Core 能力时，正向 consumer fixture 必须持续覆盖 `<QCNetworkCache.h>`、`<QCNetworkMemoryCache.h>`、`<QCNetworkDiskCache.h>`、`QCNetworkCacheMetadata` accessor API、`lookup(url, QCNetworkCacheReadMode::FreshOnly)` 和 `QCNetworkCacheLookupResult` accessor API。
- Multipart builder 作为 Core 能力时，正向 consumer fixture 必须持续覆盖 `<QCMultipartFormData.h>`、`setBoundary()`、`addTextField()`、`addFileField()`、`contentType()`、`size()`、`toByteArray()` 和 `fieldCount()`。
- default logger 作为 Core helper 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkDefaultLogger.h>`、`enableConsoleOutput(false)`、`setMinLogLevel()`、`entries()` 和 `manager.setLogger(&defaultLogger)`。
- cancel token 作为 Core helper 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkCancelToken.h>`、`attach(QCNetworkReply *)`、`attachMultiple(QList<QCNetworkReply *>)`、`setAutoTimeout()`、`cancel()` 和 `isCancelled()`。
- Middleware base 作为 Core 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkMiddleware.h>`、继承 base class、`manager.addMiddleware()`、`middlewares()` 和 `removeMiddleware()`。
- MockHandler 作为 Core Test Support 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkMockHandler.h>`、`QCNetworkCapturedRequest` accessor API、`recordRequest()`、`takeCapturedRequests()`、`mockResponse()` 和 manager mock handler 绑定。
- ConnectionPool 管理面作为 Core 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkConnectionPoolConfig.h>`、`<QCNetworkConnectionPoolManager.h>`、config accessor API、manager `setConfig()` / `config()` 和 statistics accessor API。
- 反向断言：独立 consumer 尝试 `#include <QCNetworkReply_p.h>` 或 `#include <QCNetworkConnectionPoolManager_p.h>` 必须编译失败。
- consumer smoke 不允许回落到源码树 include path。

## 6. 验证口径

public header 边界的最低验收口径如下：

- 快路径：`ctest --test-dir build -L '^public-api$' --output-on-failure`
- 慢路径：`ctest --test-dir build -L '^public-api-slow$' --output-on-failure`

其中：

- `public-api`：逐头 self-compile + 规则扫描
- `public-api-slow`：staging install、安装集合校验、导出合同校验、isolated consumer smoke

> 说明：CTest 的 label 参数是正则；为了避免 `public-api` 误匹配 `public-api-slow`，本仓库文档统一使用带锚点的写法。
