# Public Header 边界与安装面

> 本文记录 QCurl 当前对外安装面的唯一合同：哪些头文件属于 public API、哪些内容必须留在 internal/private，以及 install/export/consumer smoke 的验收边界。

## 1. 安装面 SSOT

QCurl 的 install surface 由以下 CMake 清单定义：

- `src/CMakeLists.txt` 中的 `QCURL_INSTALL_HEADERS`
- `src/CMakeLists.txt` 中的 `QCURL_INSTALL_HEADERS_BLOCKING_EXTRAS`
- `src/CMakeLists.txt` 中的 `QCURL_INSTALL_HEADERS_OTHER_EXTRAS`
- `src/CMakeLists.txt` 中的 `QCURL_INSTALL_HEADERS_TEST_SUPPORT`
- 生成头 `QCurlConfig.h`

机器可读发布面清单位于 `tests/public_api/surface_manifest.json`。它记录每个 public header
所属的四个交付组件之一、Stable/Preview 成熟度、Public/Internal 可见性，以及 2.0.0
目标安装面。`Preview` 不是第五组件，`Internal` 也不是可安装模块。
`qcurl_public_api_surface_manifest` 会把这份清单同四个组件 manifest 双向对齐，防止头文件
无归属、重复归属、回流 Core 或以陈旧条目滞留机器合同。

四个逻辑交付组件固定为 Core、Blocking Extras、Other Extras 和 Test Support。Core 与
Other Extras 是生产 runtime 编译库；Blocking Extras 是显式 `INTERFACE` 消费面，其实现
编入 Core；Test Support 是开发用途静态库。Diagnostics 与 WebSocket 是 Other Extras 中的
Preview API，Middleware Extras 是同一组件中的 Stable API。

本文件中的“源码兼容安装面”表示 2.x Core 源码兼容与安装/consumer 合同，不表示二进制
兼容。下游在每次 QCurl 更新后必须重新编译和链接；2.0 不建立 ABI baseline。

其中：

- `QCPimpl.h` 已从 Core component 安装面移除；`tests/public_api/run_public_api_checks.py scan` 会阻止 install headers 回流 `#include <QCPimpl.h>` 与 `QCURL_DECLARE_*` helper macro。
- `QCCookie.h` 与 `QCCookieAsyncResult.h` 进入默认源码兼容安装面，因为 `QCNetworkAccessManager` 的 cookie import/export 与 per-call `QFuture` API 对外依赖 `QCCookie`、`QCCookieOperationResult` 和 `QCCookieExportResult` 值类型；每次调用只通过 Future 返回一个结构化结果。
- `QCNetworkLogger.h` 进入默认源码兼容安装面，因为 `QCNetworkAccessManager::setLogger()` / `logger()` / `setDebugTraceEnabled()` / `debugTraceEnabled()` 对外依赖该 Core contract。
- `QCNetworkCachePolicy.h` 进入默认源码兼容安装面，因为 `QCNetworkRequest::setCachePolicy()` / `cachePolicy()` 对外依赖该 Core type header。
- `QCNetworkRequestConfig.h` 进入 Core 源码兼容安装面，承载 `QCNetworkHttpAuthConfig`、`QCNetworkRedirectConfig`、`QCNetworkTransferConfig` 及相关枚举；`QCNetworkRequest.h` 只保留请求值对象与便捷转发 API。DNS / DoH / connect-to / resolve override / Happy Eyeballs / 本地端口绑定仍是 Advanced/Internal 候选，不属于无组件 Core consumer contract。
- `QCNetworkDefaultLogger.h` 与 `QCNetworkCancelToken.h` 作为 P1 低风险 Core helper 进入默认源码兼容安装面；logger 通过 `QSharedPointer` 由 manager 和 reply 共享持有。
- `QCurlRuntime.h` 作为进程级 Core lifecycle controller 进入默认源码兼容安装面；它只表达应用级手动
  libcurl cleanup 的状态、lease 协调和结构化结果，不提供动态库卸载或外部 lease API。
