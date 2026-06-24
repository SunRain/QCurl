# QCurl Changelog

本文只记录公开 `1.0.0 first stable` 发布线。pre-1.0 日期流水、RC 草稿和旧不兼容变更过程已移入 `docs/internal/pre-1.0-history.md` 索引和内部 raw archive，不再作为当前用户-facing release history。

正式 tag 与 GitHub Release 尚未创建前，本文件使用 `Unreleased` 承接候选变更；发布流程完成后再改为 `## [1.0.0] - <release-date>`。

## [Unreleased]

### Release readiness

- 将当前候选发布线正式收敛为 `QCurl 1.0.0 first stable`：`PROJECT_VERSION=1.0.0`、shared library `SOVERSION=1`、Core ABI baseline `qcurl-core-v1`、默认 shared library `libQCurl.so.1.0.0`。
- 明确 1.0.0 只稳定默认 Core install surface：`find_package(QCurl CONFIG REQUIRED)` / `QCurl::QCurl`、Core headers、CMake package、pkg-config 和 shared/static consumer contract。
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
- Other Extras 保持 Preview / non-Stable ABI；Diagnostics、Middleware Extras、WebSocket 可通过 `COMPONENTS OtherExtras` / `QCurl::OtherExtras` 显式使用，但不进入 1.0.0 Core ABI 承诺。

### Compatibility boundary

- 1.0.0 是 QCurl 首个 Stable ABI baseline；pre-1.0、RC 和历史草稿不构成公开兼容承诺。
- 本发布不提供旧 pre-1.0、RC 或历史草稿 surface 的兼容层、alias、wrapper、ABI shim 或迁移窗口。
- 下游应以 1.0.0 头文件、CMake package、pkg-config 和当前 shared/static 产物重新构建。
- Core cookie public API 不保留 QtNetwork cookie overload / alias / shim / wrapper；下游应迁移到 `QCurl::QCCookie`。
- 未显式配置代理的请求不再继承 libcurl 环境代理；需要代理的调用方必须显式设置 `QCNetworkProxyConfig`。

### ABI evidence

- 当前 release baseline 由当前 `libQCurl.so.1.0.0` 生成：`abi/baseline/qcurl-core-v1.abi.xml`。
- Release 阻断 ABI diff 使用当前 baseline → 当前库的 clean diff：`build/abi/qcurl-core-v1.abidiff.txt`。
- 历史 ABI 对比材料只保留在内部归档，不作为当前 1.0.0 release gate 或用户可见放行证据。

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
- WebSocket、Diagnostics、Middleware Extras 不随 1.0.0 Core 一起宣布 Stable。
- 本条目是 release-candidate changelog；只有完成 tag 与 GitHub Release 后，才能改写为已发布的 `## [1.0.0] - <release-date>`。
