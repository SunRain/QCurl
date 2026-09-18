# UCE（Unified Contract & Evidence）

> 维护者使用正文：启动命令、前提、tier 与证据判据同页；机器字段由独立 schema 定义。

UCE 是 QCurl 面向门禁与证据链的统一入口：它不试图证明“所有实现细节都正确”，而是把**可观测 contract**、**可归档 evidence** 与 **fail-closed 判定**收敛到同一套口径里。

## 启动前提与命令

从仓库根目录运行。需要满足[开发依赖](../build-and-test.md)、curl 子模块/testenv、pytest、本地端口权限；nightly/soak 还需 strace、Docker/httpbin、Node 与受控 ws 依赖。HTTP/3 是否 required 按显式合同配置，不靠公网偶然成功。

```bash
git submodule update --init --recursive
cmake -S . -B build-uce -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DBUILD_TESTING=ON \
  -DQCURL_BUILD_SHARED_LIBS=ON -DQCURL_BUILD_LIBCURL_CONSISTENCY=ON
cmake --build build-uce --parallel
RUN_ID="local-pr-$(date -u +%Y%m%dT%H%M%S%NZ)"
python3 scripts/run_uce_gate.py --tier pr --build-dir build-uce \
  --run-id "$RUN_ID"
```

nightly/soak 的环境组使用本地 httpbin；在同一 shell 中继承 fixture 环境后运行：

```bash
./tests/qcurl/httpbin/start_httpbin.sh --write-env build-uce/test-env/httpbin.env
source build-uce/test-env/httpbin.env
RUN_ID="local-nightly-$(date -u +%Y%m%dT%H%M%S%NZ)"
python3 scripts/run_uce_gate.py --tier nightly --build-dir build-uce \
  --run-id "$RUN_ID"
./tests/qcurl/httpbin/stop_httpbin.sh
```

长跑将 tier 改为 soak，并使用新的 run-id；每次重跑必须保留独立证据，不覆盖旧目录。成功需要退出 0、required 合同通过且四件归档独立校验通过，不能只看输出的 PASS 字样。

## 1. 目标与非目标

### 1.1 目标

- 提供单一的 UCE runner 入口，统一产出 `manifest.json`、`policy_violations`、reports、logs、
  `tar.gz` 归档和包外 archive envelope。
- 用稳定版本化 contract 表达“哪些行为被证明了”，避免把实现细节、一次性日志和人工解释混入门禁结论。
- 明确 PR / nightly / soak 三层职责：默认 PR 只追求快速、确定性、低噪声；高成本放大项放到 nightly / soak。
- 所有 gate 均 fail-closed：缺失证据、未执行、跳过、脱敏失败、归档失败，都必须进入 `policy_violations`。

### 1.2 非目标

- 不把“全绿”表述成“实现绝对无问题”。
- 不把内部调度、对象释放顺序、线程细节等非稳定可观测面直接纳入默认 contract。
- 不在 schema 未冻结前扩散 provider / workflow / validator 的字段口径。

## 2. Tier 分层

| Tier | 默认定位 | 目标 | 允许成本 | 失败语义 |
|------|----------|------|----------|----------|
| `pr` | 默认 CI 快速门禁 | offline + `libcurl_consistency p0/p1`、TLC/HES 最小 contract、快速反馈 | 低到中 | 任一 required evidence 缺失即失败 |
| `nightly` | push/manual acceptance 与周期回归 | offline/env/p0/p1/p2、public-api-slow、capability skip=fail、DCI/BP/CTBP/HES/TLC、netproof | 中到高 | 缺失 required provider / evidence / archive 即失败 |
| `soak` | 长跑与稳定性放大 | nightly contract + 扩大 fixed seed 组 + 更长运行时长 | 高 | 与 nightly 一致，但允许运行时间更长 |

### 2.1 Netproof capability 口径

- `pr`：记录 capability 与 `downgrade_reason`，用于解释“本轮未覆盖什么”，但默认不把 `strace/tc/netns` 作为 required provider。
- `nightly` / `soak`：至少要求 `strace` provider 可用；`tc` / `netns` 是否 required 由具体 contract 决定，并必须写进 manifest。
- capability 结论必须可机器消费，不能只留在 CI 日志里。
- strace 使用 `-yy` 标注调用时的文件描述符类型，只排除明确的 `AF_UNIX` / `AF_NETLINK`
  本地通信；INET 及类型未知的网络操作仍阻断，包括继承、复制和复用的描述符。仅创建
  套接字的能力探测独立记录，不等同于网络传输。
