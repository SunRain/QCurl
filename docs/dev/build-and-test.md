# 构建与测试

本文负责日常开发构建、QtTest 与 libcurl 一致性专题的命令、前提和结果解释。候选资格与五树命令只在[发布操作](release/release-procedure.md)，UCE 启动、sanitizer 和归档判据只在[UCE 使用](uce/README.md)。所有命令默认从仓库根目录执行。

## 1. 开发构建

最低 Qt **6.10.3**、C++17、libcurl 7.85.0；源码构建需要 QtCore/QtNetwork 与 zlib 开发包。测试需要 QtTest、Python 3/pytest，具体本地服务见下节。使用独立 Qt SDK 时加 `-DCMAKE_PREFIX_PATH=/path/to/Qt/6.10.3/gcc_64`；Qt 6.10.0–6.10.2 不受支持。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=ON \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF
cmake --build build --parallel
```

仓库默认打开 tests/examples/benchmarks；上面只关闭与日常库测试无关的示例和基准。默认使用 system libcurl；bundled 一致性构建见后文。Ubuntu/Debian CI 的 Qt SDK 由[setup-qt action](../../.github/actions/setup-qt/action.yml)读取 `QCURL_MIN_QT_VERSION`，Arch 快照也须满足同一下限。

## 2. QtTest 与本地服务

### 离线严格检查

```bash
python3 scripts/ctest_strict.py --build-dir build
```

默认选择 `offline`，不依赖外网，QtTest skip 计数必须为 0。需要缩小范围时传递 CTest 选择参数，并确认实际执行目标非空；不能用删掉失败目标来宣称原集合通过。

### httpbin 与环境组

需要 Docker、本机端口监听权限；部分测试还需 Node 和受控 WebSocket 依赖。服务准备：

```bash
./tests/qcurl/httpbin/start_httpbin.sh --write-env build/test-env/httpbin.env
source build/test-env/httpbin.env
curl -fsS "${QCURL_HTTPBIN_URL}/get" >/dev/null
python3 scripts/ctest_strict.py --build-dir build --label-regex env
./tests/qcurl/httpbin/stop_httpbin.sh
```

`env/local_port/httpbin/websocket` 组通过 suite preflight 检查端口、`QCURL_HTTPBIN_URL`、Node 与 ws；缺前置直接失败。`tst_QCNetworkHttp2` 默认启动仓库 Node fixture，并通过 HTTP/2 capability probe 与 suite preflight 检查头文件/运行时能力：

```bash
ctest --test-dir build -R '^tst_QCNetworkHttp2$' --output-on-failure
```

需要改地址时使用 `QCURL_HTTP2_TEST_BASE_URL` / `QCURL_HTTP2_TEST_HTTP1_BASE_URL`。沙箱禁止监听不算产品通过，应补足执行环境，不把 skip 改成 PASS。

### 检查选择与结果

```bash
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
```

- 非 `external_*` QtTest 用 `FAIL_REGULAR_EXPRESSION` 将 QSKIP 判为失败；严格 wrapper 另核对 skip 计数。仅有 CTest 退出 0 不能证明未执行/禁用的目标通过。
- coverage map 的 `contract` / `regression` / `smoke` / `diagnostic` 含义与标签表见[测试目录](../../tests/README.md)；`tests/coverage-map.yaml` 关联代表测试。
- `tst_QCNetworkDiagnosticsLocal` 是 deterministic env provider：覆盖本地 DNS、HTTP 200/404、TLS fixture、heartbeat/deadline、取消/析构/并发的恰好一次 Future 完成。`checkSSL()` 没有显式 CA 注入入口，本地 TLS 用例证明证书校验及明确结果，不保证默认信任 fixture。
- 公网 `tst_QCNetworkDiagnostics` 与 `external_heavy` 只作显式 smoke。大文件 smoke 可用 `QCURL_RUN_EXTERNAL_HEAVY=1` 选择；URL 与长度通过 `QCURL_LARGE_FILE_URL` / `QCURL_LARGE_FILE_EXPECTED_BYTES` 设置，HEAD preflight 失败可 QSKIP，不进入 deterministic 通过证明。

<a id="public-api"></a>
## 3. Public API 与安装消费检查

改动公开头、组件、install/export 或 pkg-config 时，使用已构建的 shared/ON 测试树：

```bash
ctest --test-dir build -L '^public-api$' --output-on-failure
ctest --test-dir build -L '^public-api-slow$' --output-on-failure
```

- `public-api`：逐头 self-compile、禁止实现泄漏的扫描与机器清单校验。
- `public-api-slow`：staging install、安装集合/导出检查、隔离 consumer、非默认组件 opt-in 及默认 Core 负向 consumer。不能回落源码 include。
- 标签是正则，使用锚点避免将两组混为一组。consumer 的独有覆盖与四类合同检查见[公共头边界](architecture/public-header-boundary.md#contract-checks)。
- static/OFF 的安装、consumer 和生命周期证据用[package evidence](release/release-procedure.md#package-evidence)，不在 OFF 树运行 CTest。static 与测试开启的组合固定拒绝：`QCURL_STATIC_TESTING_UNSUPPORTED：静态构建不支持测试`；保留负向 configure 检查，不为其提供正向教程。

Linux 安装面验证的范围：bundled curl + WebSocket ON 是必须保留的合同路径；涉及安装/导出时按需补 system libcurl；WebSocket OFF 只用于能力缺失负向变体，不替代正式完整包证据。日常 system 分支的配置为：

```bash
cmake -S . -B build-public-api-system -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DBUILD_TESTING=ON \
  -DQCURL_BUILD_SHARED_LIBS=ON -DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF
