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

发布合同变更还必须同步验证机器可读 surface、默认 ABI 模式和 API 文档输入：

```bash
python3 scripts/run_release_gate.py --scan-metadata \
  --release-shared-build-dir build-release-shared
python3 scripts/generate_doxygen_input_from_surface_manifest.py \
  --manifest tests/public_api/surface_manifest.json \
  --output build/doxygen/qcurl_api_input.doxy --check
doxygen Doxyfile
python3 scripts/run_release_gate.py --tier full --abi-mode none \
  --release-shared-build-dir build-release-shared \
  --release-static-build-dir build-release-static \
  --test-shared-gcc-build-dir build-test-shared-gcc \
  --test-shared-clang-build-dir build-test-shared-clang \
  --asan-ubsan-lsan-build-dir build-asan-ubsan-lsan \
  --tsan-build-dir build-tsan --dry-run
```

2.0 的 public surface 是源码兼容合同。该验证不建立二进制兼容承诺，下游仍须在每次
QCurl 更新后重新编译和链接。

`QCNetworkCacheRequestKey.h` 属于 Core install surface；runtime registry、multi transfer record 和 handle
accessor 属于 internal/private，不能加入 surface manifest 或 Doxygen public input。`QCurlRuntimeState`
作为 `QCurlRuntime.h` 中的 public 状态枚举随应用级关闭控制器进入 Core install surface。

### 2.5 白盒测试 companion

`BUILD_TESTING=ON` 时，正式 `QCurl`、`QCurlOtherExtras` 和静态 `QCurlTestSupport` 仍保持
发行语义，不定义 `QCURL_ENABLE_TEST_HOOKS`；`QCurlBlockingExtras` 是无编译产物的
`INTERFACE` target。需要故障注入或私有实现覆盖的 QtTest 与一致性测试按所有权链接
`QCurlTestInternals`（含 Blocking 实现）、`QCurlOtherExtrasTestInternals` 或
`QCurlTestSupportTestInternals`。

这些 companion 只用于测试构建，使用 `EXCLUDE_FROM_ALL`，不安装、不导出，也不进入
公共 ABI。`BUILD_TESTING=OFF` 的发行构建不生成 companion。

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

Static library 是显式 opt-in 路径，但 static 与 `BUILD_TESTING=ON` 的组合不受支持。
`QCURL_STATIC_TESTING_UNSUPPORTED：静态构建不支持测试` 是 configure 阶段的固定诊断；只运行
负向 configure 测试，不进入 build 或链接阶段。涉及 static target、导出依赖、安装包合同或
release ready 结论时，使用 `BUILD_TESTING=OFF` 的 release-static 树：

```bash
cmake -S . -B build-release-static -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DBUILD_TESTING=OFF \
  -DQCURL_BUILD_SHARED_LIBS=OFF \
  -DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF

cmake --build build-release-static \
  --target QCurl QCurlOtherExtras QCurlTestSupport -j"$(nproc)"
python3 scripts/release_package_evidence.py \
  --build-dir build-release-static --linkage static \
  --contract tests/public_api/package_gate_manifest.json \
  --surface-manifest tests/public_api/surface_manifest.json \
  --install-report build-release-static/evidence/package/static-install-consumer.json \
  --lifecycle-report build-release-static/evidence/lifecycle/static.xml
```

Static export 只允许 `QCurl::QCurl` 通过 public link interface 暴露 Core 必需的 `CURL::libcurl`，并由 `QCurlConfig.cmake` 同步 `find_dependency(CURL ...)`；`ZLIB::ZLIB` 不属于默认 Core export / `qcurl.pc` 合同。正式打包前仍以 full release gate 的最新 shared/static 结果为准；static gate 通过只证明 Core static library ready，不代表 whole project 发布就绪。

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

## 6.1 2.0.0 release-candidate gate

`scripts/run_release_gate.py` 是本仓库的 no-git 发布门禁入口。它不检查工作区历史，也不调用
`git`；输入只来自当前源码、六棵显式构建树、capability probe 和文档扫描。

