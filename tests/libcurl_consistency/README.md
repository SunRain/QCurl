# QCurl ↔ libcurl（含 libtest 工具）数据一致性测试候选集

> 目标：基于上游 `curl/tests/` 的现有用例，筛选出可用于判断 **QCurl 与 libcurl API 在“可观测数据层面”一致性** 的最小回归集合。
>
> 约束：当前构建的 libcurl 已启用 **HTTP/3** 与 **WebSockets**。

---

## 1. 一致性定义（本候选集覆盖范围）

本候选集的唯一判定核心：在不依赖实现细节的前提下，仅比较 **外部可观测结果** 是否一致。

### 1.1 可观测数据（定义与边界）

“可观测数据”指可在测试/使用方视角 **稳定采集且可复现** 的输出，来源包括：

- **客户端 API 输出（QCurl）**：`QCNetworkReply`/`QCWebSocket` 的信号序列、`error()`/`errorString()`、`readAll()`/`rawHeaderData()` 等。
- **客户端 API 输出（libcurl）**：`curl_easy_perform` 的 `CURLcode`、`curl_easy_getinfo`（如 `CURLINFO_RESPONSE_CODE`）、write/header/xferinfo 回调的事件序列与参数。
- **服务端观测**：服务端日志中可稳定提取的 `method/path/query/status/协议族` 与白名单请求头（见 6.1）。
- **运行器产物**：`artifacts` JSON 与 `download_*.data` 等落盘文件（见 6.4）。

为保证可复现性，本候选集将可观测数据分为：

- **主断言（默认 Gate）**：请求语义摘要（服务端观测）+ 响应字节（hash/len）+ 状态码/协议族；（WS 场景）增加帧/事件序列。
- **在范围内但当前未完全覆盖**：HTTP 回调/信号序列（细粒度）、并发/多路复用的时序指标、multipart/form-data 的“字节级编码细节”对齐等；对应缺口已在覆盖矩阵与 `tasks.md` 中列出。

### 1.2 术语（本候选集约定）

- **可观测一致性**：对齐可观测输出，而不是对齐内部实现/资源释放顺序。
- **请求语义摘要**：以服务端观测为准的 `{method, url, headers_allowlist, body_len, body_sha256}`；其中 `url` 会剔除用于关联的 query `id`。
- **对比器**：`tests/libcurl_consistency/pytest_support/compare.py`；默认比较 `request(s)`/`response(s)` 以及可选 `cookiejar`/`error` 字段（见 6.4）。
- **baseline / QCurl**：同一用例在 libcurl baseline 与 QCurl 侧执行器（`tests/tst_LibcurlConsistency.cpp`）下生成的 artifacts。

### 1.3 覆盖范围与非目标

当前已落地的主断言覆盖：

- **请求侧**：方法/URL（去除关联用 query `id`）/关键头白名单（如 `Host`、`Range`、`Cookie`、`Proxy-*`、`Content-Length`）以及请求体字节（包含二进制与 `\\0` 的情况）。
- **响应侧**：响应体字节、关键状态（如 2xx/4xx）与基本协议路径（`http/1.1|h2|h3`）。
- **WebSocket**：握手语义（头白名单）与帧收发/事件序列的可观测结果。

明确不做/不作为 Gate 判据：

- **不依赖内部实现细节**：不比较 libcurl 内部状态机、内存/句柄释放顺序、线程调度等。
- **不比较不可稳定复现的数据**：如 `Date`、`Server`、随机 token、精确耗时数值（但 **超时/取消的触发结果与终态** 已在 `test_p1_timeouts.py`/`test_p1_cancel.py` 覆盖）。
- **不覆盖 QCurl 未暴露的 libcurl 选项**：如 `CURLU` URL API、`CURLOPT_SHARE`、HTTP trailers、`CURLOPT_AWS_SIGV4` 等（除非后续明确纳入并补齐观测与对比）。

### 1.4 覆盖矩阵（按协议层/功能点/错误路径/时序语义）

> 说明：此处仅统计“QCurl ↔ libcurl 可观测一致性”的对比测试；`tests/` 下的 QCurl 单侧单元/集成测试不计入一致性覆盖。

#### 协议层 / 传输层

