# QCurl 对 legendary-python Qt/C++ 移植项目的技术尽调与可行性评估（可估价/可排期）

生成时间：2026-01-02（更新：2026-01-06）  
范围与证据口径：本结论严格基于仓库可见证据（`legendary-python/README.md`、`legendary-python/legendary/**`、`src/**`、`tests/**`），不引入外部网络信息；当信息不足时会明确标注“不足以判断”及建议补充的文件/用例。  
已执行的本地验证（更新：2026-01-06）：在现有构建目录 `build/` 运行 `ctest --output-on-failure`，38/38 通过；本环境运行 `python -m pytest -q tests/libcurl_consistency/test_env_smoke.py` 仍受端口/权限限制失败（PermissionError: Operation not permitted），建议在具备端口绑定权限的 CI runner 上执行（见 `.github/workflows/*gate*.yml`）；受限环境的关键语义回归门禁已补齐为纯离线 QtTest（见第 3 节）。

## 1) 需求映射

### 1.1 以 legendary 的文档与代码为准提炼的网络能力清单（按功能域归类）

从 `legendary-python/README.md` 的关键词密度与命令域（auth/login/token/refresh/webview/session/download/install/manifest/verify/cloud/save/timeout 等）以及实际代码路径 `legendary-python/legendary/api/egs.py`、`legendary-python/legendary/api/lgd.py`、`legendary-python/legendary/downloader/mp/manager.py`、`legendary-python/legendary/downloader/mp/workers.py`、`legendary-python/legendary/utils/webview_login.py` 可以归纳：移植版必然需要大量 HTTPS REST 调用（OAuth 与普通 API）、少量 GraphQL（在 `legendary-python/legendary/api/egs.py` 有 graphql 关键词与 host 模板），以及高吞吐下载（以 manifest/chunk 为核心概念，关键词与模型分布在 `legendary-python/legendary/models/manifest.py`、`legendary-python/legendary/models/chunk.py`，并由 `legendary-python/legendary/downloader/mp/*` 驱动）。同时，legendary 当前 Python 实现侧重 `requests.session()`（代码扫描结果显示 `requests.session()` 出现在 `legendary-python/legendary/api/egs.py`、`legendary-python/legendary/api/lgd.py`、`legendary-python/legendary/downloader/mp/workers.py`），并普遍设置超时参数（`timeout=` 分布在 6 个文件中，含上述 API 与 downloader 文件）。

因此，“移植到 Qt/C++ 时必须由网络库承担”的最低集合应覆盖：HTTP 请求构造与发送、连接复用/并发、超时/取消、TLS 安全默认、代理与证书策略、下载/上传流式 I/O、错误模型与可观测性（至少日志与诊断）。而 OAuth/设备码登录、token 刷新、manifest 解析与 chunk 选择/校验等更偏业务语义，推荐由上层实现（见第 6 节的边界建议）。

#### 1.1.1 补充：legendary 侧未在初版覆盖的网络细节

进一步扫描 `legendary-python/legendary/**` 后，发现若干初版未显式纳入的网络行为与隐含约束。

其一，`legendary-python/legendary/api/egs.py` 显式配置 urllib3 连接池与重试（`HTTPAdapter(pool_maxsize=16)`；`Retry(total=3, backoff_factor=0.1, status_forcelist=[500..504])`，并 `mount('https://', ...)`），这意味着移植版需要可配置的连接并发上限与“按 HTTP 5xx 状态码”重试策略（不止网络错误）；同时该文件还包含 `get_signed_chunk_urls()`，与下载器侧的“签名分块 URL”工作流（`legendary-python/legendary/downloader/mp/manager.py`）强关联。

其二，下载器支持绑定本地源 IP 并轮转（`legendary-python/legendary/downloader/mp/workers.py` 使用 `source_address`；`legendary-python/legendary/downloader/mp/manager.py` 轮转 `bind_ip`；CLI `legendary-python/legendary/cli.py` 暴露 `--bind`），对应到 QCurl 侧可用 `CURLOPT_INTERFACE`（`src/QCNetworkRequest.h`/`src/QCNetworkReply.cpp` 的映射证据已存在）。

其三，存在 CDN 选择与 HTTP 降级开关（`legendary-python/legendary/core.py` 的 `preferred_cdn`、`disable_https` 以及 `https->http` 重写），要求移植版在“允许 http”与“证书/TLS 策略”之间有明确边界、默认值与可观测性（例如日志标注降级原因）。

其四，Web 登录链路包含 XSRF cookie→header 的 CSRF 交换流程（`legendary-python/legendary/core.py` 访问 `id/api/csrf` 并发送 `X-XSRF-TOKEN`；同时依赖 cookie 与 `X-Epic-*`/`X-Requested-With` 等头），要求网络层至少具备 cookie jar 与 header 注入；若使用 Qt WebView 登录，则需额外设计 cookie SSOT 与互通方案（详见第 6/7 节）。

### 1.2 逐项映射：legendary 需求 ↔ QCurl 现状（实现位置与成熟度）

下表中的“成熟度”采用可审计证据口径：实现文件存在且关键 libcurl 选项/结构出现（来自对 `src/**` 的静态扫描，QCurl 共使用 97 个 `CURLOPT_*` 选项，见关键实现集中在 `src/QCNetworkReply.cpp`）；并且有对应测试文件且 `ctest` 已通过（对应 `tests/tst_*.cpp` 的覆盖与执行结果）。