```bash
python3 scripts/run_release_gate.py --tier fast \
  --release-shared-build-dir build-release-shared --dry-run
python3 scripts/run_release_gate.py --tier strict \
  --release-shared-build-dir build-release-shared \
  --release-static-build-dir build-release-static \
  --test-shared-gcc-build-dir build-test-shared-gcc --dry-run
python3 scripts/run_release_gate.py --tier full \
  --release-shared-build-dir build-release-shared \
  --release-static-build-dir build-release-static \
  --test-shared-gcc-build-dir build-test-shared-gcc \
  --test-shared-clang-build-dir build-test-shared-clang \
  --asan-ubsan-lsan-build-dir build-asan-ubsan-lsan \
  --tsan-build-dir build-tsan --dry-run
```

`full` 固定要求六棵树：`release-shared`、`release-static`、`test-shared-gcc`、
`test-shared-clang`、`asan-ubsan-lsan` 和 `tsan`。门禁会在执行步骤前重新检查每棵树的
真实路径、编译器、`BUILD_TESTING`、`QCURL_BUILD_SHARED_LIBS` 和 sanitizer capability；
缺参或 capability 漂移直接失败。OFF 树的安装、consumer 和生命周期证据由
`release_package_evidence.py` 生成，不调用 OFF 树的 CTest。

分层含义：

- `fast`：`contract.json`、`public-api`、`public-api-slow`，用于快速确认安装面和 consumer contract。
- `strict`：在 fast 基础上增加 QtTest skip=fail、deprecated curl API、label matrix 和 skip contract。
- `full`：在 strict 基础上增加完整 CTest、libcurl consistency full gate、动态符号 allowlist、capability matrix、sanitizer、Doxygen 和 release metadata scan。full 层验证 2.0 的源码兼容、下游需重编译合同，不证明稳定 ABI。

## 6.2 2.0 非稳定 ABI 合同与诊断工具

QCurl 2.0 的正式发布门禁使用 `--abi-mode none`。该模式是默认值，不生成或读取 v2 ABI
baseline，也不执行 ABI compatibility diff：

```bash
python3 scripts/run_release_gate.py \
  --tier full \
  --abi-mode none \
  --release-shared-build-dir build-release-shared \
  --release-static-build-dir build-release-static \
  --test-shared-gcc-build-dir build-test-shared-gcc \
  --test-shared-clang-build-dir build-test-shared-clang \
  --asan-ubsan-lsan-build-dir build-asan-ubsan-lsan \
  --tsan-build-dir build-tsan
```

该合同保证文档化 Core surface 在 2.x 内源码兼容，但不保证二进制兼容。下游每次升级
QCurl 都必须重新编译和链接。`SOVERSION 2` 只表示当前装载命名空间，不能用来证明两个
QCurl 2.x 构建可直接替换。

full gate 运行 Core 与 Other Extras 的动态符号 allowlist，其中 Core allowlist 同时覆盖
嵌入的 Blocking Extras 公共符号。该门禁检查 private/internal 符号泄漏，不检查跨版本
二进制兼容。Test Support 为静态开发库，不运行动态符号门禁。缺少
`abi/baseline/qcurl-core-v2.abi.xml` 不是 2.0 release blocker。

`scripts/qcurl_abi_gate.py`、`--abi-mode current` 和 `--abi-mode promotion-candidate` 仍保留为
显式诊断工具，供未来稳定 ABI 项目使用。例如，维护者可以生成不受控的诊断 snapshot：

```bash
python3 scripts/qcurl_abi_gate.py \
  --library build/src/libQCurl.so.2.0.0 \
  --headers-dir src \
  baseline
```

