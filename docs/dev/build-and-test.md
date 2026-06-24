# 构建与测试

本文是仓库内唯一的本地构建与回归命令入口。需要调整命令、门禁口径或前置条件时，只修改本文，避免说明分叉。

## 1. 最小构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

默认会按仓库配置启用 tests / examples / benchmarks。

## 2. QtTest 门禁

### 2.1 离线门禁

```bash
python3 scripts/ctest_strict.py --build-dir build
```

适用范围：

- `LABELS=offline`
- 不依赖外网
- 不应通过 `QSKIP` 逃避证据

### 2.2 环境门禁

```bash
python3 scripts/ctest_strict.py --build-dir build --label-regex env
```

适用范围：

- 依赖本地环境但仍应可复现的测试
- 常见前置：`httpbin`、本机端口绑定、`node`
- 若运行环境禁止本地监听端口，`tst_QCNetworkHttp2` / `tst_QCNetworkActorThreadModel` 会以 `QSKIP` 给出明确原因；这类测试不应归入 `offline`

### 2.3 直接跑 ctest

```bash
ctest --test-dir build --output-on-failure
```

注意：直接运行 `ctest` 时，QtTest 的 `QSKIP` 可能被 ctest 视为通过；需要取证式门禁时请优先使用 `ctest_strict.py`。

### 2.4 Public API 安装面门禁

当改动 `QCURL_INSTALL_HEADERS`、install/export 规则、或任何 public header include 依赖时，至少执行：

```bash
ctest --test-dir build -L '^public-api$' --output-on-failure
ctest --test-dir build -L '^public-api-slow$' --output-on-failure
```

说明：

- `public-api`：逐头 self-compile + 规则扫描（禁止 `<curl/...>` / `CURL*` / `curl_*` / Qt private / `*_p.h` / `tuple` / `QCPimpl.h` / `QCURL_DECLARE_*` 泄漏）
- `public-api-slow`：staging install、安装头集合校验、导出合同校验、staging-isolated consumer smoke（含 `QCNetworkReply_p.h` 反向断言）
- 为避免 `public-api` 正则误匹配 `public-api-slow`，文档统一使用带锚点的 label 写法

Linux-only 支持边界如下：

| 组合 | 级别 | 说明 |
|------|------|------|
| `Linux + bundled curl + WebSocket ON` | must | 默认 gate 与发布阻断路径，修改 public/install contract 时必须提供证据 |
| `Linux + system libcurl` | should | 涉及 install/export/public-api 或发行包契约时，建议补跑对应 public-api 验证 |
| `Linux + QCURL_FORCE_DISABLE_WEBSOCKET_SUPPORT=ON` | optional | 仅在变更 WebSocket 安装面、导出合同或 feature gate 时按需补跑 |

若当前 `build/` 是 bundled curl 一致性构建（`QCURL_BUILD_LIBCURL_CONSISTENCY=ON`），上述两条命令直接可用。

如需按 `should` 级别额外验证 system libcurl 构建路径，可使用一套更轻的本地构建目录：

```bash
cmake -S . -B build-public-api-system -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DBUILD_TESTING=ON \
  -DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF

cmake --build build-public-api-system --target QCurl qcurl_public_api_self_compile -j"$(nproc)"
ctest --test-dir build-public-api-system -L '^public-api$' --output-on-failure
ctest --test-dir build-public-api-system -L '^public-api-slow$' --output-on-failure
```

如需按 `optional` 级别额外验证 **WebSocket OFF** 的安装面（模拟 `QCURL_WEBSOCKET_SUPPORT` 关闭时的条件安装/导出合同），可使用：

```bash
cmake -S . -B build-public-api-system-no-ws -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DBUILD_TESTING=ON \
  -DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF \
  -DQCURL_FORCE_DISABLE_WEBSOCKET_SUPPORT=ON

cmake --build build-public-api-system-no-ws --target QCurl qcurl_public_api_self_compile -j"$(nproc)"
ctest --test-dir build-public-api-system-no-ws -L '^public-api$' --output-on-failure
ctest --test-dir build-public-api-system-no-ws -L '^public-api-slow$' --output-on-failure
```

最近一次本地复验：`2026-04-16` 已按上述命令在 `build-public-api-system-no-ws` 路径执行，`public-api` 与 `public-api-slow` 均通过。

Static library 是显式 opt-in 路径，不能用默认 shared gate 代替。涉及 static target、导出依赖、安装包合同或 release ready 结论时，至少补跑：

```bash
cmake -S . -B build-static -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DBUILD_TESTING=ON \
  -DQCURL_BUILD_SHARED_LIBS=OFF \
  -DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF

cmake --build build-static --target QCurl qcurl_public_api_self_compile -j"$(nproc)"
ctest --test-dir build-static -L '^public-api$' --output-on-failure
ctest --test-dir build-static -L '^public-api-slow$' --output-on-failure
```