- sanitizer 构建在这个 ptrace 子门禁中关闭 LSan；泄漏证据由独立 sanitizer 入口负责，不能把 strace 结果当作泄漏检测通过。

### 2.2 必需 CTest 目标的执行判据

- offline、env、capability、public-api-slow 各自以本轮标签选中的目标为必需集合，
  使用 CTest 自带的 JSON 清单保存到 `meta/ctest_list_<label>.json`；未选中的目标不参与核对。
- 进程退出码非零直接失败。退出码为零时，每个必需目标仍须在本次执行日志中具有唯一的
  CTest `Passed` 结果；空集合、Disabled、Skipped、缺失或重复结果都使对应合同失败。
- `manifest.json` 保留真实进程 `returncode`，不把判据失败伪装成子进程错误。
  执行证据缺口写入该结果的 `details.execution_errors` 和合同 `notes`，并使用既有
  `gate_*_failed` 策略码阻断；HTTPBin 的 env 入口传播同一合同结果。
- 只核对 CTest 的逐目标结果行，不全局搜索日志中的 Skipped、Disabled 或 Sanitizer。
  共享结果解析供 UCE 与 TSan 使用，不改变公共 `ctest_strict.py` 其他调用方的命令行语义。

## 3. 证据目录结构

UCE evidence root 以 `--build-dir build` 为例；本页启动命令使用 build-uce，对应输出前缀也改为 build-uce：

```text
build/evidence/uce/<run-id>/
├── manifest.json
├── policy_violations.json
├── logs/
├── reports/
├── contracts/
├── artifacts/
└── meta/
    └── candidate_fingerprint.json

build/evidence/uce/<run-id>.tar.gz
build/evidence/uce/<run-id>.archive-envelope.json
```

约束：

- `manifest.json` 是机器判定入口。
- `tar.gz` 是归档必需品；缺失即失败。
- 每个 run-id 只能使用一次；已有目录、tar 或 envelope 时拒绝运行，不覆盖旧证据。
- 包内外 manifest/policy 逐字节一致。归档新增失败时只有限次补打包失败快照，绝不
  通过包后重写元数据把旧成功状态留在 tar 内。
- `candidate_fingerprint` 记录 HEAD、完整 index、工作树/未跟踪内容、递归子模块与
  唯一正式发布合同 `docs/dev/release/2.0.0-hard-break-release-contract.md`；本次输出不参与
  指纹，避免自引用。本地 `.helloagents/` 不入库、不作为输入，缺少该目录不阻断 CI。
- contract report、raw evidence、capability snapshot、脱敏扫描结果都必须能通过 manifest 反向定位。

## 4. 合同族（Contracts@v1）

| Contract | 目标 | 典型证据源 |
|----------|------|------------|
| TLC | 时间线不变量：headers-before-body、finish/cancel 互斥、progress 单调 | `libcurl_consistency` artifacts、QtTest / DCI timeline evidence |
| CTBP | 连接复用 / TLS 边界证明 | conn-id、TLS policy、ALPN / SNI / proxy / client cert 证据 |
| HES | 头部 / 压缩 / 时序语义 | raw headers、encoding evidence、专题 report |
| DCI | 确定性 chaos 注入 | fixed seed + timeline contract |
| BP | backpressure 语义合同（buffer pressure + user pause/resume） | QtTest `dci_evidence_*.jsonl`（stream=`bp-user-pause`） |
| SAN-RACE | sanitizer 放大 | ASan / UBSan / LSan / TSAN report |
| HFG | offline 真实性（netproof） | `strace`、`tc`、`netns` capability 与对应 provider evidence |

### 4.1 DCI vs BP 边界（避免“混合同类不同确定性层级”）

