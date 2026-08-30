# QCurl Changelog

本文记录当前 `2.0.0` hard-break 候选和已发布的 `1.0.0` 历史版本。pre-1.0 日期流水、RC 草稿和旧不兼容变更过程已移入 `docs/internal/pre-1.0-history.md` 索引和内部 raw archive，不再作为当前用户-facing release history。

`v1.0.0` 已发布；`v2.0.0` 尚未创建 tag、推送或发布，因此当前变更继续放在 `Unreleased`。

## [Unreleased]

### Release readiness

- 将当前候选发布线切换为 `QCurl 2.0.0`：`PROJECT_VERSION=2.0.0`、shared library `SOVERSION=2`、默认 shared library `libQCurl.so.2.0.0`。
- 明确 2.0.0 的默认 Core install surface 保证源码兼容，但 ABI 非稳定；下游在每次 QCurl 更新后必须重新编译和链接。
- Full release gate 以 `scripts/run_release_gate.py --tier full --abi-mode none` 和六棵显式构建树为入口，覆盖 shared/static public API、strict QtTest、完整 CTest、QCurl/libcurl observable consistency、动态符号 allowlist、capability matrix、sanitizer、Doxygen 和 metadata scan，不要求 v2 ABI baseline。

### Source-compatible Core

- 将 `QCNetworkAccessManager`、`QCNetworkRequest`、`QCNetworkReply` 及异步 HTTP `head()` / `get()` / `post()` / `put()` / `patch()` / `deleteResource()` / `sendCustomRequest()` 路径纳入源码兼容 Core。
- 将 HTTP method / version / error / priority types、TLS、proxy、timeout、retry、redirect、transfer、cache policy、connection pool config / manager、middleware base、logger、default logger 和 cancel token 纳入源码兼容 Core。
- 将 proxy 的 fail-closed 默认行为纳入源码兼容 Core：未显式配置代理或显式设置 `QCNetworkProxyConfig::ProxyType::None` 时，QCurl 会禁止 libcurl 从 `HTTP_PROXY` / `HTTPS_PROXY` 等环境变量隐式继承代理。
- 将 lane-aware scheduler、Cache lookup、Multipart builder、body helper、transfer/download/resumable job types 纳入源码兼容 Core。
- 将 Core cookie API `QCurl::QCCookie`、`QCCookieOperationResult`、`QCCookieExportResult` 纳入源码兼容 Core；默认 Core consumer 不需要 QtNetwork cookie 类型或 `Qt6Network` 链接依赖。

### Package-shipped non-default surfaces

- 交付面固定为 Core、Blocking Extras、Other Extras、Test Support 四个逻辑消费组件；物理产物收敛为 `libQCurl`、`libQCurlOtherExtras` 和静态 `libQCurlTestSupport` 三个库，Preview 是成熟度，Internal 是可见性，不建立第五模块。
- Blocking Extras 保留独立头文件、`COMPONENTS BlockingExtras` 和 `QCurl::BlockingExtras` 显式消费面，但实现编入 `libQCurl`；该 CMake target 为 `INTERFACE`，只需安装 `BlockingExtrasDevelopment`，不再生成独立 runtime library。
- Other Extras 保持独立 shared/static 聚合库；consumer 需要安装 `OtherExtrasRuntime` / `OtherExtrasDevelopment`，显式 `COMPONENTS OtherExtras` 并链接 `QCurl::OtherExtras`。Diagnostics 与 WebSocket 标记为 Preview，Middleware Extras 标记为 Stable。
- Test Support 从 Core 抽离为开发静态库；consumer 只安装 `TestSupportDevelopment`，显式 `COMPONENTS TestSupport` 并链接 `QCurl::TestSupport`。它不属于生产 Runtime。
- Core 动态符号 allowlist 同时覆盖 Core 与 Blocking Extras 公共符号；Other Extras 保持独立 allowlist，Test Support 为静态开发库，不建立动态符号门禁。

### Compatibility boundary

- 1.0.0 是已发布的历史 ABI baseline；2.0.0 相对 v1 hard-break，并建立新的 Core 源码兼容线，但不建立稳定 ABI。
- 本发布不提供 v1 surface 的兼容层、alias、wrapper、ABI shim 或迁移窗口。
- 下游应以 2.0.0 头文件、CMake package、pkg-config 和当前 shared/static 产物重新构建。
- 后续每次 QCurl 2.x 更新也要求下游重新编译和链接；`SOVERSION 2` 只表示当前装载命名空间。
- `QCNetworkRequest(const QUrl &)` 现在是 `explicit`：依赖 `QUrl` 隐式转换的源码必须改为 `QCNetworkRequest{url}`；该变更不改变构造函数符号或对象布局，不提供兼容重载或迁移开关。
- Core cookie public API 不保留 QtNetwork cookie overload / alias / shim / wrapper；下游应迁移到 `QCurl::QCCookie`。
- 未显式配置代理的请求不再继承 libcurl 环境代理；需要代理的调用方必须显式设置 `QCNetworkProxyConfig`。

### ABI policy

- 2.0.0 的正式 release gate 使用 `abiMode=none`，不生成或要求 `qcurl-core-v2.abi.xml`。
- 动态符号 allowlist 继续检查 private/internal export 泄漏，但不构成二进制兼容证据。
- 稳定 ABI 合同、支持平台矩阵和首份 baseline 作为未来 TODO，见 `docs/roadmap/stable-abi-contract-and-baseline.md`。

### Verification

- `python3 -m py_compile scripts/qcurl_abi_gate.py scripts/run_release_gate.py`
- `pytest -q tests/test_release_gate_unit.py`
- 按 `docs/dev/build-and-test.md` 的六树命令执行 `scripts/run_release_gate.py --tier full --abi-mode none --dry-run`
- `python3 scripts/run_release_gate.py --scan-metadata --release-shared-build-dir build-release-shared`
- `git diff --check`

### Not included in this release claim

- Tag、GitHub Release、release assets、checksums、SBOM、signature 和 provenance 尚未创建或上传。
- WebSocket、Diagnostics、Middleware Extras 不随 2.0.0 Core 一起宣布 Stable。
- 稳定 ABI 合同、v2 ABI baseline 和跨 2.x 二进制兼容承诺不属于本次发布。
- 本条目是 2.0.0 release-candidate changelog；只有完成后续 tag 与 GitHub Release 授权，才能改写为已发布条目。

## [1.0.0] - 2026-06-24

v1.0.0 已发布。该版本是历史 Core ABI 基线；后续 v2.0.0 hard-break 不提供 v1 兼容层。
