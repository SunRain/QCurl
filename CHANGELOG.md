# QCurl Changelog

本文记录当前 `2.0.0` hard-break 候选和已发布的 `1.0.0` 历史版本。pre-1.0 日期流水、RC 草稿和旧不兼容变更过程已移入 `docs/internal/pre-1.0-history.md` 索引和内部 raw archive，不再作为当前用户-facing release history。

`v1.0.0` 已发布；`v2.0.0` 尚未创建 tag、推送或发布，因此当前变更继续放在 `Unreleased`。

## [Unreleased]

### Release readiness

- 将当前候选发布线切换为 `QCurl 2.0.0`：`PROJECT_VERSION=2.0.0`、shared library `SOVERSION=2`、Core ABI baseline `qcurl-core-v2`、默认 shared library `libQCurl.so.2.0.0`。
- 明确 2.0.0 只稳定默认 Core install surface：`find_package(QCurl CONFIG REQUIRED)` / `QCurl::QCurl`、Core headers、CMake package、pkg-config 和 shared/static consumer contract。
- Full release gate 以 `scripts/run_release_gate.py --tier full --build-dir build --static-build-dir build-static` 为入口，覆盖 shared/static public API、strict QtTest、完整 CTest、QCurl/libcurl observable consistency、capability matrix、metadata scan 和当前 ABI baseline clean diff。

### Stable Core

- 稳定 `QCNetworkAccessManager`、`QCNetworkRequest`、`QCNetworkReply` 及异步 HTTP `head()` / `get()` / `post()` / `put()` / `patch()` / `deleteResource()` / `sendCustomRequest()` 路径。
- 稳定 HTTP method / version / error / priority types、TLS、proxy、timeout、retry、redirect、transfer、cache policy、connection pool config / manager、middleware base、logger、default logger 和 cancel token。
- 稳定 proxy 的 fail-closed 默认行为：未显式配置代理或显式设置 `QCNetworkProxyConfig::ProxyType::None` 时，QCurl 会禁止 libcurl 从 `HTTP_PROXY` / `HTTPS_PROXY` 等环境变量隐式继承代理。
- 稳定 lane-aware scheduler、Cache lookup、Multipart builder、body helper、transfer/download/resumable job types。
- 稳定 Core cookie API：`QCurl::QCCookie`、`QCCookieOperationResult`、`QCCookieExportResult`；默认 Core consumer 不需要 QtNetwork cookie 类型或 `Qt6Network` 链接依赖。

### Package-shipped non-default surfaces

- Blocking Extras 随包发布，但不混入默认 Core Stable；consumer 需要显式 `COMPONENTS BlockingExtras` 并链接 `QCurl::BlockingExtras`。
- Test Support 仅作为显式 opt-in 测试 surface；consumer 需要显式 `COMPONENTS TestSupport` 并链接 `QCurl::TestSupport`。
- Other Extras 保持 Preview / non-Stable ABI；Diagnostics、Middleware Extras、WebSocket 可通过 `COMPONENTS OtherExtras` / `QCurl::OtherExtras` 显式使用，但不进入 2.0.0 Core ABI 承诺。

### Compatibility boundary

- 1.0.0 是已发布的历史 ABI baseline；2.0.0 是允许公开 hard-break 的新 ABI 线。
- 本发布不提供 v1 surface 的兼容层、alias、wrapper、ABI shim 或迁移窗口。
- 下游应以 2.0.0 头文件、CMake package、pkg-config 和当前 shared/static 产物重新构建。
- `QCNetworkRequest(const QUrl &)` 现在是 `explicit`：依赖 `QUrl` 隐式转换的源码必须改为 `QCNetworkRequest{url}`；该变更不改变构造函数符号或对象布局，不提供兼容重载或迁移开关。
- Core cookie public API 不保留 QtNetwork cookie overload / alias / shim / wrapper；下游应迁移到 `QCurl::QCCookie`。
- 未显式配置代理的请求不再继承 libcurl 环境代理；需要代理的调用方必须显式设置 `QCNetworkProxyConfig`。

### ABI evidence

- 当前 release baseline 将由 `libQCurl.so.2.0.0` 通过受控 promotion 生成：`abi/baseline/qcurl-core-v2.abi.xml`。
- candidate 阶段使用 `qcurl-core-v1.abi.xml` 生成 v1-to-v2 hard-break report；final 阶段只对 v2 baseline 做 clean diff。
- v1 historical ABI 只作为 old-baseline 输入，不作为当前 2.0.0 clean-diff 替代品。

### Verification

- `python3 -m py_compile scripts/qcurl_abi_gate.py scripts/run_release_gate.py`
- `pytest -q tests/test_release_gate_unit.py`
- `python3 scripts/qcurl_abi_gate.py diff`
- `python3 scripts/run_release_gate.py --tier full --build-dir build --static-build-dir build-static --dry-run`
- `python3 scripts/run_release_gate.py --scan-metadata --build-dir build`
- `git diff --check`
- `env QCURL_HTTPBIN_URL=http://127.0.0.1:32768 python3 scripts/run_release_gate.py --tier full --build-dir build --static-build-dir build-static`

### Not included in this release claim

- Tag、GitHub Release、release assets、checksums、SBOM、signature 和 provenance 尚未创建或上传。
- WebSocket、Diagnostics、Middleware Extras 不随 2.0.0 Core 一起宣布 Stable。
- 本条目是 2.0.0 release-candidate changelog；只有完成后续 tag 与 GitHub Release 授权，才能改写为已发布条目。

## [1.0.0] - 2026-06-24

v1.0.0 已发布。该版本是历史 Core ABI 基线；后续 v2.0.0 hard-break 不提供 v1 兼容层。