| 功能域 | legendary 需求证据（文档/代码） | 移植时归属 | QCurl 对应实现（模块/文件证据） | 测试覆盖证据（tests）与成熟度 | 现状判定 |
|---|---|---|---|---|---|
| 协议：HTTP/HTTPS 基础 | `legendary-python/legendary/api/egs.py`、`legendary-python/legendary/api/lgd.py` 使用 `requests.session()`；`legendary-python/README.md` 有 api/timeout 关键词 | 网络库必须 | `src/QCNetworkAccessManager.*`、`src/QCNetworkRequest.h`、`src/QCNetworkReply.*`；核心 curl 设置集中在 `src/QCNetworkReply.cpp`（含 `CURLOPT_URL`/`CURLOPT_HTTPHEADER` 等） | `tests/tst_QCNetworkRequest.cpp`、`tests/tst_QCNetworkReply.cpp`，且 `ctest` 通过；成熟度高 | 已满足 |
| 协议：HTTP/2 | 非显式需求，但可能提升下载并发/性能 | 网络库可选（对业务透明） | `src/QCNetworkHttpVersion.*`、`src/QCNetworkConnectionPoolManager.cpp` 与 `CURLOPT_HTTP_VERSION`/`CURL_HTTP_VERSION_*` 的使用 | `tests/tst_QCNetworkHttp2.cpp` 且 `ctest` 通过；成熟度高 | 已满足 |
| 协议：HTTP/3 | 非显式需求；对跨平台交付风险更高 | 网络库可选（建议可降级） | `src/QCNetworkHttpVersion.*`、`src/CurlFeatureProbe.*`；HTTP/3 相关 token 出现在 `src/QCNetworkHttpVersion.cpp` | `tests/tst_QCNetworkHttp3.cpp`（离线：降级/失败语义）且 `ctest` 通过；交付门禁：`QCURL_REQUIRE_HTTP3=1` + `.github/workflows/release_delivery_http3_gate.yml`；成熟度中（成功路径依赖 curl/QUIC 与 curl testenv） | 已满足（运行时能力探测+降级策略已固化；支持 `QCURL_REQUIRE_HTTP3=1` 强制交付口径） |
| 协议：WebSocket | legendary 当前代码未发现 websocket 使用（对 `legendary-python/legendary/**` 扫描无命中），可能仅用于未来 UI | 上层可选 | `src/QCWebSocket.*`、`src/QCWebSocketPool.*`、`src/QCWebSocketCompressionConfig.*`、`src/QCWebSocketReconnectPolicy.*`，并包含 `CURLWS_*`/`CURLOPT_WS_OPTIONS` 相关实现 | `tests/tst_QCWebSocket.cpp`、`tests/tst_QCWebSocketPool.cpp`、`tests/tst_QCWebSocketCompression.cpp` 且 `ctest` 通过；成熟度高 | 已满足（但对 legendary 移植非阻塞） |
| 请求方法：GET/POST/PUT/DELETE/PATCH/HEAD 等 | API 调用必然覆盖 GET/POST；其余取决于端点 | 网络库必须 | `src/QCNetworkReply.cpp` 含 `CURLOPT_CUSTOMREQUEST`/`CURLOPT_POSTFIELDS`/`CURLOPT_UPLOAD` 等；`src/QCNetworkRequest.h` 提供请求级配置入口 | `tests/libcurl_consistency/test_p1_http_methods.py`（存在但当前环境未执行到，见第 3 节）；C++ 侧有 `tests/tst_QCNetworkRequest.cpp`；成熟度中到高 | 已满足（但建议补齐“方法 × 重定向/重试”组合用例的自动化回归门禁） |
| 认证：Bearer Token | `legendary-python/README.md` 有 token/refresh；API 必然需要 `Authorization: Bearer` | 上层负责 token 生命周期；网络库必须支持 header 注入 | `src/QCNetworkRequest.h`/`src/QCNetworkRequestBuilder.*`/`src/QCRequestBuilder.h` 存在 `Authorization` 相关字符串；`src/QCNetworkReply.cpp` 支持 `CURLOPT_HTTPHEADER` | `tests/tst_QCNetworkMiddleware.cpp`（用于统一注入 header 的天然位置）；成熟度高 | 已满足 |
| 认证：HTTP Basic/Digest（或代理认证） | OAuth token 端点依赖 Basic 认证（`legendary-python/legendary/api/egs.py`：`auth=self._oauth_basic`）；企业网络也可能需要 | 网络库可选（代理认证更重要） | `src/QCNetworkRequest.h` 定义 `QCNetworkHttpAuthConfig`/`QCNetworkHttpAuthMethod`；`src/QCNetworkReply.cpp` 使用 `CURLOPT_HTTPAUTH`、`CURLOPT_USERNAME`、`CURLOPT_PASSWORD` | `tests/tst_Integration.cpp`（可选：需本地 httpbin，缺失时会 skip）；`tests/libcurl_consistency/test_p1_httpauth.py`（Linux gate 覆盖）；成熟度高 | 已满足 |
| 会话与 Cookie：内存会话 | Web 登录链路依赖 cookie 与 XSRF：`legendary-python/legendary/core.py` 使用 `EPIC_COUNTRY`、`XSRF-TOKEN` 并将其写入 `X-XSRF-TOKEN` 头；`legendary-python/legendary/utils/webview_login.py` 提示不同 WebView 后端的 cookie 持久化差异 | 网络库必须（至少 cookie jar 能力）；上层负责策略 | `src/QCNetworkAccessManager.h` 提供 `setCookieFilePath/cookieFilePath`；并提供 cookie bridge：`QCNetworkAccessManager::importCookies/exportCookies/clearAllCookies`（需 `shareCookies`）；`src/QCNetworkReply.cpp`/`src/QCCurlMultiManager.cpp` 使用 `CURLOPT_COOKIEFILE`/`CURLOPT_COOKIEJAR`/`CURLOPT_COOKIELIST` | `tests/tst_QCNetworkCookieBridge.cpp`（离线：import/export/clear）与 `tests/tst_QCNetworkMiddlewareIntegration.cpp`（离线：XSRF cookie→header 且不覆盖显式 header）；另有 `tests/libcurl_consistency/test_p1_cookiejar_1903.py`、`tests/libcurl_consistency/test_p2_cookie_request_header.py`（Linux gate 覆盖）；成熟度高 | 已满足（QCurl 侧已提供 cookie jar + bridge API + XSRF middleware 门禁；Qt WebView cookie store 集成属上层，见第 6/7 节） |
| 重定向策略 | 登录/授权跳转常见 | 网络库必须 | `src/QCNetworkRequest.h` 提供 follow/maxRedirect/postRedirectPolicy 等；`src/QCNetworkReply.cpp` 使用 `CURLOPT_FOLLOWLOCATION`/`CURLOPT_MAXREDIRS`/`CURLOPT_POSTREDIR`，并读取 `CURLINFO_REDIRECT_URL`（见 `src/QCCurlMultiManager.cpp`） | `tests/libcurl_consistency/test_p1_redirect_policy.py`、`tests/libcurl_consistency/test_p1_redirect_and_login_flow.py`（存在但当前环境门禁）；成熟度中到高 | 已满足（建议把重定向与“敏感头处理”作为验收项固化） |
| 代理：HTTP/HTTPS/SOCKS | 企业环境与地域网络常见 | 网络库可选但建议支持 | `src/QCNetworkProxyConfig.*`；`src/QCNetworkReply.cpp` 使用 `CURLOPT_PROXY*` 系列（含 `CURLOPT_PROXYUSERNAME`/`CURLOPT_PROXYPASSWORD`/`CURLOPT_PROXY_SSL_*` 等） | `tests/tst_QCNetworkProxy.cpp` 且 `ctest` 通过；另有 `tests/libcurl_consistency/test_p1_proxy.py`、`test_p2_socks5_proxy_fail.py`；成熟度高 | 已满足 |
| TLS/证书：校验默认值 | 任何 Epic API/下载都要求安全默认 | 网络库必须 | `src/QCNetworkSslConfig.cpp` 提供 `defaultConfig()` 与 `insecureConfig()`；`src/QCNetworkReply.cpp`/`src/QCWebSocket.cpp` 使用 `CURLOPT_SSL_VERIFYPEER`/`CURLOPT_SSL_VERIFYHOST`、`CURLOPT_CAINFO` | `tests/tst_QCNetworkHttp2.cpp`、`tests/tst_QCNetworkHttp3.cpp` 与 `ctest` 通过提供了 TLS 路径级证据；成熟度高 | 已满足 |
| TLS：证书钉扎（pinning） | 非必需，但对抗劫持/企业合规可能需要 | 网络库可选 | `src/QCNetworkSslConfig.h` 与 `src/QCNetworkReply_p.h` 存在 `CURLOPT_PINNEDPUBLICKEY` | `tests/libcurl_consistency/test_p2_tls_pinned_public_key.py`（Linux gate 覆盖；capability-gated）；成熟度中 | 已满足（pinned public key 可运行回归门禁已具备） |
| 压缩：gzip/br 等 | Epic API 常用压缩；下载也可能 | 网络库可选但建议支持 | `src/QCNetworkRequest.h` 与 `src/QCNetworkReply.cpp` 使用 `CURLOPT_ACCEPT_ENCODING` | `tests/tst_QCNetworkRequest.cpp`（API 契约）+ `tests/libcurl_consistency/test_p1_accept_encoding.py`（Linux gate 覆盖）；成熟度高 | 已满足（Accept-Encoding/自动解压契约与可运行门禁已具备） |
| 限速：发送/接收 | 大规模下载时的 QoS | 网络库可选 | `src/QCNetworkRequest.h` 提供 `setMaxDownloadBytesPerSec/setMaxUploadBytesPerSec` 并在 `src/QCNetworkReply.cpp` 绑定 `CURLOPT_MAX_RECV_SPEED_LARGE`/`CURLOPT_MAX_SEND_SPEED_LARGE` | `tests/tst_QCNetworkRequest.cpp`（API 契约：参数合法性/禁用语义）；成熟度中 | 已满足 |
| 重试与退避：网络错误 | Epic API 与下载链路需要韧性 | 网络库必须（至少提供策略钩子） | `src/QCNetworkRetryPolicy.*`；`src/QCNetworkRequest.h` 有 `setRetryPolicy` 等入口 | `tests/tst_QCNetworkRetry.cpp` 且 `ctest` 通过；成熟度高 | 已满足 |
| 重试与退避：HTTP 5xx（500-504） | API 侧显式配置按 5xx 重试（`legendary-python/legendary/api/egs.py`：`Retry(status_forcelist=[500,501,502,503,504], allowed_methods={'GET'})`） | 上层与网络库均可；建议网络库提供通用机制，上层配置策略 | QCurl 在执行路径中基于 `CURLINFO_RESPONSE_CODE` 将 HTTP 4xx/5xx 映射为 `NetworkError` 并走统一重试逻辑（`src/QCCurlMultiManager.cpp` 与 `src/QCNetworkReply.cpp`）；策略层用 `QCNetworkRetryPolicy::shouldRetry(NetworkError, attemptCount)` 判定；默认 policy 已包含 500/501/502/503/504；并新增 opt-in 的 `QCNetworkRetryPolicy::retryHttpStatusErrorsForGetOnly` 用于对齐 legendary 的 `allowed_methods={'GET'}` | `tests/tst_QCNetworkRetryOffline.cpp` 覆盖 500/503/501 的离线重试语义；`tests/tst_QCNetworkRetry.cpp` 仍提供 httpbin 集成验证（环境缺失时会跳过） | 已满足 |
| 重试与退避：HTTP 429 / Retry-After | Epic 侧限流时决定稳定性（429 + `Retry-After`） | 上层与网络库均可实现；建议网络库提供通用机制，上层配置策略 | 已补齐：`src/QCNetworkError.h`/`src/QCNetworkError.cpp` 增加 `HttpTooManyRequests(429)` 与命名描述；`src/QCNetworkRetryPolicy.*` 默认将 429 纳入 `retryableErrors`，并新增 `delayForAttempt(attemptCount, serverDelay)` 覆写入口（以 `maxDelay` cap）；`src/QCNetworkReply.cpp`（sync+mock）与 `src/QCCurlMultiManager.cpp`（async）解析 `Retry-After`（delta-seconds 与 HTTP-date）并优先使用；另提供 opt-in 的 `retryHttpStatusErrorsForGetOnly` 以对齐 legendary 的 `allowed_methods={'GET'}` | 新增纯离线门禁 `tests/tst_QCNetworkRetryOffline.cpp` 覆盖 429+Retry-After（delta-seconds / HTTP-date）覆写与回退指数退避；`tests/tst_QCNetworkMockHandler.cpp` 覆盖 MockHandler 自动生效与序列/捕获能力 | 已满足 |
| 并发/队列/优先级 | legendary 同时在 API 侧配置连接池并在下载侧并发：`legendary-python/legendary/api/egs.py` 使用 `HTTPAdapter(pool_maxsize=16, pool_connections=16)`；下载器为 mp worker（`legendary-python/legendary/downloader/mp/*`） | 网络库必须提供并发基础，上层定义调度策略 | `src/QCNetworkRequestScheduler.*`、`src/QCNetworkRequestPriority.h`、`src/QCNetworkConnectionPoolManager.*`、`src/QCNetworkConnectionPoolConfig.h` | `tests/tst_QCNetworkScheduler.cpp`、`tests/tst_QCNetworkConnectionPool.cpp`、`tests/tst_QCNetworkShareHandle.cpp` 且 `ctest` 通过；成熟度高 | 已满足 |
| 流式下载/写盘/进度 | 下载涉及 CDN manifest 与签名 chunk URL：`legendary-python/legendary/core.py` 的 `get_cdn_manifest/get_cdn_urls`、`preferred_cdn`、`disable_https`；`legendary-python/legendary/api/egs.py` 的 `get_signed_chunk_urls()`；下载器 `legendary-python/legendary/downloader/mp/manager.py` 消费 signed URL | 网络库必须 | `src/QCNetworkReply.h` 暴露 `readyRead`/`downloadProgress` 等信号与回调设置入口；`src/QCNetworkReply.cpp` 实现写入回调；另有 `src/QCNetworkDiskCache.*` | `tests/tst_QCNetworkFileTransfer.cpp`、`tests/tst_QCNetworkIODeviceLifetime.cpp` 且 `ctest` 通过；成熟度高 | 已满足 |
| 断点续传：Range/Resume | legendary 侧目前更偏 chunk 级 resume（代码未检出 `Range` 头），但网络库具备更通用能力有益 | 上层定义 resume 粒度；网络库提供 Range/seek 能力 | `src/QCNetworkRequest.h` 提供 `setRange`；`src/QCNetworkReply.cpp` 出现 `CURLOPT_RANGE`；上传侧有 seek 回调 | `tests/libcurl_consistency/range_resume_baseline_client.cpp` 与 `tests/libcurl_consistency/test_p2_pause_resume*.py`（存在但门禁）；成熟度中到高 | 已满足（建议对“下载 resume + 校验”在上层落地） |
| 超时：连接/总时长/低速断开 | legendary 大量 `timeout=` 使用（见 `legendary-python/legendary/api/egs.py` 等） | 网络库必须 | `src/QCNetworkTimeoutConfig.*` 与 `src/QCNetworkReply.cpp` 使用 `CURLOPT_TIMEOUT_MS`/`CURLOPT_CONNECTTIMEOUT_MS`/`CURLOPT_LOW_SPEED_*` | `tests/libcurl_consistency/test_p1_timeouts.py`（门禁）；C++ 侧无专门命名 timeout 测试但 `ctest` 通过多项网络用例；成熟度高 | 已满足 |
| 取消语义 | 下载取消、登录取消等 | 网络库必须 | `src/QCNetworkCancelToken.*`，并在 `src/QCNetworkReply.h` 暴露 `cancel`/`pause`/`resume` 等 | `tests/tst_QCNetworkCancelToken.cpp` 且 `ctest` 通过；成熟度高 | 已满足 |
| 错误模型：HTTP 状态码/底层错误映射 | 上层需要可稳定分流重试/提示 | 网络库必须 | `src/QCNetworkError.*`；`src/QCNetworkReply.cpp` 使用 `CURLINFO_RESPONSE_CODE` 等；无异常（`src/**` 未检出 `throw`） | `tests/tst_QCNetworkError.cpp` 且 `ctest` 通过；成熟度高 | 已满足 |
| 日志与可观测性：请求/响应/诊断 | 迁移调试与线上定位必须 | 网络库必须提供基础，上层决定采样与落盘 | `src/QCNetworkLogger.*`、`src/QCNetworkDiagnostics.*`、`src/QCNetworkLogRedaction.*`（脱敏工具）；并可通过 middleware 输出结构化观测事件（见 `src/QCNetworkMiddleware.*`） | `tests/tst_QCNetworkLogger.cpp`、`tests/tst_QCNetworkDiagnostics.cpp`、`tests/tst_QCNetworkUnifiedPolicyMiddlewareOffline.cpp` 且 `ctest` 通过；成熟度高 | 已满足 |
| 中间件/拦截器（注入 auth、统一重试策略、日志/观测） | legendary 的 session/header 语义在移植时需要统一注入点 | 网络库建议提供钩子，上层配置 | 已在 manager 发起路径自动接入：`QCNetworkAccessManager` 的 `send* / send*Sync / schedule*` 在 reply 构造前调用 `onRequestPreSend`，在 reply 构造后/execute 前调用 `onReplyCreated`，并在 `QCNetworkReply::finished` 时调用 `onResponseReceived`（见 `src/QCNetworkAccessManager.cpp`）；调度器支持显式 `replyParent` 绑定（见 `src/QCNetworkRequestScheduler.*`）；离线回放/捕获由 MockHandler 提供（`src/QCNetworkReply.cpp`）。标准 middleware 已落地：`QCUnifiedRetryPolicyMiddleware`（默认重试注入，显式优先：`QCNetworkRequest::isRetryPolicyExplicit()`）、`QCRedactingLoggingMiddleware`（脱敏日志）、`QCObservabilityMiddleware`（结构化观测；依赖 `QCNetworkReply::method/httpStatusCode/durationMs` 一致性） | `tests/tst_QCNetworkMiddlewareIntegration.cpp`（纯离线断言：执行链路自动接入 + scheduler 路径一致性）、`tests/tst_QCNetworkUnifiedPolicyMiddlewareOffline.cpp`（默认重试注入/显式禁用不覆盖/脱敏日志/观测字段）、`tests/tst_QCNetworkMockHandler.cpp` | 已满足（作为移植项目统一注入点可用；需遵守 middleware 生命周期契约，避免悬空指针） |
| multipart/form-data（上传/表单） | 云存档上传或某些 API 可能用到 | 网络库必须 | `src/QCMultipartFormData.*` | `tests/tst_QCMultipartFormData.cpp` 且 `ctest` 通过；成熟度高 | 已满足 |
| 上传：流式/seek 约束 | 大文件/断点上传时常见 | 网络库可选但建议 | `src/QCNetworkReply.h` 暴露 seek/progress 回调设置入口；`src/QCNetworkReply.cpp` 使用 `CURLOPT_UPLOAD`/`CURLOPT_READDATA` | `tests/tst_QCNetworkStreamUpload.cpp` 且 `ctest` 通过；成熟度高 | 已满足 |
| 网络路径：绑定源地址/网卡（bind_ip/source_address） | downloader 支持 `--bind` 绑定源 IP 并轮转（`legendary-python/legendary/downloader/mp/*`、`legendary-python/legendary/cli.py`） | 网络库必须提供“绑定接口/源地址”能力，上层决定策略 | `src/QCNetworkRequest.h` 提供 `setNetworkInterface/networkInterface` 并映射 `CURLOPT_INTERFACE`；`src/QCNetworkReply.cpp` 设置 `CURLOPT_INTERFACE` | `tests/tst_QCNetworkNetworkPath.cpp` 且 `ctest` 通过；成熟度高 | 已满足 |
| 跨平台差异：证书/代理/网络路径 | legendary 运行在 Win/macOS/Linux；网络库必须在发布包内“策略可控” | 网络库必须提供可配置项，上层负责落地策略 | 代码层面平台条件主要出现在 `src/QCNetworkDiagnostics.cpp`；发布集成相关在 `CMakeLists.txt`、`src/CMakeLists.txt`、`qcurl.pc.in`、`src/qcurl.pri` | Linux gate 可复现：`.github/workflows/libcurl_consistency_ext_gate.yml`、`.github/workflows/release_delivery_http3_gate.yml`（artifacts：`build/libcurl_consistency/reports/`）；本机 `ctest` 通过；但对 Win/mac 多平台 CI 与证书打包策略不足以判断 | 部分满足（Linux 侧证据已可复现；Win/mac 发布一致性与 CA/代理策略验收不在本次范围） |