诊断 snapshot 不能进入 `abi/baseline/`，不能作为 2.0 发布放行或阻断证据，也不能改写公开
兼容性声明。稳定 ABI 合同、支持平台矩阵、baseline 存储、promotion 与阻断 diff 的恢复条件见
`docs/roadmap/stable-abi-contract-and-baseline.md`。

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
```

`asan-ubsan-lsan` 沿用独立构建与 `run_uce_gate.py --tier nightly`。普通构建与 ASan 构建
不得指向下面的检测专用 Qt；它也不是 QCurl 消费者的新依赖。

#### 检测专用 Qt 与 TSan 构建

只给 QCurl 加 `-fsanitize=thread` 不足以观察 Qt 的同步路径。使用与普通构建同版本的
官方 qtbase，以同一个 Clang 构建 Qt 和 QCurl；不修改 `/usr`、不提高产品最低 Qt 版本。
本地已准备的组合为 Clang 22.1.8、Qt 6.11.2、系统 libcurl 8.22.0。换版本后必须重新校准，
不能沿用旧报告。下面使用宿主 Qt 的准确版本，不取浮动分支：

```bash
set -euo pipefail
ROOT="$PWD"
QT_VERSION="$(qmake6 -query QT_VERSION)"
QT_SERIES="${QT_VERSION%.*}"
DEPS="$ROOT/build/tsan-deps"
QT_TSAN_PREFIX="$DEPS/qt-$QT_VERSION"
SOURCE="$DEPS/src/qtbase-$QT_VERSION"
ARCHIVE="qtbase-everywhere-src-$QT_VERSION.tar.xz"
URL="https://download.qt.io/archive/qt/$QT_SERIES/$QT_VERSION/submodules/$ARCHIVE"
mkdir -p "$SOURCE" "$DEPS/qtbase-build"
(
  cd "$DEPS/src"
  curl -fL "$URL" -o "$ARCHIVE"
  curl -fL "$URL.sha256" -o "$ARCHIVE.sha256"
  sha256sum --check "$ARCHIVE.sha256"
  tar -xf "$ARCHIVE" --strip-components=1 -C "$SOURCE"
)
(
  cd "$DEPS/qtbase-build"
  "$SOURCE/configure" -prefix "$QT_TSAN_PREFIX" -release -force-debug-info \
    -nomake examples -nomake tests -no-gui -no-widgets -sanitize thread \
    -- -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_INSTALL_LIBDIR=lib
)
cmake --build "$DEPS/qtbase-build" --parallel 4
cmake --install "$DEPS/qtbase-build"
cmake -S "." -B "build-tsan" -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_PREFIX_PATH="$QT_TSAN_PREFIX" -DQt6_DIR="$QT_TSAN_PREFIX/lib/cmake/Qt6" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=ON \
  -DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=thread"
```

源码归档校验值、下载来源、Qt 配置和构建原始输出须与本轮证据一起保留。重复执行时使用
该版本对应的独立构建树；不要在同一 Qt build 目录中混用不同版本。runner 复用已经明确
设置的 `Qt6_DIR`，核对实际加载库、PIE、Qt 插桩配置及 Clang 版本，拒绝混入系统 Qt。
编译器、探针和 Qt 的 Clang 数字版本必须一致；`Ubuntu` 等厂商展示前缀不参与版本判定，
非 Clang、缺少版本或版本不一致仍失败。

#### 校准与产品验收

先准备第 4 节的本地 httpbin，运行命令必须继承其环境：

```bash
bash "tests/qcurl/httpbin/start_httpbin.sh" --port 0 --name "qcurl-tsan-httpbin" \
  --write-env "build/test-env/tsan-httpbin.env"
source "build/test-env/tsan-httpbin.env"
python3 "scripts/run_uce_sanitizers.py" --profile tsan --build-dir "build-tsan" \
  --output-dir "build/evidence/uce-sanitizers/tsan/$(date -u +%Y%m%dT%H%M%S%NZ)" --nproc 4