| 分类 | 可观测点 | 覆盖结论 | 证据（测试/代码位置） | 缺口任务 |
|---|---|---|---|---|
| HTTP/1.1 | 请求语义摘要 + 响应字节一致 | 已覆盖 | `tests/libcurl_consistency/test_p0_consistency.py`（P0，含 http/1.1） | - |
| HTTP/2 | 请求语义摘要 + 响应字节一致 | 已覆盖 | `tests/libcurl_consistency/test_p0_consistency.py`（P0，h2） | - |
| HTTP/3 | 请求语义摘要 + 响应字节一致 | 部分覆盖（依赖 `env.have_h3()`） | `tests/libcurl_consistency/conftest.py`（nghttpx-h3 注入）+ `tests/libcurl_consistency/test_p0_consistency.py`（h3 变体） | - |
| TLS 校验 | verifyPeer/verifyHost + CA 路径的成功/失败语义 | 已覆盖 | `tests/libcurl_consistency/test_p2_tls_verify.py` | - |
| WebSocket | 握手语义 + 帧/事件序列 | 已覆盖（基础 + ext） | `tests/libcurl_consistency/test_p0_consistency.py`（ws_*）+ `tests/libcurl_consistency/test_ext_ws_suite.py` | - |

#### 功能点

| 分类 | 可观测点 | 覆盖结论 | 证据（测试/代码位置） | 缺口任务 |
|---|---|---|---|---|
| 下载 | 文件字节一致（含并发与 Range 续传） | 已覆盖 | `tests/libcurl_consistency/pytest_support/case_defs.py`（download_*）+ `tests/tst_LibcurlConsistency.cpp`（download_*） | - |
| 上传 | 回显字节一致（PUT/POST） | 已覆盖 | `tests/libcurl_consistency/test_p0_consistency.py`（upload_*） | - |
| 二进制请求体 | `\\0` 字节一致（POSTFIELDS） | 已覆盖 | `tests/libcurl_consistency/test_p1_postfields_binary.py` | - |
| Cookie 持久化 | cookiejar 文件内容一致 | 已覆盖 | `tests/libcurl_consistency/test_p1_cookiejar_1903.py` | - |
| Cookie 发送 | 服务端看到的 `Cookie:` 一致 | 已覆盖 | `tests/libcurl_consistency/test_p2_cookie_request_header.py` | - |
| 重定向 | 多跳 302 序列与最终落点一致 | 已覆盖 | `tests/libcurl_consistency/test_p1_redirect_and_login_flow.py` | - |
| Proxy | proxy 视角（GET absolute-form / CONNECT authority）一致 | 已覆盖 | `tests/libcurl_consistency/test_p1_proxy.py` | - |
| 响应头 | `Location/Set-Cookie/WWW-Authenticate`（白名单）一致 | 部分覆盖 | `tests/libcurl_consistency/http_observe_server.py` + `tests/libcurl_consistency/test_p1_redirect_and_login_flow.py` | - |
| 响应头字节级 | 原始响应头字节/重复头一致性 | 已覆盖（跳过 `Date/Server`） | `tests/libcurl_consistency/test_p1_resp_headers.py` + `src/QCNetworkReply.cpp`（`rawHeaderData()`） | - |
| HTTP 方法面 | HEAD/DELETE/PATCH 的可观测语义对齐 | 已覆盖（HEAD/PATCH） | `tests/libcurl_consistency/test_p1_http_methods.py` + `tests/tst_LibcurlConsistency.cpp` | - |
| Multipart | multipart/form-data parts 语义一致（name/filename/type/size/sha256；不比较 boundary/原始 body） | 已覆盖（语义级） | `tests/libcurl_consistency/test_p1_multipart_formdata.py` + `src/QCMultipartFormData.*` | - |

#### 错误路径

| 分类 | 可观测点 | 覆盖结论 | 证据（测试/代码位置） | 缺口任务 |
|---|---|---|---|---|
| HTTP 错误码 | 4xx/5xx：status + body + 错误归一化一致 | 已覆盖 | `tests/libcurl_consistency/test_p2_fixed_http_errors.py` | - |
| TLS 错误 | 证书校验失败：错误归一化一致 | 已覆盖 | `tests/libcurl_consistency/test_p2_tls_verify.py` | - |
| 连接拒绝 | `CURLE_COULDNT_CONNECT` ↔ `NetworkError::ConnectionRefused` | 已覆盖 | `tests/libcurl_consistency/test_p2_error_paths.py` + `src/QCNetworkError.cpp`（映射） | - |
| 超时 | `CURLE_OPERATION_TIMEDOUT` ↔ `NetworkError::ConnectionTimeout` | 已覆盖 | `tests/libcurl_consistency/test_p1_timeouts.py` + `tests/libcurl_consistency/http_observe_server.py` | - |
| 取消 | `CURLE_ABORTED_BY_CALLBACK` ↔ `NetworkError::OperationCancelled` | 已覆盖（基础） | `tests/libcurl_consistency/test_p1_cancel.py` + `src/QCNetworkReply.cpp`（cancel） | - |
| Proxy 认证失败 | 407/认证缺失/错误凭据的可观测语义 | 已覆盖 | `tests/libcurl_consistency/test_p2_error_paths.py` + `tests/libcurl_consistency/http_proxy_server.py` | - |
| URL 非法 | `CURLE_URL_MALFORMAT` ↔ `NetworkError::InvalidRequest` | 已覆盖 | `tests/libcurl_consistency/test_p2_error_paths.py` + `src/QCNetworkError.cpp`（映射） | - |