## 2) 架构与接口评估

QCurl 的 `src/` 显示其架构定位非常明确：以 libcurl multi 为核心实现、以 Qt 的 QObject/信号槽作为异步抽象层，整体 API 形态接近 QtNetwork（存在 `QCNetworkAccessManager`/`QCNetworkRequest`/`QCNetworkReply` 三件套，分别位于 `src/QCNetworkAccessManager.*`、`src/QCNetworkRequest.h`、`src/QCNetworkReply.*`）。这对 Legendary 的 Qt/C++ 移植项目非常关键，因为上层可以用“请求对象 + reply 异步回调/信号”的方式替代 Python `requests.session()` 的习惯用法，并把 auth/header/cookie 等统一放到 manager 或上层 wrapper（例如 EgsClient）中实现（`src/QCNetworkRequestBuilder.*`、`src/QCRequestBuilder.*` 可直接注入 header）。需要注意的是：`src/QCNetworkMiddleware.*` 已在 `QCNetworkAccessManager` 的 `send* / send*Sync / schedule*` 路径中自动接入（请求前/响应后），并配套了纯离线门禁 `tests/tst_QCNetworkMiddlewareIntegration.cpp`。因此若移植项目计划依赖 middleware 作为统一注入点，当前可直接使用（仍需遵守 middleware 生命周期契约）。

