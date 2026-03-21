# 构建与测试

本文是仓库内唯一的本地构建与回归命令入口。需要调整命令、门禁口径或前置条件时，请只修改本文，避免说明分叉。

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

- `public-api`：逐头 self-compile + 规则扫描（禁止 `<curl/...>` / `CURL*` / `curl_*` / Qt private / `*_p.h` / `tuple` 泄漏）
- `public-api-slow`：staging install、安装头集合校验、导出合同校验、staging-isolated consumer smoke（含 `QCNetworkReply_p.h` 反向断言）
- 为避免 `public-api` 正则误匹配 `public-api-slow`，文档统一使用带锚点的 label 写法

若当前 `build/` 是 bundled curl 一致性构建（`QCURL_BUILD_LIBCURL_CONSISTENCY=ON`），上述两条命令直接可用。

如需额外验证 system libcurl 构建路径，可使用一套更轻的本地构建目录：

```bash
cmake -S . -B build-public-api-system -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DBUILD_TESTING=ON \
  -DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF

cmake --build build-public-api-system --target QCurl qcurl_public_api_self_compile -j"$(nproc)"
ctest --test-dir build-public-api-system -L '^public-api$' --output-on-failure
ctest --test-dir build-public-api-system -L '^public-api-slow$' --output-on-failure
```

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

如果你只想做一次“我本机能不能全量跑完”的检查，可按顺序执行：

1. 完成 `cmake` 构建
2. 启动本地 `httpbin`
3. 运行 `ctest --test-dir build --output-on-failure`
4. 按需运行 `tests/libcurl_consistency/run_gate.py --suite all --with-ext --build`

这一口径用于开发者自检，不等于正式门禁。

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

## 8. basic-no-problem 归档门禁

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

## 9. 常见失败原因

- 端口绑定受限：curl testenv / httpbin / 本地服务端无法启动
- Docker 权限不足：env 组依赖的本地服务无法拉起
- 未构建 h3-capable `nghttpx`：HTTP/3 gate 无法满足前置条件
- 外部资源漂移：`external_heavy` 依赖的公网资源可能返回 404 / DNS 失败 / 超时，应先确认远端可用性

这些问题应先解决环境，再讨论测试结论。
