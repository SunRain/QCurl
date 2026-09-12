# policy_violations 稳定字典（SSOT）

> 目的：固化测试证据链产物中的 `policy_violations` 枚举值（可机器判定/可统计/可审计）。
>
> 稳定性规则（强制）：
> 1) **禁止**修改既有 code 的语义（只能补充说明/定位建议）。
> 2) 新增 code 必须先更新本文件并在对应脚本/门禁中落地。
> 3) 机器消费端遇到未知 code 时应视为 **失败且需要人工复核**（避免“新增违例未被识别→误判通过”）。

机器可读版本（必须与本文件枚举值一致，用于 CI 校验）：
- [policy_violations_dictionary.json](policy_violations_dictionary.json)

schema_version: 1
last_updated: 2026-09-05

---

## 字段出现位置

1) **UCE 取证门禁**
- 工件：`build/evidence/uce/<run-id>/manifest.json`
- 字段：`policy_violations: string[]`
- 入口：`python3 scripts/run_uce_gate.py --tier nightly --build-dir build --run-id <run-id>`

2) **libcurl_consistency Gate**
- 工件：`build/libcurl_consistency/reports/gate_<suite>.json`（或 `--reports-dir` 指定目录）
- 字段：`policy_violations: string[]`
- 入口：`python3 tests/libcurl_consistency/run_gate.py --suite p0|p1|p2|all ...`

---

## 机器判定规则（统一口径）

- `policy_violations == []` **且** `result == "pass"` → 通过（证据链完整、口径满足）
- `policy_violations != []` → 失败（即使某些子步骤 `returncode == 0`，也不得给出“通过”结论）

---

## 严重级别（用于排障优先级，不改变“非空即失败”的门禁语义）

- `evidence_severity`（对证据链的影响）：
  - **CRITICAL**：证据链中断/不可归档/安全违规/无法证明；必须优先处理
  - **HIGH**：证据集合已执行但失败；通常意味着实现回归或一致性破坏（也可能是环境异常）
  - **MEDIUM**：报告/打包/解析问题；实现质量信号不明确，但证据链不完整仍必须失败

- `quality_signal`（对“实现质量”的指示性，便于决策）：
  - **IMPLEMENTATION**：更可能是实现/行为回归
  - **INFRA**：更可能是环境/权限/依赖问题
  - **SECURITY**：更可能是敏感信息落盘等安全问题
  - **EVIDENCE**：更可能是取证/归档/报告生成问题
  - **MIXED**：实现与环境均可能（需结合日志判定）

---

## 枚举值：UCE（manifest.json）