#### 时序语义 / 生命周期

| 分类 | 可观测点 | 覆盖结论 | 证据（测试/代码位置） | 缺口任务 |
|---|---|---|---|---|
| 重定向序列 | 多跳请求序列一致（顺序敏感） | 已覆盖 | `tests/libcurl_consistency/test_p1_redirect_and_login_flow.py` | - |
| 并发多请求 | 多请求集合等价（按 URL 稳定排序）+ keep-alive 复用统计（ext） | 部分覆盖 | `tests/libcurl_consistency/test_ext_suite.py`（ext_multi_get4_* + ext_reuse_keepalive_http_1_1） | - |
| WS 事件序列 | 帧类型/顺序一致 | 已覆盖（ext） | `tests/libcurl_consistency/test_ext_ws_suite.py` | - |
| HTTP 回调/信号序列 | `readyRead/finished/error/cancelled/progress` 的序列与约束 | 已覆盖（取消后无事件约束 + 进度稳定摘要 + pause/resume 弱判据 + pause/resume 强判据/语义合同） | `tests/libcurl_consistency/test_p1_cancel.py` + `tests/libcurl_consistency/test_p1_progress.py` + `tests/libcurl_consistency/test_p2_pause_resume.py` + `tests/libcurl_consistency/test_p2_pause_resume_strict.py` | - |
| 空 body 语义 | `readAll()` 的 `nullopt`/空字节一致性规则 | 已覆盖 | `tests/libcurl_consistency/test_p1_empty_body.py` + `src/QCNetworkReply.cpp`（readAll） | - |

### 1.5 风险点（看似一致但在可观测层面可被区分）

- **响应头重复项/多值头**：`QCNetworkReply::rawHeaders()` 由 `QMap` 构建（见 `src/QCNetworkReply.cpp` 的 `parseHeaders()`/`rawHeaders()`），会丢失重复头；因此一致性对比以 `rawHeaderData()` 为准，并在 `tests/libcurl_consistency/test_p1_resp_headers.py` 中对齐 header 行集合（跳过 `Date/Server`），写入并比较 `response.headers_raw_*` 字段。
- **空响应体与 `readAll()` 语义**：已修复 `readAll()` 在“终态且 body 为空”时返回 empty QByteArray（不再是 `std::nullopt`），并通过 `tests/libcurl_consistency/test_p1_empty_body.py` 覆盖 `200 + Content-Length: 0` 与 `204 No Content`；`p1_redirect_nofollow` 不再需要绕过逻辑。
- **chunked vs `Content-Length`**：`test_07_17_hx_post_reuse` 的 baseline 在 http/1.1 路径下可能走 chunked（无 `Content-Length`），而 QCurl（`POSTFIELDS+SIZE`）会显式带 `Content-Length`；当前已将该头从默认断言中排除（`tests/libcurl_consistency/test_p0_consistency.py` 的 `include_content_length`），如需 header 严格对齐需单独任务补齐。
- **multipart 编码细节差异**：boundary 字符串与 `Content-Type: multipart/form-data; boundary=...`、`Content-Length` 等属于实现细节，可在可观测层面被区分但并非稳定契约；当前一致性用例以“服务端可解析 parts 语义摘要”对齐，明确不比较 boundary/原始请求体字节（见 `tests/libcurl_consistency/test_p1_multipart_formdata.py`）。
- **并发多请求“顺序语义”**：ext_multi 用例采用集合等价（按 URL 排序）而不比较完成顺序；若业务依赖时序（回调顺序/首包先后），需新增任务采集并对齐“完成顺序/关键事件序列”。
- **HTTP/3 覆盖的可见盲区**：h3 变体会在 `env.have_h3()` 为 False 时自动跳过；需在 Gate 报告/产物中显式呈现“是否覆盖 h3”，避免误以为已覆盖（见 6.3 的 gate 输出）。

