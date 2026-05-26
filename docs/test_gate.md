# 测试门禁可靠性说明

> Maintainer evidence contract: this document is not part of the first-public-release user navigation. Keep it focused on gate semantics and reproducible evidence.

本文总结默认 gate 的可证明边界、非证明边界和最可靠的证据入口。

## 0. 当前入口分层

- `scripts/run_release_gate.py --tier fast|strict|full`
  - RC / Stable 发布门禁入口；不调用 `git`
  - `fast`：`contract.json`、`public-api`、`public-api-slow`
  - `strict`：在 fast 基础上增加 QtTest skip=fail、deprecated curl API、label matrix、skip contract
  - `full`：在 strict 基础上增加完整 CTest、libcurl consistency full gate、ABI diff、capability matrix 和 release metadata scan
- `scripts/run_uce_gate.py --tier pr|nightly|soak`
  - UCE 统一证据入口：产出 `manifest.json`、`policy_violations.json`、专题 report、`test-artifacts/` 与 `tar.gz`
  - `pr`：最小一致性证据（offline + `libcurl_consistency p0/p1` + TLC/HES 最小 contract）
  - `nightly`：在 `pr` 基础上补齐 DCI fixed seed、CTBP、HES 扩展、BP（backpressure）与 netproof/strace
  - `soak`：沿用 nightly contract，并放大固定 seed 组与长跑时长
- `scripts/run_basic_no_problem_gate.py`
  - 仍保留为 historical acceptance gate；在 UCE 完整接管 acceptance 归档前继续作为并行入口
- `tests/libcurl_consistency/run_gate.py`
  - 仍保留为专题 provider；UCE 复用其 evidence，不替换其专题 contract

## 1. 当前最强的证据来源

- `tests/libcurl_consistency/run_gate.py`
  - skip=fail
  - schema 校验
  - 脱敏扫描
  - capability manifest 选案：先产出 `build/libcurl_consistency/reports/capabilities.json`，再决定是否纳入 feature-dependent pytest 文件；其中 `capabilityMatrix` 记录 HTTP/2、HTTP/3、WebSocket、HSTS、Alt-Svc、proxy/SOCKS、TLS pinned public key 和 raw observability 的 Fail / Warn / Preview 归属
- `scripts/qcurl_abi_gate.py`
  - release ABI baseline / ABI diff gate
  - 缺少 `abidw` / `abidiff`、共享库、头目录或调试信息时 fail-closed
  - 通过 public header layout scan 不等于通过 Stable ABI gate
- `public-api-slow`
  - 覆盖 default Core staging install、export contract、Core consumer smoke
  - 同时覆盖 Blocking Extras / Test Support / Other Extras 的 opt-in install、default Core negative consumer 和 isolated consumer smoke
  - Blocking Extras fixture 必须覆盖 `maxInMemoryBodyBytes`、`BodyTooLarge`、`downloadToDevice()`、`bytesReceived()` 与 curl diagnostic code
  - Core consumer fixture 必须覆盖 `QCNetworkRedirectConfig` / `QCNetworkTransferConfig`，证明 Request 配置族在默认安装面可用
  - Other Extras fixture 必须覆盖 `QCNetworkDiagnostics.h` 与 `QCNetworkMiddlewareExtras.h`，并由 default Core 负向 consumer 证明其不能隐式 include
- `QCURL_BUILD_SHARED_LIBS=OFF` static public-api gate
  - 需要在独立 `build-static` 目录重跑 `public-api` 与 `public-api-slow`
  - static export 允许必要的 `CURL::libcurl` / `ZLIB::ZLIB` public dependency，但必须由 `QCurlConfig.cmake` 的 `find_dependency()` 补齐
- 强判据专题
  - `pause_resume_strict`
  - `resp_headers_raw`
  - 其他明确比较原始字节或结构化事件边界的用例
- `tests/qcurl/CMakeLists.txt`
  - 通过 `FAIL_REGULAR_EXPRESSION` 把 `QSKIP` 视为无证据失败
  - `env/local_port/httpbin/websocket` 组通过 `tests/qcurl/run_qttest_with_preflight.py` 在进入 QtTest 前先做 suite 级前置检查，缺少本地端口、`QCURL_HTTPBIN_URL`、`node` 或受控 `ws` 依赖时直接 fail-closed
  - `tst_QCNetworkHttp2` 额外通过 `qcurl_http2_capability_probe` + `--require-http2-suite` 把 HTTP/2 编译期/运行期能力与本地 fixture 前置统一到 preflight
  - `tst_QCNetworkDiagnosticsLocal` 已作为 `env;local_port;diagnostics` 的 deterministic provider 接入默认 gate：覆盖 `resolveDNS(localhost)`、本地 HTTP 200/404 probe、local HTTP `diagnose()`，以及 repo TLS fixture 的 `checkSSL()` 合同；原 `tst_QCNetworkDiagnostics` 保持 `external_network`，只负责公网探测

## 2. 当前门禁不能证明的内容

门禁全绿，不等于以下问题一定不存在：

- 资源释放与生命周期问题
- 全量异步竞态
- 所有连接复用/池化边界
- 所有 TLS 细节与平台差异
- 所有头部、压缩和时序语义

默认 gate 比较的是“被定义为可观测 contract 的字段”，不会把所有实现细节都拉进来比较。

其中 `QCNetworkDiagnostics::checkSSL()` 目前没有显式 CA 注入入口，所以 local diagnostics TLS gate 只证明“repo fixture 会走到证书校验路径，并返回明确的证书结果或 SSL 失败”，不把“默认必须通过”当作当前 contract。

当前已纳入 UCE 的“专题补强”如下：

- TLC：时间线不变量（headers-before-body / pause quiet window / terminal quiet）
- CTBP：连接复用与 TLS 边界
- HES：头部 / 压缩 / `Expect: 100-continue` / chunked 上传语义
- DCI：固定 seed 的 mock chaos（pause / cancel / deleteLater）与 Qt timeline 证据（deterministic）
- BP：backpressure 语义合同（buffer pressure + user pause/resume；独立于 DCI fixed-seed suite）
- HFG：offline suite 的 `strace` network syscall 证明

## 3. 应如何解读 P0

P0 的作用是提供最小、可重复、可机器判定的字节级或摘要级证据。

不要从 P0 推断：

- 所有并发语义都已经被覆盖
- 所有复用语义都已经被覆盖
- 所有 pause/resume 细节都已经被覆盖

这些能力是否被纳入默认 gate，必须以对应专题测试是否存在、README 是否明确声明为准。

## 4. 可靠性使用原则

1. 先看 suite 定义，再看报告结果。
2. 先看专题 contract，再看“是否全绿”。
3. 如果一个差异只能靠长篇解释成立，而没有专题测试兜底，就不应把它当作稳定证据。

## 5. 何时需要补强门禁

出现以下情况时，应该新增或强化专题用例，而不是继续堆说明文字：

1. 某类失败反复出现但 reports 无法归因
2. 某个 public API 的外部行为变成产品契约
3. README 需要靠大量“例外说明”才能解释为什么全绿
4. 某个结论只能依赖单次日志或人工判断

## 6. 相关入口

- `tests/README.md`
- `tests/libcurl_consistency/README.md`
- `docs/internal/maintainer-backlog/libcurl-consistency.md`
- `docs/dev/build-and-test.md`