| code | evidence_severity | quality_signal | 含义（触发条件） | 优先定位工件（建议） |
|---|---|---|---|---|
| `gate_offline_failed` | HIGH | IMPLEMENTATION | `LABELS=offline` 严格门禁失败（含 test 失败或 skipped 超限）。 | `logs/ctest_strict_offline.log`、`meta/ctest_list_offline.txt` |
| `gate_public_api_slow_failed` | HIGH | IMPLEMENTATION | `public-api-slow` 安装/消费者契约门禁失败或没有可执行测试。 | `logs/public_api_slow.log`、`meta/ctest_list_public_api_slow.txt` |
| `gate_capability_failed` | HIGH | IMPLEMENTATION | `capability` QtTest 严格门禁失败或发生 skipped（skip=fail）。 | `logs/capability.log`、`meta/ctest_list_capability.txt` |
| `env_preflight_httpbin_start_failed` | CRITICAL | INFRA | httpbin 启动失败（Docker 权限/镜像/端口/健康检查失败等）。 | `logs/httpbin_start.log` |
| `env_preflight_httpbin_env_missing` | CRITICAL | EVIDENCE | httpbin 启动命令返回成功，但未生成 `httpbin.env`（证据链断裂）。 | `logs/httpbin_start.log`、`httpbin/httpbin.env`（缺失） |
| `env_preflight_httpbin_env_parse_error` | CRITICAL | EVIDENCE | `httpbin.env` 存在但解析失败（无法获得 `QCURL_HTTPBIN_URL` 等关键环境）。 | `httpbin/httpbin_env_parse_error.txt`、`httpbin/httpbin.env` |
| `env_preflight_httpbin_url_missing` | CRITICAL | EVIDENCE | httpbin 已启动，但 env 缺失 `QCURL_HTTPBIN_URL`（无法运行 `LABELS=env` 证据集合）。 | `httpbin/httpbin.env` |
| `gate_env_failed` | HIGH | MIXED | `LABELS=env` 严格门禁失败（真实执行后失败或 skipped 超限）。 | `logs/ctest_strict_env.log`、`meta/ctest_list_env.txt` |
| `gate_libcurl_consistency_p0_failed` | HIGH | IMPLEMENTATION | `tests/libcurl_consistency/run_gate.py --suite p0` 失败（包含 policy checks）。 | `logs/libcurl_consistency_p0.log`、`libcurl_consistency/reports/runs/uce-<run-id>-p0/gate_p0.json`、同目录 `junit_p0.xml` |
| `gate_libcurl_consistency_p1_failed` | HIGH | IMPLEMENTATION | `tests/libcurl_consistency/run_gate.py --suite p1` 失败（包含 policy checks）。 | `logs/libcurl_consistency_p1.log`、`libcurl_consistency/reports/runs/uce-<run-id>-p1/gate_p1.json`、同目录 `junit_p1.xml` |
| `gate_libcurl_consistency_p2_failed` | HIGH | IMPLEMENTATION | `tests/libcurl_consistency/run_gate.py --suite p2` 失败（包含 CTBP 相关 TLS/边界专题与 policy checks）。 | `logs/libcurl_consistency_p2.log`、`libcurl_consistency/reports/runs/uce-<run-id>-p2/gate_p2.json`、同目录 `junit_p2.xml` |
| `gate_exception` | CRITICAL | EVIDENCE | UCE 编排或门禁工作负载发生未预期异常（必须结构化记录并尽量归档可用证据）。 | `manifest.json` 的 `exception` 字段与 `logs/gate_exception.log` |
| `packaging_tar_gz_failed` | CRITICAL | EVIDENCE | 证据 tarball 打包失败（无法归档，证据链不完整）。 | `manifest.json` 的 `packaging.tar_gz_error`、文件系统空间/权限 |
| `archive_envelope_source_missing` | CRITICAL | EVIDENCE | 写 archive envelope 时归档文件缺失（打包声称成功但产物不存在，或被外部删除）。 | `manifest.json: packaging.envelope_error`、`<run-id>.tar.gz`（缺失） |
| `archive_envelope_read_failed` | CRITICAL | EVIDENCE | 归档文件存在但无法读取以计算 sha256（I/O 错误、权限不足、介质故障），归档身份无法证明。 | `manifest.json: packaging.envelope_error`、文件系统权限/介质健康状态 |
| `archive_envelope_write_failed` | CRITICAL | EVIDENCE | 归档可读但 envelope 落盘失败（磁盘满、权限不足、序列化错误），归档身份记录未持久化。 | `manifest.json: packaging.envelope_error`、`<run-id>.archive-envelope.json`（缺失或不完整） |
| `env_preflight_httpbin_stop_failed` | MEDIUM | INFRA | httpbin 停止命令失败（容器残留，可能影响后续 run 的端口分配）。 | `logs/httpbin_stop.log`、`docker ps -a` 中的残留容器 |
| `artifact_required_missing` | CRITICAL | EVIDENCE | UCE manifest 中标记为 `required=true` 的 artifact 缺失。 | `manifest.json: artifacts`、`policy_violations.json: missing_required_artifacts` |
| `capability_required_provider_missing` | CRITICAL | INFRA | 当前 tier 声明的 required provider 缺失（如 nightly 缺 `strace`）。 | `manifest.json: capabilities.netproof`、`netproof/capabilities.json` |
| `timeline_provider_missing` | CRITICAL | EVIDENCE | TLC contract 声明的 required provider 缺失 timeline stream（如 PR 缺 Qt、nightly 缺 baseline/qcurl 任一侧）。 | `manifest.json: contracts.timeline@v1`、`timeline/report.json: provider_summary` |
| `timeline_evidence_parse_error` | CRITICAL | EVIDENCE | TLC timeline evidence 无法解析（无效 UTF-8/JSON、非对象内容或读取失败）。 | `timeline/report.json: violations`、`timeline/*.timeline.jsonl` |
| `timeline_contract_failed` | HIGH | IMPLEMENTATION | TLC 校验发现 headers/progress/pause/backpressure/terminal invariant 失败。 | `timeline/report.json: streams[].violations`、`timeline/*.timeline.jsonl` |
| `ctbp_evidence_missing` | CRITICAL | EVIDENCE | CTBP contract 要求的 `runner × kind` 组合缺失（如 nightly 缺 baseline/qcurl 任一侧或缺连接/TLS 任一类证据）。 | `manifest.json: contracts.ctbp@v1`、`ctbp/report.json: violations` |
| `ctbp_contract_failed` | HIGH | IMPLEMENTATION | CTBP 校验发现连接复用/TLS 边界证明失败（如跨边界复用、unique_connections 不一致、TLS 失败口径漂移）。 | `ctbp/report.json: entries[].violations`、`ctbp/evidence.json` |
| `hes_evidence_missing` | CRITICAL | EVIDENCE | HES contract 要求的 `runner × kind` 组合缺失（例如缺 baseline/qcurl 任一侧或缺某类 HES kind 证据）。 | `hes/report.json: missing`、`manifest.json: contracts.hes@v1` |
| `hes_contract_failed` | HIGH | IMPLEMENTATION | HES 校验发现头部/压缩/Expect/Chunked 语义不满足合同，或 required coverage 缺失。 | `hes/report.json: entries[].violations`、`hes/report.json: missing` |
| `dci_binary_missing` | CRITICAL | INFRA | DCI QtTest 二进制缺失（无法执行 fixed-seed suite）。 | `dci/report.json: qt_test_binary` |
| `dci_seed_run_failed` | HIGH | MIXED | DCI fixed-seed QtTest 执行失败（returncode!=0，可能是实现回归或环境波动）。 | `dci/report.json: runs[]`、对应 `logs/dci_*.log` |
| `dci_evidence_missing` | CRITICAL | EVIDENCE | DCI 用例执行后未产出 `dci_evidence_*.jsonl`（skip/未执行/证据链断裂均视为失败）。 | `dci/report.json: runs[].evidence_files` |
| `netproof_strace_missing` | CRITICAL | INFRA | netproof 所需 `strace` 缺失（nightly/soak required provider 不满足）。 | `netproof/strace_report.json: policy_violations` |
| `netproof_subject_failed` | HIGH | MIXED | offline subject 在 `strace` 下执行失败（returncode!=0）。 | `netproof/strace_report.json: subject_returncode`、`netproof/trace/subject.log` |
| `netproof_network_syscall_detected` | HIGH | IMPLEMENTATION | offline subject 观测到 network syscalls（违反 offline 真实性合同）。 | `netproof/strace_report.json: network_syscalls` |
| `bp_binary_missing` | CRITICAL | INFRA | backpressure 合同所需 QtTest 二进制缺失。 | `bp/report.json: qt_test_binary` |
| `bp_test_run_failed` | HIGH | MIXED | backpressure QtTest 用例执行失败（returncode!=0）。 | `logs/bp_testAsyncDownloadBackpressure.log`、`bp/report.json: gate` |
| `bp_evidence_missing` | CRITICAL | EVIDENCE | backpressure 用例未产出目标 stream 的 `dci_evidence_*.jsonl`（skip/未执行/证据链断裂均视为失败）。 | `bp/report.json: summary`、`bp/report.json: evidence_files` |
| `bp_evidence_parse_error` | CRITICAL | EVIDENCE | backpressure JSONL 证据解析失败（格式损坏或混入非 JSON 行）。 | `bp/report.json: summary.parse_errors`、`bp/report.json: evidence.scanned_files` |
| `bp_contract_failed` | HIGH | IMPLEMENTATION | BP validator 发现事件顺序/不变量/最终结果不满足合同（如 pause drift 超限、body_len 不一致）。 | `bp/report.json: violations` |