Static export 只允许 `QCurl::QCurl` 通过 public link interface 暴露 Core 必需的 `CURL::libcurl`，并由 `QCurlConfig.cmake` 同步 `find_dependency(CURL ...)`；`ZLIB::ZLIB` 不属于默认 Core export / `qcurl.pc` 合同。正式打包前仍以 full release gate 的最新 shared/static 结果为准；static gate 通过只证明 Core static library ready，不代表 whole project Stable。

Static public-api-slow 还覆盖 enum-only metatype consumer：fixture 只 include/use `QCNetworkRequestPriority` 和 `QCurl::initialize()`，不依赖 scheduler 符号，用来证明 static consumer 可以显式初始化 canonical Qt 元类型。

## 3. HTTP/2 本地验证

`tst_QCNetworkHttp2` 默认使用仓库内置 node server，不依赖公网。

```bash
ctest --test-dir build -R tst_QCNetworkHttp2 --output-on-failure
```

如需覆盖 base URL，可使用：

- `QCURL_HTTP2_TEST_BASE_URL`
- `QCURL_HTTP2_TEST_HTTP1_BASE_URL`

若当前环境禁止本地端口监听（例如某些沙箱/容器），测试会识别 `listen EPERM` / `permission denied` 并显式跳过。

## 4. 本地 httpbin（供 env/集成测试使用）

启动：

```bash
./tests/qcurl/httpbin/start_httpbin.sh --write-env build/test-env/httpbin.env
source build/test-env/httpbin.env
curl -fsS "${QCURL_HTTPBIN_URL}/get" >/dev/null
```

停止：

```bash
./tests/qcurl/httpbin/stop_httpbin.sh
```

## 5. libcurl_consistency gate

### 5.1 构建前置

```bash
git submodule update --init --recursive

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DBUILD_TESTING=ON \
  -DQCURL_BUILD_LIBCURL_CONSISTENCY=ON

cmake --build build --parallel
```

### 5.2 运行 gate

```bash
python3 tests/libcurl_consistency/run_gate.py --suite p0 --build
python3 tests/libcurl_consistency/run_gate.py --suite p1 --build
python3 tests/libcurl_consistency/run_gate.py --suite all --build
QCURL_LC_EXT=1 python3 tests/libcurl_consistency/run_gate.py --suite all --with-ext --build
```

强制要求 HTTP/3 覆盖时：

```bash
QCURL_LC_EXT=1 QCURL_REQUIRE_HTTP3=1 \
  python3 tests/libcurl_consistency/run_gate.py --suite all --with-ext --build
```

补充说明见：

- `tests/libcurl_consistency/README.md`

## 6. 本地全量自检

开发者做一次本地全量自检时，可按顺序执行：

1. 完成 `cmake` 构建
2. 启动本地 `httpbin`
3. 运行 `ctest --test-dir build --output-on-failure`
4. 按需运行 `tests/libcurl_consistency/run_gate.py --suite all --with-ext --build`

该口径只用于本地自检，不等于正式门禁。

## 6.1 1.0.0 first stable release gate

`scripts/run_release_gate.py` 是本仓库的 no-git 发布门禁入口。它不检查工作区历史，也不调用
`git`；输入只来自当前源码、构建目录、ABI baseline、capability probe 和文档扫描。

```bash
python3 scripts/run_release_gate.py --tier fast --build-dir build --dry-run
python3 scripts/run_release_gate.py --tier strict --build-dir build --dry-run
python3 scripts/run_release_gate.py --tier full --build-dir build --dry-run
```

分层含义：

- `fast`：`contract.json`、`public-api`、`public-api-slow`，用于快速确认安装面和 consumer contract。
- `strict`：在 fast 基础上增加 QtTest skip=fail、deprecated curl API、label matrix 和 skip contract。
- `full`：在 strict 基础上增加完整 CTest、libcurl consistency full gate、ABI diff、capability matrix 和 release metadata scan。只有 full 层可作为 Stable shared library release gate。

## 6.2 ABI baseline / ABI diff gate

Stable shared library 需要真实 ABI baseline，而不是只依赖 public header layout scan。
本仓库使用 `scripts/qcurl_abi_gate.py` 包装 `abidw` / `abidiff`：

```bash
python3 scripts/qcurl_abi_gate.py --library build/src/libQCurl.so.1.0.0 baseline
python3 scripts/qcurl_abi_gate.py --library build/src/libQCurl.so.1.0.0 diff
```

默认 baseline 路径为 `abi/baseline/qcurl-core-v1.abi.xml`，默认 diff 报告路径为
`build/abi/qcurl-core-v1.abidiff.txt`。缺少 `abidw` / `abidiff`、共享库、头目录或调试信息时，
gate fail-closed；发布结论必须把它记录为 release blocker。

Fresh release 口径下，`QCurl 1.0.0` 是首个 Stable ABI baseline。pre-1.0、RC
或历史草稿不构成公开 ABI 兼容承诺；正式 release gate 只使用当前 baseline → 当前库的
clean diff 作为阻断证据。

示例：

