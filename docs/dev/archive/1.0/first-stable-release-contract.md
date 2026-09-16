# QCurl 1.0.0 first stable release contract

> 历史归档：以下原文仅反映记录时点，不定义当前 2.0 合同或发布资格；其中“当前”按原始上下文阅读。

本文是 QCurl 当前发布线的唯一 release contract。它定义 `1.0.0 first stable` 的版本身份、稳定范围、非稳定范围和验证证据；旧 `2.x`、`3.0.0`、RC 与不兼容变更历史只作为 pre-1.0 内部参考，不构成当前发布入口。

本文件定义目标发布合同，不单独构成当前 checkout 已通过 release gate 的证明；实际 readiness 以最后一次源码变更之后生成的构建、测试、ABI 与 sanitizer 证据为准。

默认安装包是安全门禁输入。`Core`、`BlockingExtras`、`TestSupport`、`OtherExtras`
四个导出目标必须同时具有 shared/static 安装后 consumer、生命周期测试和 sanitizer
证据；默认 `cmake --install` 的全部文件必须进入逐文件 package inventory。WebSocket
capable 构建必须以默认启用配置通过 WebSocket 与 Pool 测试，force-disable 只能作为
negative capability variant，不能形成 release candidate PASS。

## 发布身份

| 项 | 当前合同 |
| --- | --- |
| 项目版本 | `1.0.0` |
| Shared library SOVERSION | `1` |
| Core ABI baseline | `abi/baseline/qcurl-core-v1.abi.xml` |
| ABI diff 报告 | `build/abi/qcurl-core-v1.abidiff.txt` |
| 默认 Core library | `libQCurl.so.1.0.0` |
| 发布叙事 | `QCurl 1.0.0 first stable` |

`QCurl 1.0.0` 是首个 Stable ABI baseline。pre-1.0、RC 和历史草稿不构成公开兼容承诺；本发布不提供旧 `2.x` / `3.0.0` 兼容层、alias、wrapper、ABI shim 或迁移窗口。下游应以 1.0.0 头文件、CMake package、pkg-config 和 shared/static 产物重新构建。

## 稳定范围

默认 `find_package(QCurl CONFIG REQUIRED)` / `QCurl::QCurl` consumer 只承诺 Core install surface。Core 以 `src/CMakeLists.txt` 的 `QCURL_INSTALL_HEADERS` 加生成头 `QCurlConfig.h` 为准。

Core / Stable 包含：

- `QCNetworkAccessManager` 与异步 `head()` / `get()` / `post()` / `put()` / `patch()` / `deleteResource()` / `sendCustomRequest()` 路径。
- `QCCookie` 与 `QCCookieAsyncResult` 中的 `QCCookieOperationResult` / `QCCookieExportResult`。
- `QCNetworkRequest`、`QCNetworkReply` 和 HTTP method / version / error / priority types。
- Core 请求入口只接受 HTTP/HTTPS；异步和 Blocking 路径的初始请求与重定向协议白名单固定为 `http,https`。
- TLS / proxy / timeout / retry / redirect / transfer / cache policy 配置。
- 重试默认关闭；启用后 GET/HEAD 默认可重试，其他 method 必须显式幂等授权并携带稳定 `Idempotency-Key`，网络错误与 HTTP 状态错误使用同一门禁。
- Proxy 默认不继承 libcurl 环境变量；未显式配置代理或显式设置 `ProxyType::None` 时，QCurl 会主动禁用 `HTTP_PROXY` / `HTTPS_PROXY` 等环境代理来源。
- lane-aware scheduler。
- 显式启用的结构化 Cache lookup API：method、规范化 URL、Vary 请求头和 `cachePartitionKey` 共同决定请求身份；认证请求缺少分区时不存储；`clear()` 返回结构化成功、部分失败或失败结果。
- Multipart builder 与 body helper。
- transfer/download/resumable job types。
- logger、default logger、cancel token、middleware base。
- connection pool config / manager。

## 非默认发布面

| surface | 发布标签 | 当前合同 |
| --- | --- | --- |
| Blocking Extras | `Blocking Extras / Package-shipped, non-default Core` | 可随包发布，提供同步 value-result 工具；需要显式 install component / target，不混入默认 Core Stable。 |
| Test Support | `Test Support / Explicit opt-in` | 仅测试程序使用；不作为生产运行时能力。 |
| Other Extras | `Other Extras / Preview / non-Stable ABI` | Diagnostics、Middleware Extras、WebSocket 等能力保持 Preview；WebSocket 不提供手工 `permessage-deflate`；不进入默认 Core Stable，1.0.0 不承诺二进制兼容。 |

WebSocket、Diagnostics 和 Middleware Extras 不随 1.0.0 Core 一起宣布 Stable。Other Extras 可通过 `OtherExtrasDevelopment` 与 `QCurl::OtherExtras` 显式使用，但 1.0.0 release contract 只保证源码级 opt-in consumer gate，不提供 ABI baseline、ABI diff gate 或二进制兼容承诺。它们需要独立 install surface、consumer smoke、ABI 检查、压力/延迟和外部依赖证据后，才能进入后续稳定合同。

## CMake / pkg-config 合同

- 默认 Core consumer：`find_package(QCurl CONFIG REQUIRED)` 并链接 `QCurl::QCurl`。
- Blocking Extras consumer：`find_package(QCurl CONFIG REQUIRED COMPONENTS BlockingExtras)` 并链接 `QCurl::BlockingExtras`。
- Test Support consumer：`COMPONENTS TestSupport` 并链接 `QCurl::TestSupport`。
- Other Extras consumer：`COMPONENTS OtherExtras` 并链接 `QCurl::OtherExtras`。
- `qcurl.pc` 只描述默认 Core；Core `.pc` 不暴露 zlib。
- `qcurl-other-extras.pc` 描述 Other Extras opt-in 目标；static 链接时暴露 zlib。

## ABI 与 release gate

正式发布前必须同时具备：

- `PROJECT_VERSION=1.0.0` 与 `SOVERSION=1` 的构建产物。
- 由当前 `build/src/libQCurl.so.1.0.0` 生成、先保存在 `build/abi/` 的诊断 ABI
  candidate 与当前快照；诊断命令不得直接写入 `abi/baseline/`。
- full gate 生成的 Core / Other Extras Linux dynamic-symbol allowlist 报告、当前 ABI
  diff 报告和 machine QA manifest。
- 经显式 `qcurl_abi_gate.py promote` 重放验证后写入的受控 baseline；promotion 只能由
  同一 Linux、完整 commit、固定 toolchain、clean worktree 的候选 manifest 授权。
- baseline-only commit 完成后重新运行的 full gate；只有重新验证通过才允许 tag/release。
- shared / static `public-api` 与 `public-api-slow` consumer gate。
- `scripts/run_release_gate.py --tier full --build-dir build --static-build-dir build-static` 通过。
- release metadata scan 阻止旧版 QCurl 发布身份、旧 ABI baseline 名称和旧 shared library 名称回流到当前公开文档；历史材料只允许保留在 `docs/internal/`。
- `git diff --check` 通过。

只通过 public header layout scan 不等于 Stable ABI ready。Fresh release 必须先生成诊断
candidate，再由显式 promotion 写入受控 baseline，并用同一当前库通过 clean ABI diff。
历史 ABI 对比材料只保留在 `docs/internal/archived-release/`，不作为当前 release 阻断项或放行证据。