cmake --build build-public-api-system --parallel
ctest --test-dir build-public-api-system -L '^public-api$' --output-on-failure
ctest --test-dir build-public-api-system -L '^public-api-slow$' --output-on-failure
```

确需检查 WebSocket 条件安装时，在另一 shared/ON 测试目录增加 `-DQCURL_FORCE_DISABLE_WEBSOCKET_SUPPORT=ON`，再执行相同标签；不要将裁剪结果声称为正式包通过。

### 白盒 companion

`BUILD_TESTING=ON` 下的正式 QCurl、Other Extras、Test Support 仍为发行语义，不定义 `QCURL_ENABLE_TEST_HOOKS`。白盒 QtTest/一致性测试按所有权链接 TestInternals companion；它们不安装、不导出，OFF producer 不生成 companion。Blocking Extras 是 INTERFACE 消费面，测试实现归入 Core companion。

<a id="libcurl-consistency"></a>
## 4. libcurl consistency

先取得 curl 子模块并准备其 testenv 依赖、Python pytest 和本地 HTTP/TLS fixture；完整依赖与专题定义见[一致性 README](../../tests/libcurl_consistency/README.md)。以下构建使用 bundled curl，而不是默认 system libcurl：

```bash
git submodule update --init --recursive
cmake -S . -B build-lc -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DBUILD_TESTING=ON \
  -DQCURL_BUILD_SHARED_LIBS=ON -DQCURL_BUILD_LIBCURL_CONSISTENCY=ON
cmake --build build-lc --parallel
python3 tests/libcurl_consistency/run_gate.py --suite p0 --qcurl-build build-lc
```

扩展检查选择：

```bash
python3 tests/libcurl_consistency/run_gate.py --suite p1 --qcurl-build build-lc
python3 tests/libcurl_consistency/run_gate.py --suite all --qcurl-build build-lc
QCURL_LC_EXT=1 python3 tests/libcurl_consistency/run_gate.py \
  --suite all --with-ext --qcurl-build build-lc
```

`--build` 可让 runner 配置/构建上述显式 `--qcurl-build` 目录。HTTP/3 是业务门槛时，在扩展命令前设置 `QCURL_REQUIRE_HTTP3=1`；准备 h3-capable nghttpx 和匹配的 libcurl/QUIC backend，缺能力必须失败。

### 判据与证据

- 只用 run_gate 作为正式专题入口：它先生成 `build-lc/libcurl_consistency/reports/capabilities.json`，按能力规划测试，完成 schema 与脱敏检查、skip=fail 和报告。裸 pytest 用于诊断，不能替代这条证据链。
- capabilityMatrix 记录 build/runtime libcurl、HTTP/2/3、WebSocket、HSTS、Alt-Svc、proxy/SOCKS、TLS pinning 与 raw observability，并区分 Fail/Warn/Preview。
- P0 是最小可重复的字节级或摘要级证据；不能据此推断全部并发、复用与 pause/resume 语义。强判据如 pause_resume_strict、resp_headers_raw 以对应专题的原始字节或结构化事件为准。
- 不把 libcurl 内部状态、动态日期/头、multipart boundary、版本特有诊断文本或未约定的完成顺序当作默认比较合同。
- gate 全绿也不证明所有生命周期、竞态、TLS 后端、复用或压缩时序。先看被选中的 suite，再看专题合同及其报告；缺专题覆盖时明确写未覆盖，不能用解释补成 PASS。

<a id="maintenance-rules"></a>
### 持续维护规则

`tests/libcurl_consistency/coverage-map.yaml` 记录覆盖映射，稳定合同留在专题 README，专题决策留在 handoff；逐次运行日志保留在 reports/artifacts，**不再维护完成度或行数状态板**。

新增 public 可观察行为、既有 contract 无法表达、反复差异无法归因、只能靠口头例外解释“全绿”时，应补充对应专题用例。优先调整测试和 compare/schema，再同步说明；不能通过放宽规则制造一致。审查提出任务时写清输入、期望输出、边界、优先级、验收和复现步骤，但不把任务列表写入历史归档或当前规则正文。

## 5. 文档与轻量检查

无需新建 C++ 构建树即可执行：

```bash
git diff --check
python3 scripts/run_release_gate.py --scan-metadata
python3 scripts/validate_policy_violations_dictionary.py
python3 tests/public_api/run_public_api_checks.py hard-break-guards --repo-root .
```

检查文档时还需核对有效链接/章节、仓内路径、命令参数和前置条件。词法扫描不会编译 C++ 示例；修改 Quickstart/README 示例时从最终正文取用代码，在安装前缀上独立 configure、build 并核对实际响应。API 文档生成与产物检查见[Doxygen 正文](api-docs.md)。

## 6. 失败处理

- 端口绑定、Docker、Node/ws、httpbin 或 h3-capable nghttpx 不可用：先补齐对应环境，不降低原测试合同。
- 报告缺失、选择为空、失败退出、skip 或 sanitizer 报错：分别记录真实原因；聚合退出 0 不能掩盖证据缺口。
- 公网漂移只影响外部 smoke；不能把它归咎于 deterministic 本地 fixture，也不能用外部成功替代本地合同。

日常检查仅证明本次选择范围；发布资格、远端 CI、Git 操作和正式交付是不同结论。
