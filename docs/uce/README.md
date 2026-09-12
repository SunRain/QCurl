# UCE（Unified Contract & Evidence）

> Maintainer evidence contract: this document is not part of the first-public-release user navigation. Keep it focused on gate semantics and reproducible evidence.

UCE 是 QCurl 面向门禁与证据链的统一入口：它不试图证明“所有实现细节都正确”，而是把**可观测 contract**、**可归档 evidence** 与 **fail-closed 判定**收敛到同一套口径里。

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

UCE evidence root 约定为：

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
  唯一正式发布合同 `docs/arch/2.0.0-hard-break-release-contract.md`；本次输出不参与
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

**条件性迁移（2026-09-05）**：旧 `basic-no-problem` runner/workflow 的终局仍由 UCE nightly
承接，但当前删除候选尚未达到可提交状态。UCE 路由必须保留旧 workflow 的 push
（`master`/`main`/`develop`）与 `workflow_dispatch` 语义，并自动阻断 `public-api-slow`、
capability QtTest（skip=fail）、offline/env/p0、workload/finalize/归档异常和 required artifact
缺失。至少一次 fresh nightly E2E 必须绑定当前候选 fingerprint，且 manifest、policy report、
tar 和 archive envelope 均可独立验证；完成前不得把迁移写成“已完成”。

## 7. Schema 与实现入口

- Manifest 合同：`docs/uce/schema/manifest@v1.md`
- Evidence 合同：`docs/uce/schema/evidence@v1.md`
- 发布身份与完整 acceptance 约束：[正式发布合同](../arch/2.0.0-hard-break-release-contract.md)
- 策略字典：[机器输入](policy_violations_dictionary.json)与[语义说明](policy_violations_dictionary.md)
- Netproof capability 探测：`scripts/netproof_capabilities.py`
- Netproof runner：`scripts/netproof_strace_gate.py`
- Sanitizer runner：`scripts/run_uce_sanitizers.py`
- UCE runner：`scripts/run_uce_gate.py`
- 归档独立校验：`scripts/verify_uce_archive.py --evidence-root <build>/evidence/uce --run-id <run-id> --require-pass`
- UCE 负向基础设施：`ctest --test-dir <build> -R '^qcurl_uce_infrastructure$' --output-on-failure`，属于所有 tier 的 offline 阻断项。
- CI 入口：`.github/workflows/pr_fast_gate.yml`、`.github/workflows/uce_nightly.yml`、`.github/workflows/uce_soak.yml`

三个 CI 入口在上传前逐个校验 manifest、policy、tar 和 envelope；校验与诊断上传均
使用 `always()`。`if-no-files-found: error` 只防止整组路径无匹配，不能替代逐文件校验。
本地校验不代表已执行 GitHub Actions 的远端持久化上传。

## 8. 维护规则

- 先改 schema，再改 runner / validator / workflow。
- `policy_violations` 新 code 必须先同步登记到两份正式字典，再落到脚本；运行
  `python3 scripts/validate_policy_violations_dictionary.py` 校验，缺文件或集合不一致均失败。
- 方案、会话与本地执行记录保留在被忽略的 `.helloagents/`，不得成为 CI 的隐式前置。
- 任何“缺失 provider 但这次先算通过”的特殊口径，都必须在 schema 和 README 里显式写清，不允许只存在于 CI 说明文字。