- `QCNetworkCache.h`、`QCNetworkCacheRequestKey.h`、`QCNetworkMemoryCache.h`、`QCNetworkDiskCache.h` 作为显式启用的结构化 Cache lookup Core 能力进入默认源码兼容安装面，公开读取路径限定为 `lookup(const QCNetworkCacheRequestKey &, ReadMode)`。
- `QCNetworkRequest::cachePartitionKey()` 是认证缓存隔离的调用方 opaque 输入；认证请求未提供分区时不得存储。
- `QCMultipartFormData.h` 作为 P2 Multipart builder 进入默认源码兼容安装面，内部字段和编码缓存已下沉到 shared-data 实现。
- `QCNetworkBody.h` 与 `QCNetworkMultipartBody.h` 作为 P2 body helper 进入默认源码兼容安装面，用于 JSON/form/multipart body 生成；发送仍统一走 `QCNetworkAccessManager::post()`。
- `QCNetworkTransferJob.h`、`QCNetworkDownloadToDeviceJob.h` 与 `QCNetworkResumableDownloadJob.h` 作为 P2 file-transfer job 进入默认源码兼容安装面；manager 不暴露文件传输 convenience。
- `QCNetworkMiddleware.h` 作为 P3 Middleware base 进入默认源码兼容安装面；内置 middleware 实现细节不得混入 base contract。
- `QCNetworkMiddlewareExtras.h` 属于显式 Other Extras，当前承载通用的 `QCRedactingLoggingMiddleware` / `QCObservabilityMiddleware`；策略型、签名、统一重试和错误处理等强耦合 middleware 留在 internal/private。
- `QCNetworkMockHandler.h` 与 `QCNetworkTestSupport.h` 属于显式 Test Support，只通过 `TestSupportDevelopment` 安装；Core-only component stage 不安装 mock/capture/test-support 头。
- `QCNetworkConnectionPoolConfig.h` 与 `QCNetworkConnectionPoolManager.h` 作为 P3 ConnectionPool 管理面进入默认源码兼容安装面，config/statistics 使用 accessor / shared-data API。
- `QCNetworkHttpMethod.h` 现在是独立的 public type header，`HttpMethod` 不再由 `QCNetworkReply.h` 承载。
- `QCNetworkHttpVersion.h` 只暴露 `QCNetworkHttpVersion` 枚举；libcurl 常量映射函数 `detail::toCurlHttpVersion(...)` 仅存在于 internal header `src/private/QCNetworkHttpVersion_p.h`。
- `QCNetworkConnectionPoolManager.h` 对外只保留配置、统计和资源控制 contract；reply 与连接池之间的内部协作统一收口到 `src/QCNetworkConnectionPoolManager_p.h`。
- `QCBlockingNetworkClient.h`、`QCBlockingNetworkResult.h` 与 `QCBlockingCookieStore.h` 属于显式 Blocking Extras，不进入 Core consumer；当前第一版合同覆盖受限内存 `body()`、`maxInMemoryBodyBytes`、`BodyTooLarge`、`downloadToDevice()`、`bytesReceived()` 与 curl code 诊断字段。
- `QCNetworkDiagnostics.h` 属于显式 Other Extras，只通过 `OtherExtrasDevelopment` 安装；Core-only component stage 不安装 diagnostics 头。
- WebSocket 相关头当前只会在 `QCURL_WEBSOCKET_SUPPORT` 打开时进入 `QCURL_INSTALL_HEADERS_EXTRAS` / `QCURL_INSTALL_HEADERS_OTHER_EXTRAS`；它们不属于 Core component 安装面。
- `QCURL_INSTALL_HEADERS_EXTRAS` 只是三个非默认组件头的兼容聚合清单，不表达组件所有权；所有权以三个独立的 `QCURL_INSTALL_HEADERS_*` 清单和 surface manifest 为准。源码树 examples / benchmarks 可以使用这些头；显式组件 gate 分别验证 opt-in 安装和 default Core 负向 consumer。
- `QCNetworkLaneKey.h` 与 `QCNetworkSchedulerPolicy.h` 进入 Core component 安装面；scheduler 配置入口收敛到 `QCNetworkAccessManager::setSchedulerPolicy()`，请求 lane 使用 typed key。`QCNetworkRequestScheduler.h` 仅保留既有观察/内部测试兼容面，不再作为下游配置 workflow。
- 旧的 manager-level 同步发送 API 已从 Core 移除；同步 value-result API 归属 Blocking Extras。