---

## 2. 推荐最小回归集（建议作为 Gate）

### P0（最小 Gate：强数据断言，优先级最高）

这些用例直接断言“文件/回显内容”或“WS 收发成功”，最适合作为 QCurl↔libcurl 的数据一致性回归基线：

- P0 断言包含：**请求语义摘要一致**（method/url/关键头规范化 + request body 的 len/hash）+ **响应/文件字节一致**。

- **下载字节一致（覆盖 http/1.1 + h2 + h3）**
  - `curl/tests/http/test_02_download.py`：
    - `test_02_21_lib_serial`（串行下载 + pause/resume）
    - `test_02_22_lib_parallel_resume`（并发下载 + resume）
  - 说明：基线客户端为 `LocalClient(name='cli_hx_download')`，其实际执行的二进制是 `tests/libtest/libtests`（libcurl API 客户端），并对下载文件做逐字节对比（见 `curl/tests/http/testenv/client.py`）。
  - 注：P0 要求“最终文件字节一致”并覆盖“中断 + Range 续传（resume）”；不要求 QCurl 对齐 in-flight pause/resume（`-P`）语义。

- **中断 + Range 续传一致（覆盖 http/1.1 + h2 + h3）**
  - `tests/libcurl_consistency`：`download_range_resume`（P0 自建补充）
  - 覆盖：服务端观测到“首段非 Range + 续传 Range”两次请求；最终文件字节一致。

- **上传/回显一致（覆盖 http/1.1 + h2 + h3）**
  - `curl/tests/http/test_07_upload.py`：
    - `test_07_15_hx_put`（PUT 上传，回显/长度校验）
    - `test_07_17_hx_post_reuse`（POST 复用连接路径）
  - 说明：基线客户端为 `LocalClient(name='cli_hx_upload')`，底层使用 libcurl multi + upload/read callback/mime 等路径（源码在 `curl/tests/libtest/cli_hx_upload.c`）。
  - 注：`test_07_17_hx_post_reuse` 在 http/1.1 路径下基线可能走 chunked（无 `Content-Length`），而 QCurl（`POSTFIELDS+SIZE`）会显式带 `Content-Length`；该头字段不作为默认一致性断言，仍以“请求体字节 + 回显字节”对齐为准。

- **WebSocket 收发一致（覆盖 ws 基础收发与 ping/pong）**
  - `curl/tests/http/test_20_websockets.py`：
    - `test_20_02_pingpong_small`（ping/pong）
    - `test_20_04_data_small`（data frames，小数据）
  - 说明：基线客户端为 `cli_ws_pingpong`/`cli_ws_data`（源码在 `curl/tests/libtest/cli_ws_pingpong.c`、`curl/tests/libtest/cli_ws_data.c`）。

### P1（补充：对齐 QCurl 当前实际设置的关键 libcurl 选项）

这些用例对“协议层请求体字节/二进制数据/cookie 文件落盘”有明确断言，且与 QCurl 当前实现路径匹配：

- **POSTFIELDS 二进制（包含 `\\0`）发送一致性**
  - `curl/tests/data/test1531`
  - 说明：对应工具实现 `curl/tests/libtest/lib1531.c`，验证 `CURLOPT_POSTFIELDS` 在 multi 场景下的请求体字节（`%hex[...]%`）与 `Content-Length`。

- **Cookie 文件读写一致性（`COOKIEFILE/COOKIEJAR`）**
  - `curl/tests/data/test1903`
  - 说明：验证 cookiefile → reset → 再 set，并校验最终输出 cookiejar 文件内容。

### P1（补充：业务常用链路的一致性断言）

> 这些用例不直接来自上游 `curl/tests/data/`，而是以 `tests/libcurl_consistency/` 自建服务端的方式补齐“可观测数据层面”的关键缺口。

- **重定向（`CURLOPT_FOLLOWLOCATION`）一致性**
  - 覆盖：多跳 302 的请求序列一致、最终落点一致、`Location` 响应头一致（已归一化去掉关联用的 query `id`）。
- **模拟 HTTP 登录态（`Set-Cookie` → `Cookie`）一致性**
  - 覆盖：登录响应 `Set-Cookie`、后续请求携带 `Cookie`、最终响应字节一致。
