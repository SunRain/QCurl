# libcurl_consistency maintainer backlog

> Internal maintainer status board. It is not a public testing entrypoint and does not define current release readiness. Stable contract and run instructions stay in `tests/libcurl_consistency/README.md` and `docs/dev/build-and-test.md`.

本文只保留当前专题状态、边界与维护规则；逐次执行日志与回归记录统一看 `reports/` 和 artifacts。

## 1. 当前状态

- 基础 gate（baseline runner / QCurl runner / compare / schema）已落地
- `p0` / `p1` / `p2` / `ext` 分层已落地
- HTTP/3、WebSocket、pause/resume、backpressure、multipart 等专题均已有专门用例
- 覆盖映射已落地：`tests/libcurl_consistency/coverage-map.yaml`
- 当前文档的职责是“告诉维护者哪里有 contract、哪里有证据”，不是保存逐次执行日志

## 2. 主题状态

| 主题 | 代表 ID | 状态 | 说明 |
|------|---------|------|------|
| artifacts / compare / gate 基础设施 | `LC-0 ~ LC-16` | 已完成 | 基础 runner、schema、对比器与 gate 已稳定存在 |
| pause/resume 一致性 | `LC-15` | 已完成 | 弱判据 + 强判据均已接入；详情见 `LC-15_handoff.md` |
| 响应头原始字节 / unfold / 重复头 | `LC-26`、`LC-52` | 已完成 | 以 `rawHeaderData()` 路径和专题用例为准 |
| 空 body 与 `readAll()` 终态语义 | `LC-27` | 已完成 | 终态空 body 应表现为空字节，而不是额外文档约定 |
| 超时语义 | `LC-28` | 已完成 | connect/total/low-speed 以专题用例为准 |
| 取消语义 | `LC-29` | 已完成 | 取消后的终态与事件约束已固化 |
| 进度摘要 | `LC-30` | 已完成 | 对齐稳定摘要，不比较原始事件频率 |
| 连接复用/多路复用可观测性 | `LC-31` | 已完成 | 默认看统计与集合等价，不拿完成顺序当默认契约 |
| 错误路径 | `LC-32` | 已完成 | 连接拒绝、407、malformat 等已覆盖 |
| HTTP 方法面 | `LC-33`、`LC-36 ~ LC-40` | 已完成 | HEAD / PATCH / DELETE / redirect / expect-100 等已覆盖 |
| WS 扩展场景 | `LC-34` | 已完成 | 基础 WS 与扩展场景分层管理 |
| multipart 语义一致性 | `LC-35` | 已完成 | 比较 parts 语义，不比较 boundary |
| backpressure 最小合同 | `LC-55` | 已完成 | 以边沿与 body 字节为主断言 |
| raw request header 可观测一致性 | `LC-FU-raw-header` | 已完成 | `test_p1_request_headers.py` 覆盖自定义头、同名覆盖、大小写差异 key 与敏感头 redaction；request artifact 包含脱敏 raw lines / digest 并参与 compare |
| SOCKS 成功路径 | `LC-FU-socks-success` | 已完成 | `test_p1_socks_success.py` 覆盖 SOCKS5 IP 目标与 Socks5Hostname 域名目标，观测 `ATYP` / `dst` / `rep` |
| 302 / 303 / 308 redirect | `LC-FU-redirect-302-303-308` | 已完成 | `test_p1_redirect_302_303_308.py` 覆盖 method/body 序列与 seekable / non-seekable 终态 |
| Range 边界交叉证明 | `LC-FU-range-boundary` | 已完成 | `test_p2_range_boundaries.py` 使用 `QCNetworkResumableDownloadJob` 覆盖 `Range: N-`、416 complete、mismatch start |
| ext-only HTTP/3 fallback / Http3Only / H3 success | `LC-FU-http3-policy` | 已完成 | `test_ext_http3_version_policy.py` 与 `test_ext_http3_success_h3.py` 只由 `--with-ext` 规划；suite / case id 使用 `ext_http3_version_policy` |

## 3. 默认 gate 不承诺的内容

以下内容即使被观察到，也不自动进入默认 gate：

- libcurl 内部状态机细节
- 动态头与时间快照
- multipart boundary 或其他实现生成物
- 仅在某个版本输出的诊断文本
- 未明确定义为产品契约的完成顺序/调度顺序

## 4. 何时新开 LC 任务

出现以下任一情况时，应新增或重开 LC 任务：