bash "tests/qcurl/httpbin/stop_httpbin.sh" --name "qcurl-tsan-httpbin"
```

- `qcurl_qt_tsan_control` 只链接 QtCore/Threads，不链接 QCurl/QtTest，也不注册普通 CTest。
  `std-mutex`、`qt-mutex`、`qt-wait`、`qt-queued` 必须有准确结果且零报告；
  `deliberate-race` 必须同时返回 `66` 并定位 `deliberateRaceWrite` 的数据竞争。
- 校准后，严格运行 Reply、ConnectionPool、ActorThreadModel、PoolContract、QCurlRuntime、
  NativeDiagnostics、CompletionContract，以及能力启用时的 WebSocket/WebSocketPool。
  代表集合不仅核对注册名称，还要求本次 CTest 输出中每个必需目标有唯一的 `Passed` 结果；
  部分目标被禁用、跳过或缺失执行结果时，即使 CTest 返回 `0`，TSan 门禁仍失败。
  该目标级检查仅属于 TSan 入口，不改变公共 `ctest_strict.py` 的其他调用方语义。
  Scheduler 继续逐函数隔离；空集合、缺少目标函数、skip、blacklist、超时或工具异常均失败。
- `TSAN_OPTIONS` 固定报告开关、退出码 `66` 和检测范围，不接受任意外部选项。产品只沿用
  `qt_test_tsan.supp` 的两条精确 QtTest 日志抑制与既有 watchdog 线程退出处理；独立对照
  不加载该抑制并开启线程泄漏报告。不得扩大抑制来消除 Qt 同步或 QCurl 共享状态报告。
- Scheduler 保留真实退出码、目标函数 `PASS` 和完整 `Totals` 检查。合法的
  `ThreadSanitizer: Matched ... suppressions` 统计不是错误，不因出现 `Sanitizer:` 字样
  拒绝成功运行；真实 TSan 非零退出仍失败。独立正负对照的报告判定保持不变。
- `report.json` 保存源码运行前后指纹、有效检测选项、环境身份和每条命令退出码；超时的
  门禁返回码与实际被终止的进程状态分别记录。`logs/` 保存原始输出及抑制命中。
  源码漂移、环境失败和产品失败均不能形成通过结论，失败重跑必须使用新的证据目录。
- 未插桩 Qt 的报警不能直接定性为产品缺陷或误报。Helgrind 仅按需辅助裁决，不替代 TSan；
  加载器崩溃或尚未执行用例的 `0 errors` 不是通过。上述本地验收不等于发布资格。

### 8.4 无过滤完整安装包安全门禁

发布候选必须验证不带 `--component` 过滤的完整安装树，而不是仅验证 Core
或关闭能力后的裁剪构建：

```bash
python3 scripts/run_release_gate.py --tier full \
  --release-shared-build-dir build-release-shared \
  --release-static-build-dir build-release-static \
  --test-shared-gcc-build-dir build-test-shared-gcc \
  --test-shared-clang-build-dir build-test-shared-clang \
  --asan-ubsan-lsan-build-dir build-asan-ubsan-lsan \
  --tsan-build-dir build-tsan
```

- `tests/public_api/package_gate_manifest.json` 是四个导出目标的机器合同：
  `Core`、`BlockingExtras`、`TestSupport`、`OtherExtras` 均必须声明 shared/static
  consumer、生命周期测试和 sanitizer 证据。
- `qcurl_public_api_package_stage_install` 执行无过滤 `cmake --install`，
  `qcurl_public_api_package_install_inventory` 将安装树中的每个文件写入相对路径清单，
  并关联到对应运行时目标或 package 元数据。
- `release-shared` / `release-static` 都从完整 package stage 构建四个逻辑目标的正向 consumer；Core-only
  stage 仅用于证明 Extras 未泄漏到 Core component 安装面，且每份报告绑定对应 OFF producer tree。
- WebSocket-capable libcurl 必须保持默认 WebSocket 能力并通过 `tst_QCWebSocket` 与
  `tst_QCWebSocketPool`。`QCURL_FORCE_DISABLE_WEBSOCKET_SUPPORT=ON` 只允许作为能力矩阵
  negative variant，不能替代正式 release gate PASS。

## 9. basic-no-problem acceptance 迁移（条件性 hard-breaking）

旧 `basic_no_problem_gate` runner/workflow 已进入删除候选，但当前删除仍受替代合同约束：
必须先由 UCE nightly 证明旧 workflow 的触发、检查、异常和归档语义，并完成绑定当前候选的
fresh E2E；在此之前不得提交删除。

历史与当前核对资料：

- 覆盖矩阵：`docs/reviews/2026-09-03-basic-no-problem-to-uce-coverage-matrix.md`
- UCE 文档：`docs/uce/README.md`
- 历史执行快照：`docs/reviews/2026-09-03-cleanup-p3-6-final-execution.md`

当前归档门禁入口：

```bash
python3 scripts/run_uce_gate.py --tier nightly --build-dir build --run-id "<your-run-id>"
```

## 10. 常见失败原因

- 端口绑定受限：curl testenv / httpbin / 本地服务端无法启动
- Docker 权限不足：env 组依赖的本地服务无法拉起
- 未构建 h3-capable `nghttpx`：HTTP/3 gate 无法满足前置条件
- 外部资源漂移：`external_heavy` 依赖的公网资源可能返回 404 / DNS 失败 / 超时，应先确认远端可用性

这些问题应先解决环境，再讨论测试结论。
