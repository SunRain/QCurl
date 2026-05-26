# QCurl ↔ libcurl 可观测一致性测试

本目录只回答一个问题：**在外部可观测层面，QCurl 与 libcurl 是否给出相同结果。**

这里只保留稳定 contract、运行入口与排查路径；维护者状态看板已移到
`docs/internal/maintainer-backlog/libcurl-consistency.md`，专题决策仍落到 handoff 文档。

## 1. 一致性定义

### 1.1 默认比较什么

- 请求语义摘要：`method/url/关键请求头/请求体 len+hash`
- 响应结果：`status/body len+hash/协议族`
- WebSocket：握手白名单字段与事件序列
- 特定专题的附加字段：例如 `cookiejar`、`error`、`progress_summary`、`pause_resume`

### 1.2 默认不比较什么

- 内部实现细节、线程调度、句柄释放顺序
- 动态头和时间快照（如 `Date`、`Server`）
- multipart boundary 之类实现生成物
- 只存在于某个版本或某次运行中的临时诊断文本

覆盖映射文件：`tests/libcurl_consistency/coverage-map.yaml`。它按 observable
contract 记录 suite、pytest 文件、artifact 字段和证据类型；新增专题时必须同步更新。

## 2. 测试分层

| 套件 | 目的 | 典型内容 | 入口 |
|------|------|----------|------|
| `p0` | 最小门禁 | 下载/上传/基础 WS 的主断言 | `run_gate.py --suite p0` |
| `p1` | 常用语义补齐 | cookie、redirect、multipart、timeouts、methods | `run_gate.py --suite p1` |
| `p2` | 错误/流控/协议约束 | TLS、cancel、pause/resume、backpressure、protocol restrictions | `run_gate.py --suite p2` |
| `ext` | 扩展覆盖 | h2/h3 multi、HTTP/3 policy、WS 低层、额外 TLS/cache 场景 | `QCURL_LC_EXT=1` + `--with-ext` |

说明：

- `p0/p1/p2` 属于 fail-closed gate 分层；一旦选中执行，就必须由 `run_gate.py` 负责 `skipped/no_tests/schema/redaction` 复核。
- `ext` 是显式 opt-in 的扩展覆盖层，默认不替代主 gate，也不放宽 `run_gate.py` 的 strict policy。
- smoke 用例只能证明粗粒度健康状态；不能在 README、tasks 或 gate 报告里写成强合同证明。

## 3. 统一运行入口

构建与环境准备统一参考：

- `docs/dev/build-and-test.md`

最常用的 gate 入口：

```bash
python3 tests/libcurl_consistency/run_gate.py --suite p0 --build
python3 tests/libcurl_consistency/run_gate.py --suite p1 --build
python3 tests/libcurl_consistency/run_gate.py --suite all --build
QCURL_LC_EXT=1 python3 tests/libcurl_consistency/run_gate.py --suite all --with-ext --build
```

`run_gate.py` 是唯一受支持的取证入口：它会把 `skipped_tests`、`no_tests_executed`、
`junit_parse_error`、`artifacts_schema`、`redaction` 违规统一提升为 gate 失败。直接跑裸 `pytest`
可以用于本地诊断，但不能单独作为通过证据，也不应手工拼装 artifacts。

`run_gate.py` 会在 `--build` 模式下刷新
`build/libcurl_consistency/reports/capabilities.json`：

- planner 按 manifest 决定是否纳入 feature-dependent 文件，而不是让 pytest 在运行时读取 `src/*.h` 再 `skip`
- default gate 会在进入 pytest 之前检查 `capabilities.json`、`QCURL_QTTEST` 和 `curl/tests/libtest/libtests`；缺任一前置时直接以 gate preflight 失败结束，不再把这类问题留给 test body 的 `pytest.skip(...)`
- 当前已纳入 manifest 选案的专题包括：`Accept-Encoding`、raw-body replay、unknown-size chunked POST、`TLS pinned public key`、raw request header、SOCKS success、302/303/308 redirect、Range boundary、ext-only HTTP/3 policy
- `TLS pinned public key` 用例只消费固定 fixture `tests/libcurl_consistency/testdata/pinned_public_key_sha256.txt`

## 4. 产物与证据路径

### 4.1 工件

- `curl/tests/http/gen/artifacts/<suite>/<case>/baseline.json`
- `curl/tests/http/gen/artifacts/<suite>/<case>/qcurl.json`
- `curl/tests/http/gen/artifacts/<suite>/<case>/qcurl_run/download_*.data`

其中以下专题字段会被 UCE 继续消费：

