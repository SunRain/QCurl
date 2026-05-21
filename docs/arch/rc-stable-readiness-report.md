# QCurl RC / Stable readiness report

> 日期：2026-05-18
> 范围：`.helloagents/plans/202605182025_rc_stable_shared_library_readiness/` 的执行收口。

## 结论

Core public surface reset 可以继续作为 `3.0.0-rc.1` 候选推进；整个项目仍不能宣布 Stable shared library ready。

当前已完成 release readiness 的基础收口：Core / Extras 文档口径、`QCCookieAsyncResult.h` Core 覆盖、CPack summary、README 性能承诺、no-git ABI gate 入口、no-git release gate 入口和 capability matrix 字段。

2026-05-19 补充收口：

- Blocking Extras 第一版同步 value-result 合同已落地到源码和 gate：`maxInMemoryBodyBytes` 受限内存响应体、`BodyTooLarge`、`downloadToDevice()`、`OutputDeviceError`、输入设备错误、`ReplayNotSupported`、`bytesReceived()` 与 `diagnosticCurlCode()`。
- Blocking Extras 的本地 QtTest 与 public-api opt-in consumer smoke 已覆盖 bounded body / large download 合同。
- static library 已提供 `QCURL_BUILD_STATIC` opt-in 构建路径，并补齐 static public dependency export 的 `CURL::libcurl` / `ZLIB::ZLIB` 规则。
- static public-api / public-api-slow gate 通过前，whole project static library ready 仍为 false。

- Middleware 已完成 Core base / Other Extras / internal 三层拆分：`QCNetworkMiddleware.h` 默认 Core 只保留 base contract，`QCNetworkMiddlewareExtras.h` 通过 Other Extras opt-in 暴露稳定通用 middleware，策略型 middleware 移入 private internal。
- `QCNetworkRequest.*` 已把认证、重定向和传输配置拆为自然配置族：`QCNetworkRequestConfig.h`、`QCNetworkRedirectConfig`、`QCNetworkTransferConfig`；DNS / DoH / connect-to / resolve override / Happy Eyeballs / 本地端口绑定用 `QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API` 隔离，不进入默认 Core public contract。

Stable shared library 发布仍受以下条件约束：

- 必须生成并冻结 `abi/baseline/qcurl-core-v3.abi.xml`。
- 必须通过 `scripts/qcurl_abi_gate.py diff`。
- 必须通过 `scripts/run_release_gate.py --tier full`。
- 必须归档 `build/libcurl_consistency/reports/capabilities.json`。
- WebSocket / Diagnostics 仍需按独立 checklist 提升，不能跟随 Core 默认 Stable。

## 已收口事项

| 事项 | 当前状态 | 证据入口 |
| --- | --- | --- |
| `QCCookieAsyncResult.h` Core 一致性 | 已补齐 README、架构文档和 consumer fixture validator | `README.md`、`docs/arch/public-header-boundary.md`、`tests/public_api/consumer_smoke/main.cpp` |
| release metadata | 已避免 CPack summary 把 WebSocket 写成默认能力 | `CMakeLists.txt` |
| README 性能承诺 | 已移除固定延迟数字，改为性能回归入口 | `README.md` |
| ABI gate | 已新增 no-git baseline / diff wrapper | `scripts/qcurl_abi_gate.py` |
| release gate | 已新增 fast / strict / full 分层入口 | `scripts/run_release_gate.py` |
| capability matrix | 已扩展 capability probe 的 `capabilityMatrix` | `tests/libcurl_consistency/qcurl_lc_capability_probe.cpp` |
| migration / release notes | 已新增 hard-break migration guide 与 RC notes draft | `docs/arch/3.0-hard-break-migration-guide.md`、`docs/arch/3.0-rc-release-notes.md` |
| Blocking Extras bounded body / download | 已实现第一版 value-result 同步合同，覆盖 BodyTooLarge、downloadToDevice、bytesReceived 和 opt-in consumer smoke | `src/QCBlockingNetworkClient.*`、`src/QCBlockingNetworkResult.*`、`tests/qcurl/tst_QCBlockingNetworkClient.cpp`、`tests/public_api/consumer_blocking_extras_smoke/main.cpp` |
| static opt-in 构建路径 | 已新增 `QCURL_BUILD_STATIC`、static export dependency 规则和 static public-api gate 入口；通过前不得声明 static ready | `CMakeLists.txt`、`src/CMakeLists.txt`、`cmake/QCurlConfig.cmake.in`、`docs/dev/build-and-test.md` |
| Middleware / Request 配置族收敛 | 已完成 Core base / Other Extras / internal 拆分，Request 配置族收敛到 Redirect / Transfer，Advanced 网络路径 API 默认隔离 | `src/QCNetworkMiddleware.h`、`src/QCNetworkMiddlewareExtras.h`、`src/private/QCNetworkMiddlewareInternal_p.h`、`src/QCNetworkRequestConfig.h`、`src/QCNetworkRequest.h`、`tests/public_api/consumer_smoke/main.cpp` |

## 当前风险清单

| 风险 | 归属 | Stable 前要求 |
| --- | --- | --- |
| `QCNetworkReply.h` 仍有 `ExecutionMode::Sync` 与 callback typedef / setter 概念 | Core public header 风险 | 迁出、隐藏，或建立明确非阻塞 Core 合同与 negative consumer 证据 |
| `src/private/QCNetworkReplyCurlOptions.cpp`、`src/private/QCNetworkReplyExecution.cpp` 等大职责文件 | 维护风险 | 拆分或记录为 Stable 前 blocker |
| `QCWebSocketCompressionConfig` / `QCWebSocketReconnectPolicy` 公开字段 | WebSocket Preview ABI 风险 | WebSocket 提升 Stable 前迁移为 accessor / shared-data 或冻结 ABI 策略 |
| `QCWebSocket.cpp` 建连 `curl_easy_perform()` 与 polling fallback | WebSocket Preview 运行语义风险 | 独立异步语义、latency/stress gate 和 consumer smoke |
| Diagnostics 外部命令和 details schema | Other Extras 风险 | 权限模型、schema freeze 和平台差异合同 |
| static release proof 尚未归档 full 证据 | static 发布风险 | static `public-api` / `public-api-slow`、static consumer smoke、pkg-config 与 full release gate 证据齐备前保持 static ready=false |

## 推荐执行顺序

1. 在当前构建产物上生成 ABI baseline。
2. 跑 `scripts/run_release_gate.py --tier fast` 和 `--tier strict`。
3. 在 release 候选构建上跑 `--tier full`。
4. 若 full gate 或 ABI gate 失败，保持 `Stable ready=false`，并把失败项写入 release blocker。