任何不在上述清单中的头文件，都不属于对下游的源码兼容承诺。

## 2. Public Header 设计准则

- public header 只暴露 QCurl 自有 API、值类型和 Qt-only 公开依赖。
- library-facing 的 QObject / 服务类统一使用 `ClassPrivate` 作为 d-pointer 私有类命名；现有嵌套 `Private` 属于迁移债务，不属于允许长期保留的目标状态。
- 若多个 public header 共享同一个轻量类型，优先拆出独立 type header（例如 `QCNetworkHttpMethod.h`），而不是让某个“大头文件”成为事实上的转运站。
- 能用 forward declaration 的地方优先 forward declaration；调试输出、字符串化和实现细节优先放到 `.cpp`。
- libcurl 语义转换必须下沉到 `.cpp` 或 `src/private/*`，不得通过 public header 把 `CURL*`、`curl_*` 或 `<curl/...>` 透传给下游。

### 2.1 Include 热点收敛

当前已收敛的第一梯队热点头文件用于降低安装面 include 成本；第二梯队（例如 `QCNetworkRequest.h`、`QCNetworkAccessManager.h`）与长尾头文件按后续独立审计处理。

- `QCNetworkReply.h`：不再直接 `#include "QCNetworkRequest.h"`，改为前置声明并把完整依赖下沉到 `.cpp`，避免 reply 头成为 request 配置面的传递依赖中枢。
- `QCNetworkReply.h` 不重复声明 `QObject::deleteLater()`；下游继续通过继承调用 `reply->deleteLater()`，派生 meta-object 和 Core 动态导出面不得出现 wrapper。
- `QCNetworkLaneKey.h` / `QCNetworkSchedulerPolicy.h`：typed lane 和 manager-level policy 独立成轻量 Core type header；reply 创建仍统一收口到 `QCNetworkAccessManager::head()/get()/post()/put()/patch()`，scheduler 队列 bookkeeping、host 计数、带宽窗口、定时器、互斥锁等实现细节留在 `.cpp`。
- `QCWebSocketPool.h`：公开头仅暴露 owner-thread-only 连接池合同；`acquire()` / `preWarm()`
  以 `QFuture<Result>` 表达逐调用完成，跨线程调用立即返回 `WrongThread` 且不创建 socket。
  `QCWebSocketAcquireResult` 只携带纯值 `LeaseId`，不保存 QObject 指针。调用方只能在原连接池
  owner thread 使用
  `[[nodiscard]] LeaseResult resolveLease(LeaseId leaseId, QCWebSocket **socket) const`
  临时解析 non-owning 指针，并通过
  `[[nodiscard]] LeaseResult release(LeaseId leaseId)` 归还同一 lease。pool 销毁、未知 lease、
  已失效 lease、错误线程和非法输出参数均返回结构化状态且不改变池状态。其他同步 mutator 使用
  `[[nodiscard]] bool clearPool(const QUrl &url = QUrl(), QString *error = nullptr)` 与
  `[[nodiscard]] bool setConfig(const QCWebSocketPoolConfig &config, QString *error = nullptr)`；失败写入
  稳定诊断且保持旧状态，成功清空可选错误输出。池内记录、映射、统计与定时器下沉到 `.cpp`，
  实现不使用 mutex 或局部事件循环，也不提供 queued mutator 路线。

### 2.2 Scheduler Core 源码兼容策略（typed lane / manager policy）

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