- **HTTP proxy（含 HTTPS CONNECT）一致性**
  - 覆盖：proxy 视角 `GET` absolute-form / `CONNECT` authority + `Proxy-Authorization`；HTTPS 场景同时对齐 origin 侧请求语义摘要与响应字节。

### P2（低优先级：错误/安全语义对齐）

- **TLS 校验语义（成功/失败路径）**
  - 覆盖：verifyPeer/verifyHost + 自定义 CA（`caCertPath/CAINFO`）下的成功路径，以及缺少 CA 时的证书错误路径。
- **Cookie 请求头可观测一致性（与 cookiejar 文件落盘解耦）**
  - 覆盖：相同 cookiefile 输入下，服务端看到的 `Cookie:` 值一致（做稳定归一化）。
- **固定 HTTP 错误码一致性（404/401/503）**
  - 覆盖：状态码/响应 body 字节一致，并输出统一的错误归一化字段（`kind/http_status`）。

---

## 3. 可选扩展集（可按 QCurl 迭代逐步纳入）

> 仅当你确实希望将“多请求/多路复用行为”也纳入一致性 gate 时再加入，避免引入与数据无关的波动点。

- **HTTP/2 多请求（multi + verbose/连接复用可观测）**
  - `curl/tests/data/test2402`
- **HTTP/3 多请求（multi + verbose/连接复用可观测）**
  - `curl/tests/data/test2502`
- **WebSocket 低层 API（ws callback/raw 模式）**
  - `curl/tests/data/test2301`（同簇 `test2302/test2303/test2304`）
  - `curl/tests/data/test2700`（同簇 `test2701..`；对应 `curl_ws_send/recv`）

---

## 4. 明确排除（不建议用于 QCurl↔libcurl 数据一致性基准）

这些用例更偏 libcurl 内部 API/调试输出/非 QCurl 公共能力，容易导致“对不齐”的假失败：

- **URL API / `CURLU`**：例如 `curl/tests/data/test1567`
- **共享 cookie / 手工注入 cookie list**：例如 `curl/tests/data/test506`、`curl/tests/data/test3103`
- **`curl_easy_reset`/handle 复用清理语义**：例如 `curl/tests/data/test598`、`curl/tests/data/test676`
- **HTTP trailers / AWS_SIGV4 / keep-sending-on-error 等 QCurl 未暴露选项**
  - 例如 `curl/tests/data/test1598`（trailers）、`curl/tests/data/test1937`（AWS_SIGV4）、`curl/tests/data/test1533`（KEEP_SENDING_ON_ERROR）

---

## 5. 备注：为什么这些用例能对齐 QCurl 当前进度

QCurl 当前网络请求实现会直接设置/依赖下列 libcurl 选项（示例见 `src/QCNetworkReply.cpp`）：

- `CURLOPT_FOLLOWLOCATION`（重定向）
- `CURLOPT_POSTFIELDS`/`CURLOPT_POSTFIELDSIZE`（POST/PUT/PATCH 的 QByteArray 请求体路径）
- `CURLOPT_RANGE`（Range 请求）
- `CURLOPT_PROXY*`（代理与认证）
- `CURLOPT_HTTP_VERSION`（HTTP/2/HTTP/3）
- `CURLOPT_SSL_*`（TLS 校验/CA/客户端证书）
- `CURLOPT_COOKIEFILE`/`CURLOPT_COOKIEJAR`（cookie 文件读写）

因此本目录的候选集优先覆盖这些“QCurl 实际会走到”的路径，并以 **字节/协议断言** 为主，避免把“libcurl 内部行为差异”误当成 QCurl 数据不一致。

---

## 6. 落地方式（Qt Test + pytest 混合）与任务拆分

- **执行模型**：pytest 负责拉起 `curl/tests/http/testenv` 的服务端环境（http/1.1 + h2 + h3 + ws），运行 libcurl baseline（`LocalClient(name='cli_*')` → `curl/tests/libtest/libtests`），再调用 Qt Test 生成 QCurl `artifacts`，最后在 pytest 侧做对比与报告输出。
- **任务拆分**：见 `tests/libcurl_consistency/tasks.md`。

### 6.0 端到端结构（源码结构/关键数据流/可观测输出）

- 源码结构（与一致性直接相关）：
  - HTTP：`src/QCNetworkAccessManager.*` / `src/QCNetworkRequest.*` / `src/QCNetworkReply.*`
  - 并发/调度/连接池：`src/QCCurlMultiManager.*`、`src/QCNetworkRequestScheduler.*`、`src/QCNetworkConnectionPoolManager.*`
  - WebSocket：`src/QCWebSocket.*`
  - Multipart：`src/QCMultipartFormData.*`（已纳入：语义级一致性，见 `tests/libcurl_consistency/test_p1_multipart_formdata.py`）
