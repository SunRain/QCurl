# QCurl 1.0.0 first stable readiness report

> 历史归档：以下原文仅反映记录时点，不定义当前 2.0 合同或发布资格；其中“当前”按原始上下文阅读。

## 当前判定

**当前 dirty checkout：本地 release gate PASS。** 该结论只适用于 2026-07-31 最后一次源码变更之后的本地证据；它证明当前 Core shared/static 候选满足本仓库 release gate，但不等同于已绑定 commit/tag、已执行 `git push`、已创建 GitHub Release 或已发布产物。

QCurl 1.0.0 first stable 的目标 release readiness 只针对 Core shared/static。Blocking Extras 可随包发布但不属于默认 Core Stable；Test Support 为显式 opt-in；WebSocket、Diagnostics 和 Other Extras 保持 Preview。

本文定义 readiness 证据口径，并记录可追溯 snapshot。具体 GitHub Release 发布动作见 `docs/dev/release-procedure.md`。

## 必要证据

| 证据 | 当前目标 |
| --- | --- |
| 构建版本 | `PROJECT_VERSION=1.0.0` |
| SONAME | `libQCurl.so.1` / `SOVERSION=1` |
| ABI baseline | `abi/baseline/qcurl-core-v1.abi.xml`，由当前 `libQCurl.so.1.0.0` 生成 |
| ABI diff | `build/abi/qcurl-core-v1.abidiff.txt` |
| shared public API | `ctest --test-dir build -L '^public-api$' --output-on-failure` |
| shared public API slow | `ctest --test-dir build -L '^public-api-slow$' --output-on-failure` |
| static public API | static build tree 中同名 public-api gate |
| surface / Doxygen | `surface_manifest.json`、manifest-driven input、`doxygen Doxyfile` |
| protocol / retry / cache | HTTP/HTTPS allowlist、统一 method/idempotency gate、structured cache/auth isolation 具名测试 |
| full release gate | `python3 scripts/run_release_gate.py --tier full --build-dir build --static-build-dir build-static` |
| metadata scan | `python3 scripts/run_release_gate.py --scan-metadata --build-dir build` |
| whitespace gate | `git diff --check` |

## Release blocker policy

以下任一项出现即阻塞发布：

- 对外发布面仍出现旧 QCurl 发布身份。
- ABI baseline 不是从当前 `libQCurl.so.1.0.0` 真实生成。
- WebSocket 或 Diagnostics 被写成 Core Stable。
- Core 入口仍允许非 HTTP/HTTPS scheme，或初始/重定向协议白名单缺少 HTTP-only fail-closed 证据。
- 重试可在未获显式幂等授权时重放非 GET/HEAD，或缓存未证明 method/status、Vary 和认证分区隔离。
- WebSocket Preview 文档承诺手工压缩、阻塞握手或独立 worker transport。
- static Core consumer 或 shared Core consumer 任一 gate 失败。
- metadata scan 无法区分外部协议/依赖版本和 QCurl 发布身份。
- Release asset 缺少 checksum、ABI report 或必要 gate 证据。

## Release evidence snapshots

Snapshot 只证明对应 commit / tag / CI run 的状态，不代表未来任意 checkout 自动保持通过。下列历史 snapshot 不作为当前 dirty checkout 的放行证据。

| 日期 | Commit / Tag | 证据来源 | 结果 | 备注 |
| --- | --- | --- | --- | --- |
| 2026-07-31 | dirty checkout；未绑定公开 commit/tag | GCC/Clang、shared/static public API、ABI、libcurl consistency、ASan/UBSan/LSan；命令见下方摘要 | PASS | 仅本地 Core release readiness；WebSocket/Diagnostics/Other Extras 仍为 Preview，未暂存、提交、推送、打 tag 或发布。 |
| 2026-05-24 | 未绑定公开 tag；本地 identity-reset checkout | 本地 `build` / `build-static`；命令见下方摘要 | PASS | 未执行 git tag、GitHub Release、远程发布或 `git push`。 |

### 2026-07-31 local architecture-remediation snapshot

命令与结果摘要：

- GCC full release gate：shared/static public API fast 4/4、slow 19/19，strict offline 33/33、full CTest 78/78，libcurl consistency 101/101，ABI、capability、metadata 全部通过。
- Clang：全量构建、strict offline 33/33、full CTest 78/78 全部通过。
- ASan/UBSan/LSan：`build-asan-ubsan/reports/uce/20260730T171334Z-asan-ubsan-lsan/manifest.json` 为 `pass`，24 个 result、12 个 contract、44 个 artifact、0 个 policy violation。
- Public surface：manifest-driven Doxygen 输入 46 个 public header，`doxygen Doxyfile` 通过；Core ABI baseline 到当前库 clean diff。
- 卫生：tracked changed-lines 与 35 个 untracked C/C++ 文件格式检查通过，metadata scan 与 `git diff --check HEAD` 通过。

该 snapshot 只证明当前工作树的本地 release readiness。正式发布仍必须先形成可追溯 commit，再按 `docs/dev/release-procedure.md` 重跑绑定 commit/tag 的 CI gate 并生成 release assets 与 checksum。

### 2026-05-24 local identity-reset snapshot

命令摘要：

- `python3 scripts/check_release_contract.py` 输出 `version=1.0.0, soversion=1`。
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON`，summary 显示 `Version: 1.0.0`。
- `cmake --build build --parallel`。
- 当时生成了首个 baseline；当前流程已禁止诊断命令直接写入 `abi/baseline/`，后续更新必须按 `docs/dev/release-procedure.md` 显式 promotion。
- `python3 scripts/qcurl_abi_gate.py --library build/src/libQCurl.so.1.0.0 --headers-dir src diff --baseline abi/baseline/qcurl-core-v1.abi.xml --report build/abi/qcurl-core-v1.abidiff.txt`。
- `env QCURL_HTTPBIN_URL=http://127.0.0.1:32770 python3 scripts/run_release_gate.py --tier full --build-dir build --static-build-dir build-static`。
- `python3 scripts/run_release_gate.py --scan-metadata --build-dir build`。
- `git diff --check`。

该 snapshot 是维护者证据，不等同于 GitHub Release 已发布。正式发布必须重新按 `docs/dev/release-procedure.md` 绑定 commit / tag / CI artifact，并上传 release assets、checksums 和必要报告。