### 2.4 Core headers 源码与布局策略速查表（关键公开头）

| Core header | 源码与布局策略 | 允许的后续演进方式 |
|------------|----------|--------------------|
| `QCCookie.h` | `QCCookie` 使用 implicit-sharing 值类型 + accessor API；承载 Core manager cookie import/export 和 Blocking Extras cookie snapshot/delta 的 public cookie model | 可在 Data 内扩展字段并新增 accessor；不得暴露 QtNetwork、libcurl cookie line 或 public fields |
| `QCCookieAsyncResult.h` | `QCCookieOperationResult` / `QCCookieExportResult` 使用 implicit-sharing 值类型 + accessor API；作为 manager cookie async 的唯一 per-call `QFuture` 结果 | 可在 Data 内扩展字段并新增 accessor；不得恢复结果 signal、public fields 或把 blocking cookie store 行为混入 Core |
| `QCNetworkLaneKey.h` / `QCNetworkSchedulerPolicy.h` | typed lane + manager-level policy/statistics 使用值类型 + accessor API；配置通过 `QCNetworkAccessManager::setSchedulerPolicy()` | 可在 Data 内扩展字段并新增 accessor；不得恢复裸 `QString` lane 或 `manager scheduler getter + setLaneConfig` public workflow |
| `QCNetworkAccessManager.h`（scheduler 入口） | manager-owned scheduler；公开配置/观察/取消入口为 `setSchedulerPolicy()`、`schedulerStatistics()`、`cancelLaneRequests()` | 不得恢复 scheduler 指针配置 workflow 或透明跨线程阻塞 getter |
| `QCNetworkAccessManager.h`（share/hsts 配置） | `ShareHandleConfig` / `HstsAltSvcCacheConfig` 使用 implicit-sharing 值类型 + accessor API；manager 的 cookie/scheduler/cache/share/hsts 状态已下沉到 `QCNetworkAccessManagerPrivate`，不再占用导出类布局 | 可在 `Data` 内扩展字段并新增 accessor；不得恢复 public fields 或新增 layout allowlist |
| `QCNetworkRequest.h`（lane/priority 入口） | lane 使用 `QCNetworkLaneKey`，priority 保持非抢占语义；重定向/传输便捷 API 委托到 `QCNetworkRequestConfig.h` 的配置族 | 允许新增 typed 便捷 API；不得恢复 `QString` lane setter compatibility overload；Advanced 网络路径/DNS API 不进入默认 Core |
| `QCNetworkRequestConfig.h` | `QCNetworkHttpAuthConfig` / `QCNetworkRedirectConfig` / `QCNetworkTransferConfig` 使用 implicit-sharing 值类型 + accessor API；聚合认证、重定向与传输配置自然边界 | 可在 Data 内扩展字段并新增 accessor；不得为单字段拆出 public config class，也不得暴露 `CURLOPT_*` |
| `QCNetworkCachePolicy.h` | 独立轻量 enum type header；作为 `QCNetworkRequest` 的 Core 配置类型进入 Core component 安装面 | 可新增策略枚举值，但不得把 concrete cache 实现类型或读取 API 混入该头 |
| `QCNetworkCache.h` / `QCNetworkCacheRequestKey.h` | metadata / lookup / clear result / structured request key 使用 implicit-sharing 值类型 + accessor API；canonical read API 为 `lookup(const QCNetworkCacheRequestKey &, ReadMode)`，key 表达 method、规范化 URL、Vary 请求头和认证分区 | 可在 Data 内扩展字段；不得恢复仅按 URL 定位的 virtual API、void `clear()`、public fields、`contains()` / `data()` / `metadata()` 旧读路径或新增 layout allowlist |
| `QCNetworkMemoryCache.h` / `QCNetworkDiskCache.h` | QObject cache 实现使用 private data；磁盘条目仅接受有界 fixed-header v4，v3/未知格式 miss 并尝试删除；缓存容器、锁、路径和文件系统细节留在 `.cpp` | 仅通过 accessor / virtual API 扩展行为；不得在 public header 暴露 `QCache`、`QMutex`、`QDir` 等实现依赖，也不得恢复无界 `QDataStream` 容器解析 |
| `QCNetworkBody.h` | JSON / form-url-encoded body helper；不依赖 manager，不暴露 public fields | 可新增编码 helper；不得把发送、文件 I/O 或 manager 状态混入该类型 |
| `QCMultipartFormData.h` | builder 使用 implicit-sharing 值类型；字段列表、boundary 缓存和 MIME/file helper 留在 `.cpp` | 可新增 builder API；不得恢复 nested public field layout 或把文件系统 / MIME 依赖暴露到 public header |
| `QCNetworkMultipartBody.h` | multipart body helper；owning body 返回 QByteArray，streaming body 只保存值元数据和 borrowed `QPointer<QIODevice>`，由 source owner thread 在 `takeDevice()` 中 materialize wrapper 并通过 `post()` 发送；source、调用方和 parent 必须保持同一 owner thread | 可新增 multipart body 变体；不得恢复 manager-level multipart convenience |
| `QCNetworkTransferJob.h` / `QCNetworkDownloadToDeviceJob.h` / `QCNetworkResumableDownloadJob.h` | QObject job + d-pointer；`QCNetworkDownloadToDeviceJob` 与 `QCNetworkResumableDownloadJob` 构造不启动，调用方 connect 后显式 `start()`；底层 reply 只在启动成功后可用 | 可新增 job 状态 accessor；不得绕过 manager 管线或暴露 QFile/QSaveFile 内部状态 |
| `QCNetworkLogger.h` / `QCNetworkDefaultLogger.h` | `QCNetworkLoggerHandle` 是 exported opaque 值句柄，复制/移动/析构 out-of-line；共享控制块、allocator、deleter 和最终删除留在 QCurl 内。DefaultLogger 使用 private data，文件输出通过 `QCNetworkLogResult` 返回结构化状态，manager 与 reply 保留独立 handle snapshot | 可新增结果状态或 logger setter/accessor；不得在 public ABI 恢复 owning smart pointer、裸 owning logger 注入、隐藏 last-error，或删除 reply snapshot retention |
| `QCNetworkCancelToken.h` | QObject service + private data；只暴露 reply-level attach/cancel/timeout 合同 | 仅通过 reply-level API 扩展取消语义；不得新增 request-level cancel shortcut |
| `QCNetworkMiddleware.h` | base class 使用 private data 管理注册 manager；公开面只保留 virtual hook 与 manager 注册合同 | 可新增 hook 或 helper；不得把内置 middleware 的状态、容器或锁暴露到 base header |
| `QCNetworkMockHandler.h` / `QCNetworkTestSupport.h` | 显式 Test Support 安装面；`QCNetworkCapturedRequest` 使用 implicit-sharing 值类型 + accessor API；manager 绑定通过 `QCurl::TestSupport` 命名空间表达 | 不得回到 Core component 安装面；不得恢复 `MockData` / `CapturedRequest` nested public field layout；不得把生产运行时能力写入 Core 文档 |
| `QCNetworkConnectionPoolConfig.h` / `QCNetworkConnectionPoolManager.h` | config/statistics 使用 implicit-sharing 值类型 + accessor API；manager 使用 private data；内部 curl handle 协作位于 `_p.h` | 可在 Data 内新增字段并增加 accessor；不得恢复 public fields、`QMutex`、`QHash` 或内部 helper 到 public header |