---

## 枚举值：libcurl_consistency（gate_*.json）

> 说明：这些值由 `tests/libcurl_consistency/run_gate.py` 生成并写入 `gate_<suite>.json`。

| code | evidence_severity | quality_signal | 含义（触发条件） | 优先定位工件（建议） |
|---|---|---|---|---|
| `junit_parse_error` | CRITICAL | EVIDENCE | JUnit XML 缺失或无法解析（无法证明“真实执行了哪些用例/是否有 skip”）。 | `junit_<suite>.xml`、`gate_<suite>.json` 的 `junit_counts.parse_error` |
| `no_tests_executed` | CRITICAL | EVIDENCE | JUnit 统计 `tests==0`（“未执行=无证据”）。 | `gate_<suite>.json` 的 `junit_counts.tests` 与 `pytest_*` 输出 |
| `skipped_tests` | CRITICAL | EVIDENCE | JUnit 统计 `skipped>0`（Gate 口径：skip=fail）。 | `junit_<suite>.xml`、`gate_<suite>.json` 的 `junit_counts.skipped` |
| `execution_contract` | CRITICAL | EVIDENCE | 实际执行的用例集合与计划集合不一致（漏跑、多跑或 nodeid 对不上，执行证据不可信）。 | `gate_<suite>.json: execution_contract.violations`、`junit_<suite>.xml` |
| `evidence_integrity` | CRITICAL | EVIDENCE | 证据文件完整性校验失败（缺失、截断或 digest 不匹配）。 | `gate_<suite>.json: evidence_integrity.errors`、`curl/tests/http/gen/artifacts/` |
| `sanitizer_config_failed` | HIGH | INFRA | sanitizer 构建树 CMake 配置失败（工具链缺失或参数不受支持），未能进入构建阶段。 | `sanitizers/report.json: configure`、`logs/sanitizer_configure_*.log` |
| `sanitizer_build_failed` | HIGH | IMPLEMENTATION | sanitizer 构建树编译失败，未能产出可执行 subject。 | `sanitizers/report.json: build`、`logs/sanitizer_build_*.log` |
| `sanitizer_subject_failed` | HIGH | IMPLEMENTATION | 配置与构建均成功，但 ASan/UBSan/TSan subject 执行失败（检出内存、未定义行为或数据竞争）。 | `sanitizers/report.json: subject_returncode`、`logs/sanitizer_subject_*.log` |
| `artifacts_schema` | CRITICAL | EVIDENCE | artifacts schema/version 不符合 `qcurl-lc/artifacts@v1` 或 required 字段缺失（避免字段漂移导致误判）。 | `gate_<suite>.json` 的 `postflight_artifacts_schema_check.violations`、对应 `curl/tests/http/gen/artifacts/**/{baseline,qcurl}.json` |
| `redaction` | CRITICAL | SECURITY | 脱敏扫描发现敏感头明文落盘（Authorization/Cookie 等），必须失败。 | `gate_<suite>.json` 的 `postflight_redaction_scan.violations` |
| `http3_required` | CRITICAL | INFRA | 设置 `QCURL_REQUIRE_HTTP3=1` 但缺失 HTTP/3 能力（服务端或 curl 不支持），Gate 必须失败。 | `gate_<suite>.json` 的 `preflight_http3_required` |