- 一致性测试结构：
  - QCurl 执行器：`tests/tst_LibcurlConsistency.cpp`（通过环境变量选择 case，落盘 `download_*.data`）
  - pytest 驱动与对比器：`tests/libcurl_consistency/pytest_support/*`
  - baseline：
    - 上游 baseline：`curl/build/tests/libtest/libtests`（`LocalClient(name='cli_*')`）
    - repo 内置 baseline：`qcurl_lc_http_baseline`/`qcurl_lc_postfields_binary_baseline`/`qcurl_lc_range_resume_baseline`（ext 另有 `qcurl_lc_ws_baseline`/`qcurl_lc_multi_get4_baseline`）
  - 服务端：
    - 上游 `curl/tests/http/testenv`：httpd（h1/h2）+ nghttpx（h3）+ ws_echo_server（握手观测）
    - repo 自建：`tests/libcurl_consistency/http_observe_server.py`、`tests/libcurl_consistency/http_proxy_server.py`、`tests/libcurl_consistency/ws_scenario_server.py`
- 数据流（每个 case）：
  1. pytest 分配端口并启动服务端
  2. baseline 执行并生成 `baseline.json` + 下载文件
  3. QCurl 执行器运行并生成 `qcurl.json` + 下载文件
  4. pytest 基于服务端日志回填“观测值”，再执行对比器断言

### 6.1 观测机制（P0 Gate 关键）

为避免“构造出来的语义摘要”导致伪通过，P0 的关键语义字段改为 **服务端观测值**：

- **HTTP(S)**：从 httpd 的 `access_log` 提取 `method/url/status/协议族(http/1.1|h2)` 以及关键请求头白名单（`Range`、`Content-Length`）。
  - 日志位置：`curl/tests/http/gen/apache/logs/access_log`
  - LogFormat 由 `curl/tests/http/testenv/httpd.py` 生成。
- **HTTP/3**：从 nghttpx-quic 的 `access_log` 提取 `alpn/method/path/status` 与关键请求头白名单（`Range`、`Content-Length`）。
  - 日志位置：`curl/tests/http/gen/nghttpx/access_log`
- **WebSocket**：ws echo server 在握手时写出 JSONL（path + 头白名单），用于提取 WS 的 `url` 与握手语义（不记录随机/不可比的 header）。
  - 日志位置：`curl/tests/http/gen/ws_echo_server/ws_handshake.jsonl`
- **自建观测服务（HTTP/Proxy）**：`http_observe_server.py`/`http_proxy_server.py` 输出 JSONL（请求/响应头白名单），用于 redirect/login/cookie/header/error/proxy 等用例；失败时会随 `QCURL_LC_COLLECT_LOGS=1` 自动复制到对应 case 的 `service_logs/`。

pytest driver 会为 baseline/QCurl 各自注入独立的 query `id` 以定位对应的服务端观测记录，并在写回 `artifacts` 前剔除 `id`（避免对比噪声）。

如需在失败时自动收集服务端日志用于 debug，可设置环境变量：

- `QCURL_LC_COLLECT_LOGS=1`：当某个 case 断言失败/异常时，将 `httpd/nghttpx/ws` 的关键日志复制到对应目录 `curl/tests/http/gen/artifacts/<suite>/<case>/service_logs/`，并写出 `meta.json`（包含 baseline/qcurl 的 req_id）。

### 6.2 HTTP/3 覆盖的前置条件

即使 curl 构建启用了 HTTP/3，`env.have_h3()` 仍依赖 **h3-capable 的 nghttpx**（需要 ngtcp2/nghttp3）。本仓库默认在构建 `tst_LibcurlConsistency` 时通过 `qcurl_nghttpx_h3` 从源码构建并安装 `build/libcurl_consistency/nghttpx-h3/bin/nghttpx`，并在 `curl/tests/http/config.ini` 中指向该路径；若未构建该 target，则 P0 只覆盖 http/1.1 + h2（h3 变体会自动跳过）。

### 6.3 复现命令（本仓库默认路径）

- 前置条件（最小集合）：
  - Qt6 + CMake + C++17 编译器
  - Python3 + `pytest`（以及 WS 场景的 `websockets`）
  - 可运行的 `curl/tests/http/testenv`（httpd/nghttpx/ws）
