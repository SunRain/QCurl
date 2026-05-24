# QCurl Core first Stable shared/static readiness report

> 日期：2026-05-21
> 范围：`.helloagents/plans/202605211133_core_first_stable_shared_static_release_blocker_cleanup/` 执行收口。

## 结论

当前工作区证据支持发布 **QCurl Core first Stable shared/static release**。

发布承诺范围必须保持窄口径：

- Core 默认安装面进入 first Stable 承诺，同时覆盖 shared library 与 static library。
- Blocking Extras 可作为显式 opt-in 稳定组件随包发布，但不属于默认 Core。
- Test Support 可作为显式 opt-in 测试组件随包发布，不属于生产 Core。
- WebSocket 保持 `Other Extras` 安装层，稳定性标签为 `Preview`。
- Diagnostics 保持 `Other Extras`，不进入 Core first Stable 承诺。
- 仍不宣布 whole project Stable。

## 关键变更结论

| 事项 | 当前状态 | 证据入口 |
| --- | --- | --- |
| Core Reply 同步合同迁出 | `QCNetworkReply` public API 已收敛为 Qt 风格 async-only；旧同步执行枚举、同步 callback typedef 与 setter 已从 Core public header 移除 | `src/QCNetworkReply.h`、`src/QCNetworkReply_p.h`、`src/private/QCNetworkReplyExecution.cpp` |
| Blocking Extras 同步入口 | 同步 public 能力只保留在 `QCBlockingNetworkClient` / `QCBlockingNetworkResult` value-result API；progress callback 保持 Blocking Extras opt-in，并通过 `QCurl::BlockingExtras` target 消费 | `src/QCBlockingNetworkClient.h`、`src/private/QCBlockingCurlAdapter.cpp`、`tests/qcurl/tst_QCBlockingNetworkClient.cpp` |
| Core 负向 guard | public-api gate 已新增 hard-break guard，禁止默认 Core 面重新暴露 reply 阻塞执行合同、callback setter 与旧 manager `sendHead()` / `sendGet()` / `sendPost()` / `sendPut()` / `sendPatch()` 入口 | `tests/public_api/hard_break_guards.py`、`tests/public_api/CMakeLists.txt` |
| static pkg-config 合同 | `qcurl.pc` 只描述默认 Core；普通 shared `pkg-config --libs qcurl` 不暴露 libcurl / zlib，static 输出只直接声明 Core 的 libcurl 入口，zlib 不再写入 Core `.pc` | `qcurl.pc.in`、`tests/public_api/pkg_config_contracts.py` |
| shared/static release gate | `scripts/run_release_gate.py --tier full` 已覆盖 shared public-api、static configure/build/public-api/public-api-slow、ABI diff、capability matrix 与 metadata scan | `scripts/run_release_gate.py` |
| ABI baseline | Core ABI baseline 已刷新，当前 ABI diff 通过 | `abi/baseline/qcurl-core-v3.abi.xml`、`build/abi/qcurl-core-v3.abidiff.txt` |
| readiness 口径 | 文档明确 Core shared/static first Stable；WebSocket / Diagnostics / whole project 不进入 Stable 承诺 | 本报告、`README.md`、`docs/arch/rc-maturity-review.md` |

## 最终验证证据

本轮以真实 gate 输出作为结论依据：

| 验证项 | 结果 |
| --- | --- |
| `pytest tests/public_api/test_run_public_api_checks.py` | 15 passed |
| `cmake --build build --target QCurl qcurl_public_api_self_compile tst_LibcurlConsistency -j6` | passed |
| `python3 tests/public_api/run_public_api_checks.py hard-break-guards --repo-root .` | passed |
| `ctest --test-dir build -L '^public-api$' --output-on-failure` | 4/4 passed |
| `ctest --test-dir build -L '^public-api-slow$' --output-on-failure` | 14/14 passed |
| `ctest --test-dir build-static -L '^public-api-slow$' --output-on-failure` | 14/14 passed |
| `python3 tests/public_api/run_public_api_checks.py check-pkg-config-contract --stage-dir build-static/public_api/stage` | passed |
| `python3 scripts/qcurl_abi_gate.py --library build/src/libQCurl.so.3.0.0 --headers-dir src diff` | passed |
| `python3 tests/libcurl_consistency/run_gate.py --suite p1 --qcurl-build build` | 61 passed |
| `env QCURL_HTTPBIN_URL=http://127.0.0.1:32769 python3 scripts/run_release_gate.py --tier full --build-dir build --static-build-dir build-static` | full gate passed |
| removed API residual scan | 只命中允许的 Blocking Extras progress callback public API |
| `git diff --check` | passed |

`--tier full` 的最终输出覆盖了以下关键阶段：

- `shared_public_api`
- `shared_public_api_slow`
- `static_configure`
- `static_build`
- `static_public_api`
- `static_public_api_slow`
- `strict_qttest`
- `deprecated_curl_api_guard`
- `label_matrix_guard`
- `skip_contract_guard`
- `full_ctest`（69/69 passed）
- `libcurl_consistency_full`（102 passed）
- `abi_diff`
- `capability_matrix_build`
- `capability_matrix_probe`
- `metadata_scan`

## 非 Core 风险保留

| 风险 | 归属 | 后续提升条件 |
| --- | --- | --- |
| `QCWebSocketCompressionConfig` / `QCWebSocketReconnectPolicy` 公开字段 | WebSocket Preview ABI 风险 | WebSocket 提升 Stable 前迁移为 accessor / shared-data 或冻结 ABI 策略 |
| `QCWebSocket.cpp` 建连 `curl_easy_perform()` 与 polling fallback | WebSocket Preview 运行语义风险 | 独立异步语义、latency/stress gate 和 consumer smoke |
| Diagnostics 外部命令和 details schema | Other Extras 风险 | 权限模型、schema freeze 和平台差异合同 |

## 发布判定

当前结论是 `Core shared/static first Stable ready`。该结论只适用于本轮工作区与上述验证证据；正式打包前仍应重新运行 `scripts/run_release_gate.py --tier full --build-dir build --static-build-dir build-static` 并以最新输出为准。
