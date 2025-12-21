# QCurl ↔ libcurl（含 libtest 工具）数据一致性测试候选集

> 目标：基于上游 `curl/tests/` 的现有用例，筛选出可用于判断 **QCurl 与 libcurl API 在“可观测数据层面”一致性** 的最小回归集合。
>
> 约束：当前构建的 libcurl 已启用 **HTTP/3** 与 **WebSockets**。

---

## 1. 一致性定义（本候选集覆盖范围）

本候选集聚焦“对外可观测的数据一致性”，包括：

- **请求侧**：方法/URL/关键头（如 `Host`、`Range`、`Cookie`、`Proxy-*`、`Content-Length`）以及请求体字节（包含二进制与 `\\0` 的情况）。
- **响应侧**：响应体字节、关键状态（如 2xx/4xx）与基本协议路径（HTTP/2/HTTP/3）。
- **WebSocket**：握手成功与帧收发路径（ping/pong、data frames）的可观测结果。

不覆盖（或不作为一致性基准）：

- libcurl 内部状态机/资源释放顺序、调试日志格式、计时信息等“非数据层”行为。
- QCurl 当前未暴露/未采用的 libcurl 高级选项（如 `CURLU` URL API、`CURLOPT_COOKIELIST`、`CURLOPT_SHARE`、HTTP trailers、`CURLOPT_AWS_SIGV4` 等）。

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

### P2（低优先级：安全语义对齐）

- **TLS 校验语义（成功/失败路径）**
  - 覆盖：verifyPeer/verifyHost + 自定义 CA（`caCertPath/CAINFO`）下的成功路径，以及缺少 CA 时的证书错误路径。

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

### 6.1 观测机制（P0 Gate 关键）

为避免“构造出来的语义摘要”导致伪通过，P0 的关键语义字段改为 **服务端观测值**：

- **HTTP(S)**：从 httpd 的 `access_log` 提取 `method/url/status/协议族(http/1.1|h2)` 以及关键请求头白名单（`Range`、`Content-Length`）。
  - 日志位置：`curl/tests/http/gen/apache/logs/access_log`
  - LogFormat 由 `curl/tests/http/testenv/httpd.py` 生成。
- **WebSocket**：ws echo server 在握手时写出 JSONL（path + 头白名单），用于提取 WS 的 `url` 与握手语义（不记录随机/不可比的 header）。
  - 日志位置：`curl/tests/http/gen/ws_echo_server/ws_handshake.jsonl`

pytest driver 会为 baseline/QCurl 各自注入独立的 query `id` 以定位对应的服务端观测记录，并在写回 `artifacts` 前剔除 `id`（避免对比噪声）。

如需在失败时自动收集服务端日志用于 debug，可设置环境变量：

- `QCURL_LC_COLLECT_LOGS=1`：当某个 case 断言失败/异常时，将 `httpd/nghttpx/ws` 的关键日志复制到对应目录 `curl/tests/http/gen/artifacts/<suite>/<case>/service_logs/`，并写出 `meta.json`（包含 baseline/qcurl 的 req_id）。

### 6.2 HTTP/3 覆盖的前置条件

即使 curl 构建启用了 HTTP/3，`env.have_h3()` 仍依赖 **h3-capable 的 nghttpx**（需要 ngtcp2/nghttp3）。本仓库默认在构建 `tst_LibcurlConsistency` 时通过 `qcurl_nghttpx_h3` 从源码构建并安装 `build/libcurl_consistency/nghttpx-h3/bin/nghttpx`，并在 `curl/tests/http/config.ini` 中指向该路径；若未构建该 target，则 P0 只覆盖 http/1.1 + h2（h3 变体会自动跳过）。

### 6.3 复现命令（本仓库默认路径）

- 构建 QCurl Qt Test：
  - `cmake -S . -B build && cmake --build build --target tst_LibcurlConsistency -j"$(nproc)"`
- 构建 curl baseline（libtests）：
  - `cmake -S curl -B curl/build && cmake --build curl/build --target libtests -j"$(nproc)"`
- 运行（P0）：
  - `QCURL_QTTEST="build/tests/tst_LibcurlConsistency" pytest tests/libcurl_consistency/test_p0_consistency.py`

#### Gate 入口（推荐）

为了把 P0 变成“真正可靠的 Gate”，提供统一入口脚本（输出 JUnit XML + JSON）：

- `python tests/libcurl_consistency/run_gate.py --suite p0 --build`

说明：
- 默认会设置 `QCURL_LC_COLLECT_LOGS=1`，失败时自动把 `httpd/nghttpx/ws` 关键日志复制到 `curl/tests/http/gen/artifacts/<suite>/<case>/service_logs/`。
- 在本次 Codex CLI sandbox 环境下，运行需要 `sandbox_permissions=require_escalated`（否则端口分配/服务启动会被拒绝）。

说明：
- `tests/libcurl_consistency/conftest.py` 会默认注入 `CURL_BUILD_DIR=curl/build`、`CURL=curl/build/src/curl`、`CURLINFO=curl/build/src/curlinfo`；如需自定义可在环境变量覆盖。
- 在本次 Codex CLI sandbox（socket 受限）环境下，启动 testenv/httpd/nghttpx/ws 需要 escalated 权限，否则会遇到 `PermissionError: Operation not permitted`。

### Q&A（关键决策）

- **`artifacts` 是否必须记录“服务端看到的请求字节”？**
  - 结论：不强制（不作为 P0 必选）。
  - 原因：在 HTTP/2/HTTP/3 下，服务端“看到的字节”包含帧化与（H）PACK 压缩后的传输表示，既难以稳定采集也不具备跨协议可比性。
  - 建议：记录“请求语义摘要”（method/url/关键头规范化）+ request body 的 `len/hash`，以“响应/回显字节”作为主要一致性判据；服务端视角只作为 **可选 debug 增强**（见 `tasks.md` 的 LC-12）。

- **P0 下载一致性是否必须包含 pause/resume（`cli_hx_download -P`）？**
  - 结论：不必须（不作为 P0 必选）。
  - 说明：P0 建议只对“最终下载文件字节一致”做强断言；`-P` 场景可用于增加流控扰动，但不强制 QCurl 具备 in-flight pause/resume 语义。
  - 注：P0 已包含“中断 + Range 续传（resume）”的一致性断言。
  - 若产品确实要求对齐 `-P`，建议作为独立用例/更高阶 suite（见 `tasks.md` 的 LC-15）；断点续传（“中断 + Range 续传”）由 P0 覆盖。