## 3. 不属于安装面的 internal/private 头

以下头文件属于库内实现细节，不安装给 consumer：

- `_p.h` / private：`QCNetworkAccessManager_p.h`、`QCNetworkReply_p.h`、`QCNetworkConnectionPoolManager_p.h`、`QCWebSocket_p.h`、`qbytedata_p.h`、`src/private/QCurlRuntimeState_p.h`、`src/private/QCNetworkLogRedaction_p.h`
- internal curl plumbing：curl handle / multi manager、feature probe 与 utility helper 头文件
- internal pipeline / adapters：`src/private/*`

当前 install surface 审计已确认以下收口对象：

- `QCNetworkConnectionPoolManager::{configureCurlHandle, recordRequestCompleted}` → 已迁移到 `QCNetworkConnectionPoolManager_p.h`
- `QCurlRuntimeState` → 作为 `QCurlRuntime.h` 中的 public 状态枚举随 runtime controller 进入安装面；
  `src/private/QCurlRuntimeState_p.h` 仅保存 registry、lease 和 participant 实现。
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

- Core-only component stage 的 `<staging>/include/qcurl/` 头文件集合必须与 `QCURL_INSTALL_HEADERS + QCurlConfig.h` **完全一致**；无过滤完整安装还必须逐文件归属到四个交付组件或 package metadata。
- 不允许多装 internal/private 头，也不允许漏装 manifest 中的 public 头。
- `QCPimpl.h` 不得再出现在 staging include 目录或默认 manifest 中。
- 非默认组件不进入 Core-only staging contract；Blocking Extras、Other Extras 与 Test Support 各自通过独立 manifest、install component、opt-in consumer smoke 和 default Core 负向 consumer gate 验证。
- Blocking Extras 只有 `BlockingExtrasDevelopment`，其实现随 Core Runtime 交付；Other Extras 具有 `OtherExtrasRuntime` / `OtherExtrasDevelopment`；Test Support 只有 `TestSupportDevelopment`，不得被包装为生产 Runtime。