- `dci@v1`：固定 seed 的 deterministic chaos（mock 注入），只包含少量**可复现**的 QtTest 用例集合，用于放大竞态并产出可解释 timeline。
- `bp@v1`：backpressure 语义是“外部行为合同”，依赖本地端口与传输节奏，不应混入 fixed-seed suite；因此作为独立合同在 `nightly/soak` 强制执行。
- 二者可以复用同一类证据载体（例如 `qcurl-uce/dci-evidence@v1` 的 JSONL），但 contract 的**边界与 required tier**不同。

### 4.2 HES v1 的最小强制覆盖清单（以合同为准）

`hes@v1` 目前的 required kinds 是“最小集合”，以合同文件为唯一事实来源（`tests/uce/contracts/hes@v1.yaml`）：

- `pr`：`accept_encoding`, `raw_headers`
- `nightly/soak`：`accept_encoding`, `raw_headers`, `expect_100_continue`, `chunked_upload`

## 5. 可证与不可证边界

### 5.1 当前明确可证

- 已写成 contract 且有稳定 evidence provider 的外部行为。
- 可以归档、可复跑、可被 manifest 反向定位的 gate 结果。
- capability / downgrade 语义已写入 manifest 的 provider 结论。

### 5.2 当前明确不可直接证明

- 所有线程调度与生命周期路径。
- 所有平台 TLS 后端差异。
- 未纳入专题 contract 的连接池与复用边界细节。
- 未纳入专题 contract 的头部 / 压缩 / transfer 语义。

这些内容只能在**专题 contract + 专题 evidence**存在时被纳入证明范围；没有专题时，README 必须显式说明“未覆盖”，而不是靠口头解释兜底。

## 6. 与现有入口的关系

| 当前入口 | 当前角色 | UCE 关系 |
|----------|----------|----------|
| `.github/workflows/pr_fast_gate.yml` | 现有快速门禁，覆盖 build / public-api / `ctest_strict offline` | 在 UCE PR tier 完整接管“最小一致性证据”前继续保留 |
| `tests/libcurl_consistency/run_gate.py` / `.github/workflows/libcurl_consistency_ext_gate.yml` | 当前最强的一致性专题与红线口径 | 作为 UCE provider 继续存在；UCE 包装其 evidence，不替换其专题 contract |

迁移原则：

- 在 UCE PR tier 未提供等价或更强证据前，不宣称 `pr_fast_gate` 已完成迁移。
- 在 UCE 未吸收 ext / HTTP3 / raw evidence contract 前，不削弱 `libcurl_consistency_ext_gate` 的现有边界。

**旧入口退役的验收边界**：`basic-no-problem` 的替代由 UCE nightly 承接，不能从历史删除记录或某次局部 PASS 推断当前候选合格。UCE 路由必须保留旧 workflow 的 push
（`master`/`main`/`develop`）与 `workflow_dispatch` 语义，并自动阻断 `public-api-slow`、
capability QtTest（skip=fail）、offline/env/p0、workload/finalize/归档异常和 required artifact
缺失。至少一次 fresh nightly E2E 必须绑定当前候选 fingerprint，且 manifest、policy report、
tar 和 archive envelope 均可独立验证；完成前不得把迁移写成“已完成”。

## 7. Sanitizer 放大

```bash
python3 scripts/run_uce_sanitizers.py --profile asan-ubsan-lsan \
  --build-dir build-asan-ubsan-lsan \
  --output-dir "build/evidence/uce-sanitizers/asan/$(date -u +%Y%m%dT%H%M%S%NZ)"
```

