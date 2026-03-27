# UCE（Unified Contract & Evidence）

UCE 是 QCurl 面向门禁与证据链的统一入口：它不试图证明“所有实现细节都正确”，而是把**可观测 contract**、**可归档 evidence** 与 **fail-closed 判定**收敛到同一套口径里。

## 1. 目标与非目标

### 1.1 目标

- 提供单一的 UCE runner 入口，统一产出 `manifest.json`、`policy_violations`、reports、logs 与 `tar.gz` 归档。
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
| `nightly` | 高强度回归 | DCI fixed seed、CTBP、HES 扩展、`strace` netproof、专题 contract 聚合 | 中到高 | 缺失 required provider / evidence / archive 即失败 |
| `soak` | 长跑与稳定性放大 | nightly contract + 扩大 fixed seed 组 + 更长运行时长 | 高 | 与 nightly 一致，但允许运行时间更长 |

### 2.1 Netproof capability 口径

- `pr`：记录 capability 与 `downgrade_reason`，用于解释“本轮未覆盖什么”，但默认不把 `strace/tc/netns` 作为 required provider。
- `nightly` / `soak`：至少要求 `strace` provider 可用；`tc` / `netns` 是否 required 由具体 contract 决定，并必须写进 manifest。
- capability 结论必须可机器消费，不能只留在 CI 日志里。

## 3. 证据目录结构

UCE evidence root 约定为：

```text
build/evidence/uce/<run-id>/
├── manifest.json
├── logs/
├── reports/
├── contracts/
├── artifacts/
└── meta/

build/evidence/uce/<run-id>.tar.gz
```

约束：

- `manifest.json` 是机器判定入口。
- `tar.gz` 是归档必需品；缺失即失败。
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
| `scripts/run_basic_no_problem_gate.py` / `.github/workflows/basic_no_problem_gate.yml` | 当前最接近“可归档 acceptance evidence”的组合 gate | 是 UCE runner 的主要收敛对象，迁移前保持 legacy 角色 |
| `tests/libcurl_consistency/run_gate.py` / `.github/workflows/libcurl_consistency_ext_gate.yml` | 当前最强的一致性专题与红线口径 | 作为 UCE provider 继续存在；UCE 包装其 evidence，不替换其专题 contract |

迁移原则：

- 在 UCE PR tier 未提供等价或更强证据前，不宣称 `pr_fast_gate` 已完成迁移。
- 在 UCE runner 未稳定接管归档与 fail-closed 语义前，不下线 `basic-no-problem`。
- 在 UCE 未吸收 ext / HTTP3 / raw evidence contract 前，不削弱 `libcurl_consistency_ext_gate` 的现有边界。

## 7. Schema 与实现入口

- Manifest 合同：`docs/uce/schema/manifest@v1.md`
- Evidence 合同：`docs/uce/schema/evidence@v1.md`
- Netproof capability 探测：`scripts/netproof_capabilities.py`
- Netproof runner：`scripts/netproof_strace_gate.py`
- Sanitizer runner：`scripts/run_uce_sanitizers.py`
- UCE runner：`scripts/run_uce_gate.py`
- CI 入口：`.github/workflows/pr_fast_gate.yml`、`.github/workflows/uce_nightly.yml`、`.github/workflows/uce_soak.yml`

## 8. 维护规则

- 先改 schema，再改 runner / validator / workflow。
- `policy_violations` 新 code 必须先登记到稳定字典，再落到脚本。
- 任何“缺失 provider 但这次先算通过”的特殊口径，都必须在 schema 和 README 里显式写清，不允许只存在于 CI 说明文字。