### 2.1 补充：schedule* 的 pre-send 时机与“出队/真正开始传输时再执行 pre-send”的可行性（待决）

> 结论（本次范围）：late pre-send 不纳入本次 QCurl 交付；若移植项目确需“真正开始传输前注入最新 header”，建议作为独立变更评估（涉及 reply 创建时机/重配置与调度器锁重入风险）。

当前行为的关键证据链如下：`QCNetworkAccessManager::scheduleGet/schedulePost/schedulePut` 会在调用 `QCNetworkRequestScheduler::scheduleRequest` 之前执行 `QCNetworkMiddleware::onRequestPreSend`，随后调度器在出队时通过 `QCNetworkRequestScheduler::startRequest` 直接调用 `QCNetworkReply::execute()` 开始传输。这意味着 pre-send 发生在“创建 reply 并入队”时，队列等待期间不会再次触发（`src/QCNetworkAccessManager.cpp`、`src/QCNetworkRequestScheduler.cpp`）。

需要注意的是，`QCNetworkReply` 在构造函数内立刻调用 `QCNetworkReplyPrivate::configureCurlOptions()`，而该函数会读取 `QCNetworkRequest` 的 headers 并构建 `CURLOPT_HTTPHEADER` 等易句柄配置（`src/QCNetworkReply.cpp`）。因此，即便在出队时再跑一次 `onRequestPreSend` 并修改 request，对真实网络请求也不会生效：curl 的关键选项已经在构造期固化。

如果移植项目确实存在“token 在队列等待期间可能刷新，必须在真正开始传输前注入最新 header”这类需求（例如 OAuth token 刷新、临近发送签名等），QCurl 侧可考虑新增“额外 API/模式”以支持 late pre-send，但实现必须同时解决“延迟配置或重配 curl easy handle”的问题，并规避调度器锁的重入风险。可行路径大致有两类。

第一类是延迟创建 reply：调度器队列存储 request/method/body/priority 等元数据，在出队/启动前执行 pre-send 后再创建 `QCNetworkReply`（从而让 configureCurlOptions 读取到最新 headers）。这会改变 schedule* 的返回值语义（可能需要返回占位句柄或可取消的 ticket），属于较大 API 改动。

第二类是在现有 reply 结构上增加“启动前重配”能力：在 reply 仍处于 Idle（未 execute）时，允许替换 request（或至少替换 headers/URL 等可变部分），并在 `execute()` 前重建 header slist/相关 curl option。该方案 API 侵入较小，但需要严格限定可变字段、线程亲和与状态机边界，否则容易引入双重设置、资源泄漏或与重试逻辑冲突。

此外，调度器当前会在持有内部互斥锁的路径上调用 `startRequest()->reply->execute()`（`src/QCNetworkRequestScheduler.cpp`），若 late pre-send 的 middleware 在执行过程中触发新的 schedule/config/cancel 等操作，存在自锁或重入风险。若后续实现 late pre-send，建议把“出队触发 pre-send/execute”的关键步骤改为在释放锁后执行（例如 `Qt::QueuedConnection`/`QMetaObject::invokeMethod`），以保证可维护性。

基于当前移植推进节奏与风险控制，本次仅记录上述约束与可行方案，不在 QCurl 中默认实现；待移植项目出现明确的“队列等待期间 token 刷新导致请求签名失效”等用例后，再评估采用哪种路径并补齐对应纯离线门禁（MockHandler）。

在事件循环集成方面，QCurl 明确走了“Qt EventLoop 驱动 libcurl multi”的路径：相关证据集中在 `src/QCCurlMultiManager.*`、`src/QCNetworkAccessManager.*` 与 `src/QCNetworkReply.cpp`，并且在 `src/**` 中可见 Qt 侧的 `QSocketNotifier` 与 `QTimer` 使用痕迹，以及 libcurl multi API（如 `curl_multi_socket`/`curl_multi_perform`/`curl_multi_setopt`）的调用文件命中。这种设计优点是线程与资源生命周期可控、易与 Qt UI/业务线程模型对齐，同时能自然支持并发与连接复用（`src/QCNetworkRequestScheduler.*`、`src/QCNetworkConnectionPoolManager.*`）。

接口与错误策略方面，仓库证据倾向于“无异常、显式错误对象/状态机”：`src/**` 未检出 `throw`，且存在独立错误类型 `src/QCNetworkError.*`，并在 reply 侧暴露 `error`/`errorString` 等典型接口（见 `src/QCNetworkReply.h` 的方法名与信号名抽样，以及对应的 `tests/tst_QCNetworkError.cpp`、`tests/tst_QCNetworkReply.cpp`）。这对可维护性与跨语言移植是加分项，因为 Legendary 上层可以把错误统一映射到“可重试/不可重试/需要登录/限流”等业务状态。

对移植集成方式而言，QCurl 同时提供了 CMake 与 qmake 侧集成线索：顶层 `CMakeLists.txt`、`src/CMakeLists.txt`、pkg-config 模板 `qcurl.pc.in`，以及 qmake 工程片段 `src/qcurl.pri`。这意味着移植项目可以选择以 CMake target 方式消费 QCurl，也可以在历史 Qt 工程中直接 include `.pri`，降低接入成本。

需要重点指出的潜在风险点主要不在“功能缺失”，而在“交付一致性与维护成本”：其一，HTTP/3 与部分高级 TLS/代理特性高度依赖 libcurl 的编译选项与后端库（虽有 `src/CurlFeatureProbe.*` 与 `tests/tst_CurlFeatureProbe.cpp`，但跨平台差异仍需 CI 固化）；其二，cookie jar 与 WebView（例如 Qt WebEngine）之间的 cookie 同步属于移植项目常见的工程摩擦点，QCurl 底层具备 `CURLOPT_COOKIEFILE`/`CURLOPT_COOKIEJAR`/`CURLOPT_COOKIELIST`，但“从 WebView 导出并注入、以及持久化策略”仍需上层或桥接层明确设计；其三，QCurl 已存在“按 HTTP 状态码触发重试”的执行路径与测试证据，并已补齐限流场景的 429/`Retry-After` 自适应退避与纯离线回归门禁（见 `src/QCNetworkRetryPolicy.*`、`src/QCNetworkReply.cpp`、`src/QCCurlMultiManager.cpp`、`tests/tst_QCNetworkRetryOffline.cpp`）。当前更值得提前锁定的风险点转向“跨平台发布一致性”（CA 证书路径、HTTP/3 后端差异、代理策略），见第 7 节建议。

## 3) 代码质量与测试

从规模与覆盖面看，QCurl 当前更像“可交付的基础网络库”而非“原型”：`src/` 下 C++ 头/源文件 76 个，总行数约 21.6k；`tests/` 下 Qt Test 用例文件 38 个，总行数约 16.2k；另有 Python 一致性/扩展测试目录 `tests/libcurl_consistency/`，其中 `test_*.py` 32 个，总行数约 7.7k。这个测试比重在网络库类项目中属于偏高水平，意味着很多边界条件已经被纳入自动化。