```bash
python3 scripts/qcurl_abi_gate.py \
  --library build/src/libQCurl.so.1.0.0 \
  --headers-dir src \
  baseline \
  --output abi/baseline/qcurl-core-v1.abi.xml

python3 scripts/qcurl_abi_gate.py \
  --library build/src/libQCurl.so.1.0.0 \
  --headers-dir src \
  diff \
  --baseline abi/baseline/qcurl-core-v1.abi.xml \
  --report build/abi/qcurl-core-v1.abidiff.txt \
  --current-snapshot build/abi/qcurl-core-v1.current.abi.xml
```

`run_release_gate.py --tier full` 执行正式 release gate，包含当前 ABI baseline 的阻断 diff：

```bash
python3 scripts/run_release_gate.py \
  --tier full \
  --build-dir build \
  --static-build-dir build-static
```

历史 ABI 对比材料只作为内部归档，不进入 fresh release 的公开 gate 示例或放行证据。

## 6.3 libcurl capability matrix

`tests/libcurl_consistency/qcurl_lc_capability_probe` 会生成
`build/libcurl_consistency/reports/capabilities.json`。该文件现在包含 `capabilityMatrix`，
覆盖 build/runtime libcurl version、HTTP/2、HTTP/3、WebSocket、HSTS、Alt-Svc、proxy/SOCKS、
TLS pinned public key 和 raw observability，并为缺失能力标注 Fail / Warn / Preview 归属。

## 7. external_heavy 显式 smoke

`LABELS=external_heavy` 用于真实外部资源的“大体量传输” smoke，默认关闭，不参与 deterministic 门禁。

```bash
QCURL_RUN_EXTERNAL_HEAVY=1 \
  ctest --test-dir build -L external_heavy --output-on-failure
```

补充说明：

- 当前集合仅包含 `tst_LargeFileDownload`
- 默认 URL 可通过 `QCURL_LARGE_FILE_URL` 覆盖
- 使用自定义 URL 时，可用 `QCURL_LARGE_FILE_EXPECTED_BYTES` 指定期望字节数
- 用例会先做 HEAD preflight；若资源 404、DNS 失败、远端超时或镜像站下线，会显式 `QSKIP`

## 8. UCE 统一证据门禁

### 8.1 PR tier

```bash
git submodule update --init --recursive

cmake -S . -B build-uce-pr -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DBUILD_TESTING=ON \
  -DQCURL_BUILD_LIBCURL_CONSISTENCY=ON

cmake --build build-uce-pr --parallel

python3 scripts/run_uce_gate.py --tier pr --build-dir build-uce-pr --run-id "local-pr"
```

关键工件：

- `build-uce-pr/evidence/uce/local-pr/manifest.json`
- `build-uce-pr/evidence/uce/local-pr/policy_violations.json`
- `build-uce-pr/evidence/uce/local-pr.tar.gz`

### 8.2 nightly / soak tier

```bash
python3 scripts/run_uce_gate.py --tier nightly --build-dir build-uce-pr --run-id "local-nightly"
python3 scripts/run_uce_gate.py --tier soak --build-dir build-uce-pr --run-id "local-soak"
```

nightly / soak 会额外启用：

- DCI fixed seed 集
- BP（backpressure）合同
- CTBP / HES 扩展 contract
- `scripts/netproof_strace_gate.py` 的 offline `strace` 证明

### 8.3 sanitizer 放大

```bash
python3 scripts/run_uce_sanitizers.py --profile asan-ubsan-lsan \
  --build-dir build-asan --output-dir build/evidence/uce-sanitizers/asan

python3 scripts/run_uce_sanitizers.py --profile tsan \
  --build-dir build-tsan --output-dir build/evidence/uce-sanitizers/tsan
```

说明：

- `asan-ubsan-lsan` profile 会单独配置 sanitizer build，并调用 `run_uce_gate.py --tier nightly`
- `tsan` profile 只运行线程相关子集：`tst_QCNetworkReply`、`tst_QCNetworkScheduler`、`tst_QCNetworkConnectionPool`

## 9. basic-no-problem 归档门禁（historical acceptance）

当需要生成一套可归档、可复核的最低验收工件时，使用：

```bash
python3 scripts/run_basic_no_problem_gate.py --build-dir build --run-id "<your-run-id>"
```

前置条件：

- `LABELS=env` 所需依赖可用
- `QCURL_BUILD_LIBCURL_CONSISTENCY=ON`
- 允许运行 curl testenv / 本机端口绑定

关键工件位于：

- `build/evidence/basic-no-problem/<run-id>/manifest.json`
- `build/evidence/basic-no-problem/<run-id>.tar.gz`

## 10. 常见失败原因

- 端口绑定受限：curl testenv / httpbin / 本地服务端无法启动
- Docker 权限不足：env 组依赖的本地服务无法拉起
- 未构建 h3-capable `nghttpx`：HTTP/3 gate 无法满足前置条件
- 外部资源漂移：`external_heavy` 依赖的公网资源可能返回 404 / DNS 失败 / 超时，应先确认远端可用性

这些问题应先解决环境，再讨论测试结论。