`asan-ubsan-lsan` 沿用独立构建与 `run_uce_gate.py --tier nightly`。先按[五树配置](../release/release-procedure.md#producer-trees)准备 Clang/Ninja sanitizer 树，runner 会按 profile 重新配置并构建。public-api-slow 的安装 consumer 显式继承 producer 的编译器及编译/链接参数，并记录 verbose 构建和实际运行输出；runner 固定开启泄漏检测与失败退出，报告保留有效 ASan/UBSan/LSan 选项。普通构建与 ASan 构建
不得指向下面的检测专用 Qt；它也不是 QCurl 消费者的新依赖。

### 7.1 检测专用 Qt

以下 TSan 教程是独立诊断入口，不是本次 QCurl 2.0.0 final 或 nightly CI 的前置要求。未运行、环境缺失或失败均不计入 final 通过数量，也不等于取得 TSan 覆盖；诊断确认的产品缺陷仍须处理。此非阻断政策仅适用于本次发布。

只给 QCurl 加 `-fsanitize=thread` 不足以观察 Qt 的同步路径。使用与普通构建同版本的
官方 qtbase，以同一个 Clang 构建 Qt 和 QCurl；不修改 `/usr`、不提高产品最低 Qt 版本。
编译器或依赖变化后必须重新校准，不能沿用旧报告。下面使用满足最低版本的宿主 Qt
准确版本，不取浮动分支；需要 Ninja、Clang、curl、tar、sha256sum 和 Qt 源码下载权限：

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

```

随后将独立 QCurl TSan 树指向 `QT_TSAN_PREFIX`，并使用相同 Clang；不要只给 QCurl 加插桩：

```bash
: "${QT_TSAN_PREFIX:?先准备同版本插桩 Qt 并设置其绝对安装前缀}"
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_PREFIX_PATH="$QT_TSAN_PREFIX" -DQt6_DIR="$QT_TSAN_PREFIX/lib/cmake/Qt6" \
  -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=ON \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF \
  -DQCURL_SANITIZER_PROFILE=tsan \
  -DCMAKE_C_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=thread"
```

源码归档校验值、下载来源、Qt 配置和构建原始输出须与本轮证据一起保留。重复执行时使用
该版本对应的独立构建树；不要在同一 Qt build 目录中混用不同版本。runner 复用已经明确
设置的 `Qt6_DIR`，核对实际加载库、PIE、Qt 插桩配置及 Clang 版本，拒绝混入系统 Qt。
编译器、探针和 Qt 的 Clang 数字版本必须一致；`Ubuntu` 等厂商展示前缀不参与版本判定，
非 Clang、缺少版本或版本不一致仍失败。

### 7.2 校准与产品验收

先准备本页启动前提中的本地 httpbin，运行命令必须继承其环境：

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


## 8. Schema 与实现入口

- [Manifest 合同](schema/manifest@v1.md)
- [Evidence 合同](schema/evidence@v1.md)
- 发布身份与完整 acceptance 约束：[正式发布合同](../release/2.0.0-hard-break-release-contract.md)
- 策略字典：[机器输入](policy_violations_dictionary.json)与[语义说明](policy_violations_dictionary.md)
- Netproof capability 探测：`scripts/netproof_capabilities.py`
- Netproof runner：`scripts/netproof_strace_gate.py`
- Sanitizer runner：`scripts/run_uce_sanitizers.py`
- UCE runner：`scripts/run_uce_gate.py`
- 归档独立校验：运行下列命令，run-id 必须是本次实际生成的 ID：

  ```bash
  python3 scripts/verify_uce_archive.py --evidence-root build-uce/evidence/uce \
    --run-id "$RUN_ID" --require-pass
  ```

  将 `RUN_ID` 设为前面日志中的实际 run-id；逐项核对 manifest、policy、tar、archive envelope，校验包内外元数据一致、成员完整性、完整 gzip 与 envelope digest/size。缺件或失败快照不算成功归档。
- UCE 负向基础设施：`ctest --test-dir <build> -R '^qcurl_uce_infrastructure$' --output-on-failure`，属于所有 tier 的 offline 阻断项。
- CI 入口：`.github/workflows/pr_fast_gate.yml`、`.github/workflows/uce_nightly.yml`、`.github/workflows/uce_soak.yml`

三个 CI 入口在上传前逐个校验 manifest、policy、tar 和 envelope；校验与诊断上传均
使用 `always()`。`if-no-files-found: error` 只防止整组路径无匹配，不能替代逐文件校验。
本地校验不代表已执行 GitHub Actions 的远端持久化上传。

## 9. 维护规则

- 先改 schema，再改 runner / validator / workflow。
- `policy_violations` 新 code 必须先同步登记到两份正式字典，再落到脚本；运行
  `python3 scripts/validate_policy_violations_dictionary.py` 校验，缺文件或集合不一致均失败。
- 方案、会话与本地执行记录保留在被忽略的 `.helloagents/`，不得成为 CI 的隐式前置。
- 任何“缺失 provider 但这次先算通过”的特殊口径，都必须在 schema 和 README 里显式写清，不允许只存在于 CI 说明文字。