- 构建 QCurl Qt Test：
  - `cmake -S . -B build && cmake --build build --target tst_LibcurlConsistency -j"$(nproc)"`
- 构建 curl baseline（libtests）：
  - `cmake -S curl -B curl/build && cmake --build curl/build --target libtests -j"$(nproc)"`
- 运行（P0）：
  - `QCURL_QTTEST="build/tests/tst_LibcurlConsistency" pytest tests/libcurl_consistency/test_p0_consistency.py`
- 运行（P1/all）：
  - `python tests/libcurl_consistency/run_gate.py --suite p1 --build`
  - `python tests/libcurl_consistency/run_gate.py --suite all --build`
- 运行（含 ext，可选）：
  - `python tests/libcurl_consistency/run_gate.py --suite all --with-ext --build`

#### Gate 入口（推荐）

为了把 P0 变成“真正可靠的 Gate”，提供统一入口脚本（输出 JUnit XML + JSON）：

- `python tests/libcurl_consistency/run_gate.py --suite p0 --build`

说明：
- 默认会设置 `QCURL_LC_COLLECT_LOGS=1`，失败时自动把 `httpd/nghttpx/ws` 关键日志复制到 `curl/tests/http/gen/artifacts/<suite>/<case>/service_logs/`。
- 在本次 Codex CLI sandbox 环境下，运行需要 `sandbox_permissions=require_escalated`（否则端口分配/服务启动会被拒绝）。

说明：
- `tests/libcurl_consistency/conftest.py` 会默认注入 `CURL_BUILD_DIR=curl/build`、`CURL=curl/build/src/curl`、`CURLINFO=curl/build/src/curlinfo`；如需自定义可在环境变量覆盖。
- 在本次 Codex CLI sandbox（socket 受限）环境下，启动 testenv/httpd/nghttpx/ws 需要 escalated 权限，否则会遇到 `PermissionError: Operation not permitted`。

### 6.4 可观测产物与目录结构（对比/定位入口）

- artifacts 根目录：`curl/tests/http/gen/artifacts/`
  - `<suite>/<case>/baseline.json`：baseline 侧 artifacts
  - `<suite>/<case>/qcurl.json`：QCurl 侧 artifacts
  - `<suite>/<case>/qcurl_run/download_*.data`：QCurl 侧落盘的 body/事件序列
  - `<suite>/<case>/service_logs/`：失败时收集的服务端日志（需 `QCURL_LC_COLLECT_LOGS=1`）
  - `<suite>/<case>/meta.json`：日志关联信息（包含 baseline/qcurl 的 req_id 等）
- baseline 下载文件：
  - 上游 `LocalClient(name='cli_*')`：`curl/tests/http/gen/<client_name>/download_*.data`
  - repo 内置 baseline（如 `qcurl_lc_http_baseline`）：同样落在对应 `LocalClient.run_dir`
- Gate 报告：
  - `build/libcurl_consistency/reports/junit_<suite>.xml`
  - `build/libcurl_consistency/reports/gate_<suite>.json`

### 6.5 如何判定一致/不一致、失败示例与定位路径

- 判定口径：以 `tests/libcurl_consistency/pytest_support/compare.py` 的字段对比为准（`request(s)`/`response(s)` 及可选 `cookiejar`/`error`/`response.headers_raw_*`）。
- 一致：主断言字段均一致（请求语义摘要 + 响应 hash/len + 状态码/协议族；WS 额外含事件序列）。
- 不一致：任一主断言字段不一致（包括缺失字段）；对比器会输出具体 diff 字段路径。
- 失败示例（对比器输出片段）：
  - `requests[0].headers mismatch: {...} != {...}`
  - `response.body_sha256 mismatch: <base> != <qcurl>`
- 定位路径（最小复现）：
  - 打开对应 case 的 `baseline.json` 与 `qcurl.json`（见 6.4）并对照 diff 字段
  - 校验双方 `download_*.data`（是否为空/是否部分写入/hash 是否一致）
  - 如需服务端证据：设置 `QCURL_LC_COLLECT_LOGS=1` 重跑，查看 `<suite>/<case>/service_logs/`（含 httpd/nghttpx/ws/proxy/observe 的日志）

### Q&A（关键决策）