### 5.2 导出目标

- 安装后的 package 对无组件 consumer 暴露 `QCurl::QCurl`；Blocking Extras、Test Support 与 Other Extras 通过 `QCurl::BlockingExtras`、`QCurl::TestSupport`、`QCurl::OtherExtras` opt-in target 暴露。无过滤 `cmake --install` 会部署所有已构建组件，不等于 consumer 自动加载所有 target。
- `QCurl::BlockingExtras` 必须是显式 opt-in 的 `INTERFACE` target，并只转发 `QCurl::QCurl`；其实现和动态符号归入 Core，不得生成独立 `libQCurlBlockingExtras`。`QCurl::OtherExtras` 必须是独立编译库，`QCurl::TestSupport` 必须是开发静态库。
- `QCurlTargets*.cmake` 不得定义或引用 `QCurl::libcurl_shared`。
- shared 构建下，`QCurl::QCurl` 的公开接口不得泄漏 `CURL::libcurl` 或 `ZLIB::ZLIB`。
- static 构建下，`QCurl::QCurl` 只能通过 public link interface 暴露 Core 必需的 `CURL::libcurl`；`QCurlConfig.cmake` 必须提供 `find_dependency(CURL ...)`，但默认 Core export 不再暴露 `ZLIB::ZLIB`。
- 当 `QCURL_BUILD_LIBCURL_CONSISTENCY=ON` 时，bundled `libcurl_shared` 只允许作为 staging/runtime 细节存在，不进入 public package/export 合同。
- Core 内部的 Blocking Extras 实现只能通过非安装 `_p.h` 中的 `QCBlockingHandleBridge` 及协议/运行时 helper 复用 Core 细节；这些符号保持 DSO 内隐藏。Core 向独立编译组件导出的 private bridge 只允许包含 Other Extras 使用的 persistent-transfer token register/remove，以及 Test Support 使用的 `installNetworkMockProvider`。这些导出符号是同一 package source identity 的锁步链接合同，不是 public API；精确 owner 清单由 `scripts/qcurl_abi_symbols.py` 门禁。

### 5.3 Consumer Smoke