关键路径健壮性方面，证据较强的点包括：取消与状态机（`src/QCNetworkCancelToken.*` 与 `tests/tst_QCNetworkCancelToken.cpp`）、上传/下载的 I/O 生命周期（`tests/tst_QCNetworkIODeviceLifetime.cpp`、`tests/tst_QCNetworkStreamUpload.cpp`、`tests/tst_QCNetworkFileTransfer.cpp`）、代理（`src/QCNetworkProxyConfig.*` 与 `tests/tst_QCNetworkProxy.cpp`）、连接池与共享句柄（`src/QCNetworkConnectionPoolManager.*` 与 `tests/tst_QCNetworkConnectionPool.cpp`、`tests/tst_QCNetworkShareHandle.cpp`）、缓存（`src/QCNetworkCache.*`、`src/QCNetworkDiskCache.*`、`src/QCNetworkMemoryCache.*` 与 `tests/tst_QCNetworkCache.cpp`、`tests/tst_QCNetworkCacheIntegration.cpp`）、WebSocket（`src/QCWebSocket.*` 与 `tests/tst_QCWebSocket*.cpp`）。这些都是 Legendary 移植时最容易踩坑、也最容易导致线上事故的路径。

安全默认值方面，仓库提供了正向证据：`src/QCNetworkSslConfig.cpp` 明确区分 `defaultConfig()` 与 `insecureConfig()`，且核心实现文件 `src/QCNetworkReply.cpp`/`src/QCWebSocket.cpp` 使用 `CURLOPT_SSL_VERIFYPEER`、`CURLOPT_SSL_VERIFYHOST`、`CURLOPT_CAINFO`，并存在证书钉扎相关 `CURLOPT_PINNEDPUBLICKEY`（见 `src/QCNetworkSslConfig.h` 与 `src/QCNetworkReply_p.h` 的 token 命中）。同时，`ctest` 通过了包含 TLS 交互特征的用例（`tests/tst_QCNetworkHttp2.cpp`、`tests/tst_QCNetworkHttp3.cpp`），这为“默认 TLS 路径可跑通”提供了运行时证据。

测试覆盖仍可分两类理解：第一类是“依赖 curl testenv 的一致性门禁套件（`tests/libcurl_consistency/`）”。该套件已在 Linux CI 中作为 gate 固化（见 `.github/workflows/libcurl_consistency_ext_gate.yml`、`.github/workflows/release_delivery_http3_gate.yml`），并上传报告产物 `build/libcurl_consistency/reports/` 供审计；在受限环境（禁止本机端口绑定/启动 httpd/nghttpx）仍可能 skip，需要在具备权限的 runner 上执行。第二类是“Epic 迁移强关联且必须稳定复现的语义门禁”。这部分已通过“MockHandler 接入执行链路 + 纯离线 QtTest 门禁”补齐：`src/QCNetworkMockHandler.*` 与 `src/QCNetworkReply.cpp` 支持按 URL/method 的响应序列回放与请求捕获；离线门禁覆盖了重试/限流（`tests/tst_QCNetworkRetryOffline.cpp`）、响应头折叠行 unfold（`tests/tst_QCNetworkResponseHeadersOffline.cpp`）、请求配置冲突告警（`tests/tst_QCNetworkRequestWarningsOffline.cpp`）以及 Mock 路径下 HTTP>=400 错误映射契约（`tests/tst_QCNetworkHttpErrorMappingOffline.cpp`），从而避免本地端口/httpbin 依赖导致门禁被跳过。跨平台（Win/mac）的门禁与打包一致性仍需另行补齐（本次不覆盖）。

## 4) 完备性结论

结论等级：可直接使用（作为 `@legendary-python` Qt/C++ 移植项目的基础网络库）。

依据是可审计且可复现的：其一，QCurl 已覆盖移植必需的网络底座能力，并在 `src/` 中有明确模块分层与配置入口（请求对象 `src/QCNetworkRequest.h`、reply/执行与 I/O `src/QCNetworkReply.*`、事件循环与 multi 管理 `src/QCCurlMultiManager.*`、并发与连接池 `src/QCNetworkRequestScheduler.*`/`src/QCNetworkConnectionPoolManager.*`、TLS/代理/超时/重试等策略对象 `src/QCNetworkSslConfig.*`/`src/QCNetworkProxyConfig.*`/`src/QCNetworkTimeoutConfig.*`/`src/QCNetworkRetryPolicy.*`）；其二，自动化测试覆盖广且本机可跑通，`build/` 下 `ctest` 38/38 通过，包含 HTTP/2、HTTP/3、代理、WebSocket、缓存、连接池、取消、文件传输，以及新增的多项“纯离线语义门禁”（重试/限流、响应头 unfold、请求冲突告警、HTTP 错误映射：见 `tests/tst_QCNetwork*Offline.cpp`）等关键路径的运行时证据；其三，legendary 的实际网络调用形态以 `requests.session()` + 超时 + 大量 HTTPS API + 下载为主（见 `legendary-python/legendary/api/egs.py`、`legendary-python/legendary/downloader/mp/*`），QCurl 的接口形态与能力集合可以自然承接。

需要强调的“阻塞项”更多在移植项目上层而非 QCurl 本体：OAuth/设备码登录流程、token 生命周期与持久化、manifest/chunk 业务规则、下载校验与存储布局等（第 6 节给出边界）。QCurl 侧更可能演变为阻塞的点集中在“跨平台交付一致性”（CA 证书路径/打包、代理策略、HTTP/3 后端差异）以及“调度语义/注入时机”（schedule* late pre-send 的潜在需求，见 2.1）。限流语义（429/`Retry-After`）与 GET-only（opt-in）HTTP 状态码重试 gating 已补齐，并通过离线门禁与 Linux gate 提供可复现证据。

## 4.1 基于第 1) 的逐项状态映射（已满足/部分满足/未满足）

下表聚焦“作为 Legendary 移植基础网络库是否完备”的判定口径，已把与 Legendary 当前需求弱相关但 QCurl 已实现的能力（例如 WebSocket、HTTP/3）标注为“非阻塞”。

注：本表中的“未满足（按边界推荐）”并非库侧缺陷，而是 **明确不纳入 QCurl 边界** 的业务能力（建议在 Legendary 侧实现）。责任矩阵（SSOT）见 `helloagents/wiki/legendary_port_responsibility_matrix.md`。