- `payload.hes`：HES（headers / encoding / `Expect: 100-continue` / chunked upload）专题证据
- `payload.ctbp`：CTBP（连接复用 / TLS 边界）专题证据
- `qcurl_run/dci_evidence_*.jsonl`：Qt/DCI timeline 证据（由 QtTest 直接落盘）

### 4.2 gate 报告

- `build/libcurl_consistency/reports/gate_<suite>.json`
- `build/libcurl_consistency/reports/junit_<suite>.xml`
- `build/libcurl_consistency/reports/capabilities.json`

### 4.3 失败时的服务端日志

设置：

```bash
QCURL_LC_COLLECT_LOGS=1
```

失败时可查看：

- `curl/tests/http/gen/artifacts/<suite>/<case>/service_logs/`

## 5. 关键前置条件

### 5.1 curl testenv

该套件依赖上游 `curl/tests/http/testenv` 提供的本地 httpd / nghttpx / ws 等环境；端口绑定受限时，gate 可能无法启动。

### 5.2 HTTP/3

HTTP/3 覆盖位于 ext-only 分层，依赖 `QCURL_LC_EXT=1` 和 `--with-ext`。真实 H3 success 还依赖 h3-capable `nghttpx`。相关构建方式见：

- `tests/libcurl_consistency/nghttpx_from_source/README.md`

若业务要求“缺少 h3 覆盖即失败”，应显式设置：

```bash
QCURL_REQUIRE_HTTP3=1
```

`run_gate.py --suite p2 --build` 不包含 HTTP/3 policy。`test_ext_http3_version_policy.py` 只由 `run_gate.py --suite all --with-ext` 规划执行。

## 6. 重点 contract

### 6.1 pause/resume

- 弱判据与强判据都已落地
- 强判据不依赖 `cli_hx_download -P` 的 stderr 打点窗口
- 详细说明见：
  - `tests/libcurl_consistency/LC-15_handoff.md`

### 6.2 响应头

- 响应头一致性以 `rawHeaderData()` 路径为准
- 重复头/折叠头等专题在专门用例中验证，不靠 README 文字描述兜底

### 6.3 空 body / cancel / timeout

- 这些场景的 contract 以专题 pytest + Qt 执行器结果为准
- 若实现变更影响这些语义，必须同步更新专题用例和对比器，而不是只改文档

### 6.4 可观测 follow-up coverage

- raw request header：`test_p1_request_headers.py` 覆盖普通自定义头、同名覆盖、大小写差异 key 与敏感头 redaction；request artifact 写入脱敏 raw lines / digest 并参与 compare。
- SOCKS success：`test_p1_socks_success.py` 覆盖 `Socks5` IP 目标与 `Socks5Hostname` 域名目标，代理日志记录 `ATYP` / `dst` / `rep`。
- 302 / 303 / 308 redirect：`test_p1_redirect_302_303_308.py` 覆盖 method/body 序列与 seekable / non-seekable 终态。
- Range boundary：`test_p2_range_boundaries.py` 通过 `QCNetworkResumableDownloadJob` 覆盖 `Range: bytes=N-`、416 matching complete、`Content-Range.start` mismatch。
- ext-only HTTP/3 policy：`test_ext_http3_version_policy.py` 覆盖 HTTP/1.1 fallback 与 `Http3Only` failure；`test_ext_http3_success_h3.py` 在 H3 可用时覆盖真实 H3 success。两者只由 `--with-ext` 规划执行，不属于 `--suite p2`。

## 7. 扩展规则

新增一致性专题前，应先回答四个问题：

1. 该差异是否真的能被调用方稳定观察到？
2. 该字段是否可跨协议、跨环境稳定采集？
3. 该对比是产品契约还是实现细节？
4. 失败后是否有明确的定位路径？

只要其中任一答案是否定的，就不应直接把该内容加入默认 gate。

## 8. 维护规则

- 不在本文件追加“本地跑过一次通过”的记录
- 不在本文件保存单次环境快照或版本清单
- 需要状态看板时更新 `docs/internal/maintainer-backlog/libcurl-consistency.md`
- 需要专题决策时补充 handoff / 设计说明

## 9. 相关文件

- `docs/internal/maintainer-backlog/libcurl-consistency.md`
- `tests/libcurl_consistency/LC-15_handoff.md`
- `tests/libcurl_consistency/run_gate.py`
- `tests/libcurl_consistency/pytest_support/compare.py`
- `tests/libcurl_consistency/tst_LibcurlConsistency.cpp`
- `docs/uce/README.md`