- 正向 consumer：独立工程只能通过 staging prefix 执行 `find_package(QCurl CONFIG REQUIRED)`，随后 include public headers 并链接 `QCurl::QCurl` 成功。
- cookie value 与 async result 作为 Core 值结果时，正向 consumer fixture 必须持续覆盖 `<QCCookie.h>`、`<QCCookieAsyncResult.h>`、`QCCookie` accessor、`QCCookieOperationResult::success()/failure()`、`QCCookieExportResult::success()/failure()`、`isSuccess()`、`errorCode()`、`policyCode()`、`error()` 和 `cookies()`，且不得要求 default consumer 链接 QtNetwork。
- 正向 Core consumer fixture 覆盖 `<QCNetworkLaneKey.h>`、`<QCNetworkSchedulerPolicy.h>`、typed lane、manager-level `setSchedulerPolicy()` / `schedulerPolicy()` / `schedulerStatistics()` 和 policy/lane accessor API；不得覆盖或恢复 `manager scheduler getter` 配置 workflow。
- logger 作为 Core 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkLogger.h>`、`NetworkLogEntry` accessor API、`QCNetworkLogResult`、`QCNetworkLoggerHandle::create()` / `createWithBorrow()`，以及 `manager.setLogger()` / `logger()` / `setDebugTraceEnabled()` / `debugTraceEnabled()`；public headers 不得出现 logger owning smart pointer。
- cache policy 作为 Core type header 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkCachePolicy.h>` 以及 `QCNetworkRequest::setCachePolicy()` / `cachePolicy()`。
- Cache lookup 作为 Core 能力时，正向 consumer fixture 必须持续覆盖 `<QCNetworkCache.h>`、`<QCNetworkCacheRequestKey.h>`、`<QCNetworkMemoryCache.h>`、`<QCNetworkDiskCache.h>`、`QCNetworkCacheMetadata` accessor API、`lookup(cacheKey, QCNetworkCacheReadMode::FreshOnly)`、`QCNetworkCacheLookupResult` accessor API 和 `QCNetworkCacheClearResult` 的状态、计数、残留字节与稳定错误 accessor。
- Body / Multipart / transfer job 作为 Core 能力时，正向 consumer fixture 必须持续覆盖 `<QCNetworkBody.h>`、`<QCMultipartFormData.h>`、`<QCNetworkMultipartBody.h>`、`<QCNetworkDownloadToDeviceJob.h>`、`<QCNetworkResumableDownloadJob.h>`、JSON/form body helper、multipart `fromFormData()`、`contentType()`、`data()`、`size()`、`toByteArray()`、`fieldCount()` 和 job type 可见性。
- default logger 作为 Core helper 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkDefaultLogger.h>`、`createWithBorrow()`、`enableConsoleOutput(false)`、`setMinLogLevel()`、`entries()`、结构化 `log()` 结果和 `manager.setLogger(defaultLogger)`。
- cancel token 作为 Core helper 时，正向 consumer fixture 必须持续覆盖 `<QCNetworkCancelToken.h>`、`attach(QCNetworkReply *)`、`attachMultiple(QList<QCNetworkReply *>)`、`setAutoTimeout()`、`cancel()` 和 `isCancelled()`。
- reply 生命周期 consumer 必须同时证明 `reply->deleteLater()` 继承调用可编译，且 `deleteLater()` 的 meta-object method index 位于 `QCNetworkReply::methodOffset()` 之前；动态符号 allowlist 必须拒绝派生 wrapper 导出符号。
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
- `qcurl_public_api_surface_manifest`：校验机器可读发布面清单与 CMake 生成的四组件 manifest 对齐，并验证 maturity / visibility 正交属性
- `public-api-slow`：default Core staging install、安装集合校验、导出合同校验、isolated Core consumer smoke，以及 Blocking Extras / Test Support / Other Extras 的 opt-in install / consumer smoke 与 default Core 负向 consumer
- static opt-in 路径还必须在 `QCURL_BUILD_SHARED_LIBS=OFF` 构建目录中重跑上述 `public-api` 与 `public-api-slow`，验证 static install/export/consumer contract。

> 说明：CTest 的 label 参数是正则；为了避免 `public-api` 误匹配 `public-api-slow`，本仓库文档统一使用带锚点的写法。