| 能力项（面向 legendary 移植） | 状态 | 缺口/说明 | 证据文件路径（示例） |
|---|---|---|---|
| HTTPS REST（GET/POST 为主） | 已满足 | 需要上层封装 API client | `legendary-python/legendary/api/egs.py`；`src/QCNetworkAccessManager.*`、`src/QCNetworkRequest.h`、`src/QCNetworkReply.*`；`tests/tst_QCNetworkRequest.cpp`、`tests/tst_QCNetworkReply.cpp` |
| 会话与 cookie jar（含持久化能力） | 已满足 | 底层 cookie jar/注入具备，并提供 cookie bridge API（import/export/clearAllCookies，需 `shareCookies`）；Qt WebView cookie store 的实际互通属于上层集成 | `legendary-python/legendary/core.py`、`legendary-python/legendary/utils/webview_login.py`；`src/QCNetworkAccessManager.*`、`src/QCNetworkReply.cpp`、`src/QCCurlMultiManager.cpp`；`tests/tst_QCNetworkCookieBridge.cpp`、`tests/libcurl_consistency/test_p1_cookiejar_1903.py`（Linux gate） |
| Web 登录 CSRF/XSRF（cookie→header） | 已满足 | 提供 middleware 注入点 + cookie bridge 导出能力 + 离线门禁（host/path 作用域、不覆盖显式 header） | `legendary-python/legendary/core.py`；`src/QCNetworkMiddleware.*`、`src/QCNetworkAccessManager.*`；`tests/tst_QCNetworkMiddlewareIntegration.cpp` |
| OAuth/设备码登录与 token 刷新 | 未满足（按边界推荐） | 建议上层实现，网络库仅提供 header 注入与重试/限流钩子 | `legendary-python/legendary/api/egs.py`（oauth）；`src/QCNetworkMiddleware.*`、`src/QCNetworkRequestBuilder.*` |
| 代理（含代理认证与代理 TLS 选项） | 已满足 | 无 | `src/QCNetworkProxyConfig.*`、`src/QCNetworkReply.cpp`（CURLOPT_PROXY*）；`tests/tst_QCNetworkProxy.cpp` |
| TLS 安全默认与可配置（CA/钉扎/禁用模式） | 部分满足 | default/insecure 已有；钉扎（PINNEDPUBLICKEY）已具备可运行门禁（Linux gate）；跨平台 CA 策略仍需在发布集成层固化 | `src/QCNetworkSslConfig.*`、`src/QCNetworkReply_p.h`；`tests/tst_QCNetworkHttp2.cpp`、`tests/tst_QCNetworkHttp3.cpp`、`tests/libcurl_consistency/test_p2_tls_pinned_public_key.py` |
| 超时/低速断开/取消 | 已满足 | 无 | `src/QCNetworkTimeoutConfig.*`、`src/QCNetworkCancelToken.*`；`tests/tst_QCNetworkCancelToken.cpp` |
| 并发、连接池、队列与优先级 | 已满足 | 无 | `legendary-python/legendary/api/egs.py`；`src/QCNetworkRequestScheduler.*`、`src/QCNetworkConnectionPoolManager.*`；`tests/tst_QCNetworkScheduler.cpp`、`tests/tst_QCNetworkConnectionPool.cpp` |
| 本地源地址/网卡绑定（--bind） | 已满足 | 无 | `legendary-python/legendary/downloader/mp/manager.py`、`legendary-python/legendary/downloader/mp/workers.py`、`legendary-python/legendary/cli.py`；`src/QCNetworkRequest.h`、`src/QCNetworkReply.cpp`；`tests/tst_QCNetworkNetworkPath.cpp` |
| 下载与写盘（进度、流式） | 已满足 | 下载校验（hash）建议上层实现；CDN 选择/签名 URL 为上层工作流 | `legendary-python/legendary/core.py`、`legendary-python/legendary/api/egs.py`、`legendary-python/legendary/downloader/mp/manager.py`；`src/QCNetworkReply.*`、`tests/tst_QCNetworkFileTransfer.cpp` |
| CDN 选择与 https→http 降级（disable_https） | 未满足（按边界推荐） | 属于上层策略：QCurl 仅提供 http/https 能力与日志/TLS 配置入口；是否允许降级与原因标注应由上层定义并纳入验收 | `legendary-python/legendary/core.py`、`legendary-python/legendary/cli.py`；`src/QCNetworkLogger.*`、`src/QCNetworkSslConfig.*` |
| 重试与退避（网络错误） | 已满足 | 无 | `src/QCNetworkRetryPolicy.*`；`tests/tst_QCNetworkRetry.cpp` |
| HTTP 5xx 状态码重试（500-504） | 已满足 | 5xx 重试能力已具备（含离线门禁）；默认 retryableErrors 已包含 500/501/502/503/504，并提供 opt-in 的 GET-only HTTP 状态码重试 gating | `legendary-python/legendary/api/egs.py`；`src/QCCurlMultiManager.cpp`、`src/QCNetworkReply.cpp`、`src/QCNetworkRetryPolicy.*`；`tests/tst_QCNetworkRetryOffline.cpp` |
| 限流语义（429/Retry-After） | 已满足 | 已支持 `Retry-After`（delta-seconds / HTTP-date）解析与 delay 覆写，并提供离线回归门禁 | `src/QCNetworkError.*`、`src/QCNetworkRetryPolicy.*`、`src/QCNetworkReply.cpp`、`src/QCCurlMultiManager.cpp`；`tests/tst_QCNetworkRetryOffline.cpp` |
| 错误模型与可观测性（日志/诊断） | 已满足 | 需要上层定义日志策略与采样 | `src/QCNetworkError.*`、`src/QCNetworkLogger.*`、`src/QCNetworkDiagnostics.*`；`tests/tst_QCNetworkDiagnostics.cpp` |

综合判断：QCurl 作为“基础网络库”在功能与质量上已经达到可直接承接 Legendary 移植的水平；若要以“可落地交付、可长期维护”为标准，建议把限流语义与跨平台 CI/证书策略固化列为移植项目的 P0/P1 工程化工作，而不是推迟到线上问题暴露后再补。

## 5) 待实现功能与优先级（按对移植成败影响排序，含思路/依赖/工作量/风险）

这里刻意只列“对 legendary 迁移强关联且当前证据不足/缺失”的项，避免泛泛扩展网络库边界。

| 优先级 | 功能/增强项 | 推荐实现思路（落点） | 依赖取舍建议 | 预计工作量（人日） | 主要风险 |
|---|---|---|---|---:|---|
| P0 | ✅已完成：HTTP 429/`Retry-After` 自适应退避 + GET-only（opt-in）HTTP 状态码重试 gating + 离线回归门禁 | 已落地：解析 `Retry-After`（delta-seconds / HTTP-date）并覆写 delay；提供 `retryHttpStatusErrorsForGetOnly`（opt-in）对齐 `allowed_methods={'GET'}`；离线门禁 `tests/tst_QCNetworkRetryOffline.cpp` 覆盖 429+Retry-After/回退指数退避/500/501/503 上限行为 | 继续用 libcurl；不建议引入 QtNetwork 以免双栈 | 0 | 回归维护：保持“分层门禁”口径（一致性套件聚焦可观测 I/O；重试/退避用离线门禁覆盖） |
| P0 | cookie 与 Qt WebView（WebEngine/Quick）互通方案落地 | 上层定义 cookie SSOT（WebView 或 QCurl）；提供导出/导入桥接：WebView→COOKIELIST 注入，QCurl→持久化/回写；必要时在 manager 层集中管理 cookie jar 路径 | 仍基于 QCurl cookie jar；WebView 侧按 Qt 模块实现 | 5–12 | Qt WebEngine cookie API 与线程亲和性；多 profile 支持 |
| P0 | Legendary API Client 适配层（REST+GraphQL） | 新建上层模块（不进 QCurl）：封装 base hosts、统一 header（UA/Authorization）、统一错误映射与重试策略注入；GraphQL 作为 POST JSON | 依赖 QCurl 的 request builder/headers；如需 middleware 注入点需先补齐执行链路集成 | 6–15 | 端点/参数兼容性；错误映射需要与业务状态一致 |
| P0 | 下载管理与校验（manifest/chunk 语义） | 上层实现 DownloadManager：并发队列、磁盘布局、分块校验（hash）、断点续传策略（chunk 级为主）；网络库只提供流式 I/O、取消、限速 | libcurl/QCurl 足够；hash 用 Qt/C++ 加密库或自带实现 | 10–25 | 性能与 IO 放大；并发与磁盘冲突；跨平台路径差异 |
| P1 | ✅已完成：accept-encoding 与解压行为门禁 | 已落地：`QCNetworkRequest` 托管 Accept-Encoding/自动解压契约；Linux gate 覆盖 `tests/libcurl_consistency/test_p1_accept_encoding.py`；并增加纯离线 QtTest 验证 Mock 回放路径下的配置冲突告警（`tests/tst_QCNetworkRequestWarningsOffline.cpp`） | 维持现有测试体系 | 0 | 回归维护：不同 curl 构建选项对编码支持的差异（capability-gated） |
| P1 | ✅已完成（Linux gate）：证书钉扎（pinned public key）可运行回归 | 已落地：一致性用例 `tests/libcurl_consistency/test_p2_tls_pinned_public_key.py`；baseline 对不支持选项给出可诊断失败；不要求引入新 TLS 栈 | 不引入新 TLS 栈 | 0 | 维护成本：TLS fixture/测试环境权限；能力不可用时需明确 skip/失败口径 |
| P2 | 部分完成（Linux）：HTTP/3 交付级降级策略已固化；跨平台覆盖待后续 | 已落地：运行时 capability gating + `QCURL_REQUIRE_HTTP3=1` 交付门禁（见 `.github/workflows/release_delivery_http3_gate.yml`）；Win/mac/Linux 的 CI 覆盖与打包口径仍需另行补齐 | 继续 libcurl；不要把 HTTP/3 作为强依赖 | 5–15 | QUIC 后端差异、打包复杂度、回归成本（跨平台） |

### 5.1 “HTTP 5xx/429 重试退避”在 QCurl 中的落地设计（API 形态 + 最小门禁）

本节目标是把 legendary 的 `Retry(status_forcelist=[500,501,502,503,504], allowed_methods={'GET'})` 与 429/`Retry-After` 语义，映射到 QCurl 的可实现 API，并给出可离线回归的最小测试集（不依赖外网与 docker httpbin）。

#### 5.1.1 现状证据（QCurl 已有/缺失）

QCurl 已有：在异步 multi 路径 `src/QCCurlMultiManager.cpp` 与同步路径 `src/QCNetworkReply.cpp`，请求完成后读取 `CURLINFO_RESPONSE_CODE`，当 `httpCode>=400` 时映射为 `NetworkError`（见 `src/QCNetworkError.cpp` 的 `fromHttpCode`），并复用 `QCNetworkRetryPolicy::shouldRetry(NetworkError, attemptCount)` 与 `delayForAttempt(attemptCount)` 进行延迟重试。`tests/tst_QCNetworkRetry.cpp` 对 503/500 的重试行为也已有运行时验证，但它当前依赖本地 httpbin，因此在 CI/开发机上可能被跳过（`QSKIP`）。