1. 新增 public API，且其外部可观测结果需要与 libcurl 对齐
2. 现有 contract 发生变化，compare/schema 无法继续稳定表达
3. 某类差异反复出现在 reports 中，且现有专题无法归因
4. 文档里只能靠“口头解释”而不能靠专题测试证明

## 5. 变更原则

- 优先修改测试和 compare 规则，再更新文档
- README 记录稳定 contract
- handoff 记录专题决策
- 本文件只维护专题级状态，不保留逐次执行日志

## 6. 大文件拆分边界清单

当前大文件治理按文件事实分层：第一批 gate runner 已低于 400 行，剩余大型测试 / fixture 文件继续作为后续治理对象。

### 6.1 已达标 runner

以下文件已低于 400 行，不再作为“未达标大文件”描述。

| 文件 | 当前行数 | 当前职责 | 验证入口 |
|------|----------|----------|----------|
| `scripts/run_uce_gate.py` | 109 | UCE gate CLI wrapper | `pytest -q tests/test_run_uce_gate.py` |
| `tests/libcurl_consistency/run_gate.py` | 392 | libcurl consistency gate CLI wrapper | `pytest -q tests/libcurl_consistency/test_run_gate_unit.py` |
| `tests/libcurl_consistency/http_observe_server.py` | 70 | observable HTTP server CLI wrapper | `python3 tests/libcurl_consistency/http_observe_server.py --help` |
| `tests/public_api/run_public_api_checks.py` | 311 | public API gate CLI wrapper | `pytest -q tests/public_api/test_run_public_api_checks.py` |

### 6.2 后续治理对象

以下条目只定义后续重构边界，不改变当前 case id、artifact schema、CLI 参数或 gate 分层。

| 文件 | 当前行数 | 目标职责 | 候选新文件 | 不变项 | 验证入口 |
|------|----------|----------|------------|--------|----------|
| `tests/libcurl_consistency/conftest.py` | 898 | pytest fixture、curl testenv、capability manifest 与 server lifecycle 分离 | `pytest_support/env_fixture.py`、`pytest_support/testenv.py` 或等价 helper | fixture 名称、artifact 根目录、capability manifest 语义 | p0、p1、p2 gate |
| `tests/libcurl_consistency/tst_LibcurlConsistency.cpp` | 3884 | QtTest 执行器按 case family 拆分 | `tst_lc_http3.cpp`、`tst_lc_headers.cpp`、`tst_lc_redirect_range.cpp` 或等价 helper | `QCURL_LC_CASE_ID`、工作目录产物、pytest `case_env` | p1、p2、all+ext gate |
| `tests/qcurl/tst_QCNetworkStreamUpload.cpp` | 868 | stream upload server/helper 与分支用例拆分 | 后续 stream upload helper/source | 现有 QtTest case 名、`env;local_port` label、skip=fail 规则 | 对应 QtTest + label matrix guard |
| `tests/libcurl_consistency/http_baseline_client.cpp` | 1120 | baseline CLI 解析、curl option mapping、transfer helpers 拆分 | `http_baseline_options.*`、`http_baseline_transfer.*` | CLI 参数与 exit code 行为 | p1 request header、redirect、range、HTTP/3 policy |
| `tests/qcurl/tst_QCNetworkReply.cpp` | 2302 | 按 reply 生命周期、body、headers、error 分组 | 后续 `tst_QCNetworkReply_*` 系列 | 现有 QtTest case 名和 skip=fail 规则 | `python3 scripts/check_skip_contract.py --cmake tests/qcurl/CMakeLists.txt` 与对应 QtTest |
| `tests/qcurl/tst_QCNetworkScheduler.cpp` | 1617 | scheduler 队列、优先级、取消路径分组 | 后续 scheduler helper/source | public contract 与现有测试名 | 对应 QtTest + skip contract |
| `tests/qcurl/tst_Integration.cpp` | 1240 | 集成链路按网络场景分组 | 后续 integration helper/source | 外部服务前置与 case 名 | 对应 QtTest + skip contract |
| `tests/qcurl/tst_QCWebSocket.cpp` | 974 | WS handshake、frame、close/error 分组 | 后续 websocket helper/source | WebSocket 事件语义与 case 名 | 对应 QtTest + skip contract |

## 7. 相关入口

- `tests/libcurl_consistency/README.md`
- `tests/libcurl_consistency/coverage-map.yaml`
- `tests/libcurl_consistency/LC-15_handoff.md`
- `tests/libcurl_consistency/run_gate.py`
- `tests/libcurl_consistency/pytest_support/compare.py`
- `build/libcurl_consistency/reports/`