- **`artifacts` 是否必须记录“服务端看到的请求字节”？**
  - 结论：不强制（不作为 P0 必选）。
  - 原因：在 HTTP/2/HTTP/3 下，服务端“看到的字节”包含帧化与（H）PACK 压缩后的传输表示，既难以稳定采集也不具备跨协议可比性。
  - 建议：记录“请求语义摘要”（method/url/关键头规范化）+ request body 的 `len/hash`，以“响应/回显字节”作为主要一致性判据；服务端视角只作为 **可选 debug 增强**（见 `tasks.md` 的 LC-12）。

- **P0 下载一致性是否必须包含 pause/resume（`cli_hx_download -P`）？**
  - 结论：不必须（不作为 P0 必选）。
  - 说明：P0 建议只对“最终下载文件字节一致”做强断言；`-P` 场景可用于增加流控扰动，但不强制 QCurl 具备 in-flight pause/resume 语义。
  - 注：P0 已包含“中断 + Range 续传（resume）”的一致性断言。
  - 现状：已落地 P2 的 pause/resume **弱判据**（LC-15a：事件存在性/顺序 + 终态文件字节一致，见 `tests/libcurl_consistency/test_p2_pause_resume.py`）与 **强判据/语义合同**（LC-15b：PauseEffective→ResumeReq 期间交付/写盘增量 Δ=0，见 `tests/libcurl_consistency/test_p2_pause_resume_strict.py`，纳入 `run_gate.py --suite all`）。
  - 若产品确实要求把 `cli_hx_download -P` 的 stderr 文本打点当作“pause window”契约（不推荐），需另行定义/补齐任务；LC-15a/LC-15b 均不以 stderr 打点窗口作为强判据依据。

### 6.6 常见陷阱与已知限制

- **HTTP/3 跳过不等于覆盖**：未构建 `qcurl_nghttpx_h3` 或 `env.have_h3()` 为 False 时，h3 变体会 skip；请在 Gate 报告中确认是否覆盖到 h3。
- **受限环境的端口/进程限制**：在 sandbox/容器中可能无法分配端口或启动 httpd/nghttpx/ws，需要相应权限或在宿主环境运行。
- **TLS 观测服务端依赖 CA 生成物**：`lc_observe_https` 复用 `curl/tests/http/gen/ca` 证书产物；首次需要跑一次 curl testenv 以生成 CA/证书（见 `tests/libcurl_consistency/conftest.py` 的 skip 条件）。
- **Header 归一化是白名单**：默认只比较少量关键头；如果某个头被视为产品契约，请先把它加入观测白名单并在 `tasks.md` 补齐一致性用例。
- **multipart boundary 不可比**：multipart 的 boundary/Content-Length 不稳定；当前只对齐服务端可解析出的 parts 语义摘要，避免把编码实现差异误判为不一致（见 `tests/libcurl_consistency/test_p1_multipart_formdata.py`）。
- **并发多请求默认集合对比**：ext_multi 默认按 URL 排序比较（集合等价），不比较完成顺序；若业务依赖时序（回调顺序/首包先后），需新增任务采集并对齐“完成顺序/关键事件序列”。
- **`cli_hx_download -P` 的打点不等于 pause window**：`PAUSE/RESUMED` 为 stderr 文本打点，且 `RESUMED` 打点可能晚于恢复调用，从而出现“打点区间内仍有 RECV 日志”的现象；因此门禁用例（LC-15a）不比较 pause window 内的数据/进度事件计数，强过程一致性需按 LC-15b 另行定义结构化事件边界。
- **`cli_hx_download -P` 的打点不等于 pause window**：`PAUSE/RESUMED` 为 stderr 文本打点，且 `RESUMED` 打点可能晚于恢复调用，从而出现“打点区间内仍有 RECV 日志”的现象；因此 LC-15a 不比较 pause window 内的数据/进度事件计数。LC-15b 通过 repo 内可控 baseline + 结构化事件边界定义 PauseEffective（语义合同边界），从而在不依赖 stderr 窗口的前提下实现强判据对比。
- **Sync 模式连接复用差异**：QCurl Sync（`sendGetSync`/`sendPostSync`）基于 `curl_easy_perform` 的 per-request handle 执行，单次调用不可跨请求复用连接；如需对齐 keep-alive/multiplex 复用行为，应使用 Async（multi）路径并定义可观测统计口径（见 `tasks.md` 的 LC-31）。
- **WS 握手头白名单**：默认不记录 `Sec-WebSocket-Key` 等随机头；已通过扩展 allowlist + ext 用例覆盖 `permessage-deflate` 请求头一致性（见 `tasks.md` 的 LC-34）。