QCurl 已补齐：已在 `src/QCNetworkRetryPolicy.*` 增加 `delayForAttempt(attemptCount, serverDelay)`（支持服务端 `Retry-After` 覆写并以 `maxDelay` 为上限）、并增加 opt-in 的 `retryHttpStatusErrorsForGetOnly`（对齐 legendary 的 `allowed_methods={'GET'}`）；在 `src/QCNetworkReply.cpp`（同步+Mock 回放）与 `src/QCCurlMultiManager.cpp`（异步 multi）中解析 `Retry-After`（delta-seconds / HTTP-date）并优先使用；新增离线回归门禁 `tests/tst_QCNetworkRetryOffline.cpp` 覆盖 500/501/503/429 的关键语义（含 Retry-After 覆写与回退指数退避）。

#### 5.1.2 建议的最小 API 设计（可逐步交付）

下表给出“最小可实现且可验证”的设计点，目标是兼容现有 QCurl API，同时让上层能对齐 legendary 的重试语义。

| 设计点 | 目标 | 建议接口/行为（最小） | 推荐落点 | 兼容性与备注 |
|---|---|---|---|---|
| status_forcelist 的表达 | 对齐 legendary 的 500/501/502/503/504 与后续扩展 | 保持现有 `QCNetworkRetryPolicy::retryableErrors`，并在示例/文档明确“HTTP 状态码可直接映射为 `NetworkError`”；已增强：`NetworkError` 增加 `HttpNotImplemented=501`、`HttpTooManyRequests=429`，且默认 retryableErrors 已包含 429 与 500/501/502/503/504 | `src/QCNetworkError.*`、`src/QCNetworkRetryPolicy.*` | 不新增新类型即可通过 `static_cast<NetworkError>(code)` 配置任意 4xx/5xx，但可读性差；新增枚举值是向后兼容的增强 |
| Retry-After 支持 | 对齐 429 限流语义，避免“指数退避与服务端要求冲突” | 在延迟计算中引入“服务端建议等待时间”的覆写入口：例如新增 `delayForAttempt(int attemptCount, std::optional<std::chrono::milliseconds> retryAfter)`，若 `retryAfter` 存在则优先使用（并建议以 `maxDelay` 作为上限）；解析规则支持 delta-seconds / HTTP-date | `src/QCNetworkRetryPolicy.*`（计算策略） + `src/QCCurlMultiManager.cpp`/`src/QCNetworkReply.cpp`（解析 headerData 并传入） | 建议把“解析”放在 reply 层（因为 reply 已持有原始 headerData）；策略层只负责“如何合并 retryAfter 与 backoff” |
| allowed_methods={'GET'} 对齐 | 避免对非幂等请求做默认重试（与 legendary 行为一致） | 已提供两条路径：其一，上层仅对 GET 配置 retryPolicy；其二，库侧 opt-in：`QCNetworkRetryPolicy::retryHttpStatusErrorsForGetOnly=true` 时，仅允许 GET 对 HTTP 4xx/5xx 触发重试（libcurl 层网络错误不受限） | 上层或库侧：`src/QCNetworkRetryPolicy.*` + `src/QCNetworkReply.cpp`/`src/QCCurlMultiManager.cpp` | 以 opt-in 方式引入，默认不破坏兼容；如需扩展到 HEAD/PUT 幂等集，可在后续引入 allowedMethods 集合 |
| 离线回归门禁 | 避免依赖外部 httpbin/docker，保证 CI 可跑 | 新增一组不依赖外部服务的 QtTest：覆盖 500/501/503/429 与 `Retry-After`（delta-seconds / HTTP-date），验证 retryAttempt 次数与 delay 选择；优先推荐 MockHandler 纯离线路线（不走 socket） | `tests/tst_QCNetworkRetryOffline.cpp`（新增） + `src/QCNetworkMockHandler.*`/`src/QCNetworkReply.cpp`（回放） | MockHandler 已接入执行链路并支持响应序列与 headers 注入，可作为稳定门禁；如需更贴近真实 HTTP 交互，可再补充 `QTcpServer` 方案作为集成级验证 |

#### 5.1.3 最小测试用例清单（含 MockHandler/离线回归门禁）

下面的用例以“可以作为 CI 门禁”为标准；当前已提供 MockHandler 纯离线路线（`tests/tst_QCNetworkRetryOffline.cpp`），可在不创建 socket 的情况下覆盖核心重试/限流语义。

| 用例 | 场景 | 请求配置 | 预期行为（可验证） | 推荐实现方式 | 推荐落点 |
|---|---|---|---|---|---|
| T1 | 503 固定失败，触发最大重试次数 | GET + `maxRetries=2` + 小 `initialDelay` | 发射 2 次 `retryAttempt`，最终 `error=HttpServiceUnavailable`，总时长 ≥ 2 次 delay | 进程内 `QTcpServer` 固定返回 503 | 新增 `tests/tst_QCNetworkRetryOffline.cpp`（或拆分现有 `tst_QCNetworkRetry.cpp`） |
| T2 | 500→200，验证“可恢复” | GET + `maxRetries>=1` | 发射 1 次 `retryAttempt`，最终成功 `error=NoError`，响应体为 200 场景数据 | `QTcpServer` 按连接/请求计数返回 500 后返回 200 | 同上 |
| T3 | 429 + `Retry-After`→200，验证 delay 覆写与 maxDelay cap | GET + `maxRetries=1` + `maxDelay` 设小（例如 50ms） | 总时长 ≥ 可验证阈值（建议 ≥30ms，允许调度误差）；最终成功；应优先使用 Retry-After（但不超过 maxDelay） | MockHandler：首条 429 且 headers 注入 `Retry-After: 999`，第二条 200 | `tests/tst_QCNetworkRetryOffline.cpp` |
| T4 | 429 无 `Retry-After`，回退指数退避 | GET + `maxRetries=1` + `initialDelay` 设小（例如 40ms） | 总时长 ≥ 可验证阈值（建议 ≥25ms，允许调度误差）；最终成功或最终失败取决于第二次响应 | MockHandler：首条 429（无 header）后返回 200 或继续 429 | `tests/tst_QCNetworkRetryOffline.cpp` |
| T5 | allowed_methods={'GET'} 对齐（如库侧实现） | POST + 与 GET 同策略 | POST 不应因 HTTP 5xx/429 自动重试（或按策略明确的例外）；GET 保持可重试 | 进程内 `QTcpServer` + 计数断言 | 同上或上层 client 测试 |
| T6 | MockHandler 离线序列（推荐） | GET + `maxRetries=1` | 不创建 socket，MockHandler 按序返回 429（带 Retry-After）→200；断言 delay 与最终成功 | MockHandler 响应队列/headers + Reply 执行链路回放 | `tests/tst_QCNetworkRetryOffline.cpp`（已覆盖） |

离线回归门禁建议：把上述离线测试作为 `ctest` 必跑项；将依赖 httpbin 的用例降级为“集成测试”（仍保留，但不作为强门禁），以避免 CI 环境因 docker/服务缺失而退化为 `QSKIP`。

## 6) 需要额外实现的强关联能力（非“网络请求本身”），以及推荐边界

Legendary 迁移的核心难点通常不在“能不能发 HTTP”，而在“围绕 Epic 的认证、会话与下载语义如何工程化”。结合 `legendary-python/legendary/api/egs.py`（oauth、graphql、多个 host 模板）与 `legendary-python/legendary/downloader/mp/*`（下载与 resume 语义），建议额外实现但放在 QCurl 之上的能力如下，并保持清晰边界以降低长期维护成本。

建议实现边界与 QCurl 侧支撑能力清单：见 `helloagents/wiki/legendary_port_responsibility_matrix.md`（SSOT）。
Legendary 侧最小接口清单/WBS（到类与测试用例）：见 `helloagents/wiki/legendary_port_minimal_interfaces_wbs.md`（SSOT）。

首先是 OAuth/设备码登录与 token 刷新辅助。QCurl 不应该内置 Epic OAuth 流程，因为这会把第三方业务语义固化进基础库；推荐在移植项目上层实现“认证服务”，把 token 以可替换存储（内存/磁盘/系统钥匙串）管理，并通过上层 EgsClient/request builder 统一注入 `Authorization` 头（`src/QCNetworkRequestBuilder.*`、`src/QCRequestBuilder.*` 可直接设置 header）。若移植项目希望把“统一注入点”下沉到 middleware，`src/QCNetworkMiddleware.*` 已完成与请求执行链路的集成，可直接作为稳定落点（见 `src/QCNetworkAccessManager.cpp` 与离线门禁 `tests/tst_QCNetworkMiddlewareIntegration.cpp`）。

其次是 cookie/session 持久化与 WebView 登录互通。`legendary-python/README.md` 与 `legendary-python/legendary/utils/webview_login.py` 暗示 webview 登录路径，C++/Qt 移植很可能采用 Qt WebEngine/Quick WebView。建议把“cookie SSOT 选择、profile 隔离、持久化位置与加密”作为上层架构决策；QCurl 侧仅提供通用的 cookie 导入/导出能力与稳定行为（底层已具备 `CURLOPT_COOKIEFILE`/`CURLOPT_COOKIEJAR`/`CURLOPT_COOKIELIST` 证据）。

再次是下载管理与完整性校验（hash）。Legendary 的下载围绕 manifest/chunk（模型在 `legendary-python/legendary/models/manifest.py`、`legendary-python/legendary/models/chunk.py`），这类“分块校验与落盘策略”强烈依赖业务语义与存储布局，不应进入 QCurl。推荐上层实现 DownloadManager，QCurl 仅提供流式 I/O、并发基础、取消、限速、错误回传与可观测性；校验 hash 与重试策略在上层做决策，但可以把“重试/退避/限流”的通用机制下沉到 QCurl（第 5 节 P0 项）。

最后是 Epic API 的常见请求封装层。它应该属于上层（例如 `EgsClient`/`LgdClient`），而不是 QCurl；这样当 Epic 端点变更时，变更范围限定在业务层，避免影响基础网络库的稳定性与复用价值。

## 7) 可执行实现路线图与优先级（P0/P1/P2），并行建议、验收标准、报价与工期

### 7.1 路线图（面向“移植项目可落地交付”）

| 阶段 | 目标 | 必做改动（落点） | 测试策略（落点） | 验收标准（可验证） | 可并行性 |
|---|---|---|---|---|---|
| P0（MVP） | 移植项目可稳定登录与完成一次完整下载/云存档操作 | 上层实现 OAuth/设备码或 webview 登录、token 刷新、EgsClient（REST+GraphQL）、DownloadManager（chunk/manifest）、cookie 互通方案；QCurl 侧补齐 429/`Retry-After` 自适应退避与 GET-only/501 对齐，并把重试相关门禁改为离线可跑（见第 5.1） | QCurl 继续跑 `build/` 的 `ctest`；上层用进程内 `QTcpServer`（或先补齐 MockHandler 集成后使用 `src/QCNetworkMockHandler.*`）构造离线 API 回归；下载用本地 fixture 校验 hash；把 5xx/429 用例加入 C++ 测试 | 在目标平台上：可登录、可列库、可下载并校验、可取消与恢复、错误可诊断；QCurl 侧 `ctest` 全绿 | OAuth/EgsClient/DownloadManager/cookie 桥接可并行；QCurl 限流/门禁补齐可并行 |
| P1（交付级） | 长时间运行稳定、可观测、可跨平台发布 | 固化跨平台 CA/代理策略与打包；跑通 `tests/libcurl_consistency/` 的最小集（或将关键用例迁移到可跑的 C++ 测试）；补齐钉扎与压缩门禁 | 增加 CI：Win/mac/Linux 的 `ctest`；为一致性套件建立 gate 配置；对关键网络语义（重试、重定向、cookie）引入回归门禁 | 多平台 CI 全绿；关键网络语义回归可重复；线上日志可定位问题 | CI/打包、测试门禁、业务层稳定性优化可并行 |
| P2（增强） | 性能与网络环境适应性提升 | HTTP/3 可降级策略固化；更细粒度限速/队列策略；更丰富诊断（连接复用统计等） | 增加压力/长稳测试（可复用 `tests/libcurl_consistency/run_soak_share_handle.py` 等脚本体系） | 大规模下载/弱网下成功率与性能达标 | 性能/诊断/HTTP3 可独立并行 |

### 7.2 最短可用版本（MVP）定义与验收标准（建议写入移植项目的交付清单）

| MVP 验收项 | 通过标准 | 证据落点（建议） |
|---|---|---|
| 认证登录 | 设备码或 webview 登录成功，token 可持久化并可刷新 | 上层新增认证模块与 EgsClient/request builder 注入 auth；如需统一注入点可使用已集成的 middleware（`src/QCNetworkMiddleware.*`、`src/QCNetworkAccessManager.cpp`） |
| API 基础能力 | 可稳定调用 Epic REST 与 GraphQL，错误可映射为可重试/需登录/限流/不可恢复 | 上层 EgsClient；QCurl 错误模型 `src/QCNetworkError.*` |
| 下载能力 | 能下载一个游戏/内容的完整数据，支持取消与恢复，支持并发队列 | 上层 DownloadManager；QCurl 基础能力 `src/QCNetworkReply.*`、scheduler/pool |
| 完整性校验 | 按 manifest/chunk 规则完成 hash 校验，失败可重试与诊断 | 上层校验模块（结合 `legendary-python/legendary/models/*` 语义） |
| 限流韧性 | 遇到 5xx/429 能按策略重试与退避（含 Retry-After）并最终恢复/失败可解释 | QCurl 补齐 P0 项（第 5 节）+ 回归用例 |
| 可观测性 | 关键请求链路有结构化日志/trace id，可定位失败原因 | `src/QCNetworkLogger.*` + 上层采样策略 |
| 回归门禁 | QCurl `ctest` 全绿；上层关键用例离线可回归 | `build/ctest`；上层离线测试（进程内 `QTcpServer` 或补齐 MockHandler 后的纯离线） |

### 7.3 报价与工期表（以“移植项目网络相关交付”为估价口径）

下面给出两种估价：一种是“仅补齐 QCurl 交付级缺口（更偏库侧）”，另一种是“把 Legendary 迁移真正跑通（含上层适配）”。报价以 1 名资深 Qt/C++ 网络工程师为主、必要时配 0.5 名测试/构建支持为假设；若双人并行，日历工期可下降但成本不变或略升。单价因地区与合作模式差异较大，这里给出区间并明确假设：USD 120–180/小时（或等价的人日单价），并包含 15%–30% 风险缓冲。

| 工作包 | 范围 | 工作量（人日） | 日历工期（周） | 报价区间（USD，含缓冲） | 关键风险与缓解 |
|---|---|---:|---:|---:|---|
| 仅 QCurl 补齐到“legendary 迁移友好” | HTTP 5xx/429（含 Retry-After）重试退避；cookie/webview 互通方案接口固化；钉扎/压缩门禁补齐；跨平台 CI 骨架 | 15–35 | 3–7 | 18k–75k | 风险在测试门禁与跨平台证书策略；缓解是先把最小集跑通并把策略文档化 |
| Legendary 移植网络线 MVP 交付（推荐最小购买） | 上层 OAuth/设备码或 webview 登录；EgsClient（REST+GraphQL）；DownloadManager（chunk/校验/并发）；QCurl HTTP 5xx/429 重试退避补齐；离线回归门禁 | 35–70 | 6–12 | 55k–150k | 风险在业务语义与下载校验复杂度；缓解是按 MVP 验收表逐条交付、优先离线可回归 |
| Legendary 移植网络线 交付级（P1） | 在 MVP 基础上跨平台发布与长期稳定：CI 全平台、libcurl/CA/HTTP3 降级、完整一致性测试门禁 | 60–120 | 10–20 | 110k–280k | 风险在打包与环境差异；缓解是分平台逐个打通并建立可复现构建链路 |

补充估价视角（用于“项目价格/资产评估”）：若从零开发一套具备 QCurl 当前规模与覆盖面的网络库（约 19.6k LOC 源码 + 15.5k LOC C++ 测试 + 多协议/缓存/连接池/WS 等能力），在工程化要求与测试投入不降的前提下，合理替代成本通常在 3–6 人月量级（取决于平台与协议栈要求），对应 USD 120k–400k 的量级区间；而 QCurl 现状已显著降低该成本，移植项目更应把预算集中在“上层业务适配与交付工程化”上。

### 7.4 需要进一步查看但当前证据不足以判断的点（以及建议核查路径）

在不接触真实 Epic 服务的前提下，有两类问题无法仅凭当前运行结果完全确认：其一是“业务层幂等/错误语义”是否与 Epic 实际行为一致（例如重试触发条件、错误映射、token 刷新时机）；其二是“跨平台发布一致性”（Win/mac 证书/代理/HTTP3 后端差异与打包口径）。另外，`tests/libcurl_consistency/` 套件已在 Linux CI 中作为 gate 跑通并输出报告（见 `.github/workflows/libcurl_consistency_ext_gate.yml`、`.github/workflows/release_delivery_http3_gate.yml` 与 `build/libcurl_consistency/reports/`），但在受限环境仍可能因端口/服务限制而 skip。

如需进一步把“上层 Legendary 迁移 API/下载”拆成可直接立项的 WBS（到具体模块/接口草案/测试用例清单），建议在不扩展 QCurl 边界的前提下先确认：目标 Qt 版本与平台矩阵、是否使用 Qt WebEngine 登录、token/cookie 的持久化介质、下载目录与校验策略、以及是否要求强制支持 HTTP/3。
