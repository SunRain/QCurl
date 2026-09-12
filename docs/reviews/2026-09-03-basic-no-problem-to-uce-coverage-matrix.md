# basic-no-problem 到 UCE 的覆盖矩阵

> 文档用途：记录旧 acceptance workflow 与 UCE 替代合同的逐项映射、自动证明和当前候选状态。
> 本文件是 derived review data，不改变 release identity authority。

## 1. 结论与边界

### 1.1 架构结论

`basic-no-problem` 的长期终局仍应下线，由 UCE 收敛职责。旧 Python runner 与旧
workflow 不保留 wrapper、alias、fallback 或双轨入口。UCE nightly 的额外 p1/p2、DCI、
BP、CTBP、HES 和 netproof 覆盖不能反向掩盖旧 acceptance 的缺口。

### 1.2 当前候选结论

本矩阵修订后，触发、外围检查、异常收口和归档上传的实现已纳入 UCE；但在绑定当前
候选的完整 nightly E2E、全量验证和 authority/文档最终一致性证据写入前，删除方案仍为
`NO-GO / REQUEST_CHANGES`。Git index 中已经出现旧文件删除，不构成替代合同证明。

## 2. 旧 workflow 的完整 acceptance 合同

旧入口是 `HEAD:.github/workflows/basic_no_problem_gate.yml`，不能只比较
`run_basic_no_problem_gate.py` 的三个内部 runner。完整职责如下：

| 旧职责 | 旧实现 | 失败语义 |
| --- | --- | --- |
| 触发 | push 到 `master`/`main`/`develop`，以及 `workflow_dispatch` | 未触发即无 acceptance 证据 |
| 构建前置 | recursive checkout、依赖安装、CMake configure、consistency infrastructure | 任一步骤非零即失败 |
| public API slow | `ctest -L '^public-api-slow$'` | 命令非零即失败 |
| offline QtTest | `ctest_strict.py --label-regex offline --max-skips 0` | skip、无测试或非零即失败 |
| env/httpbin QtTest | 启动 httpbin，解析 env，`ctest_strict.py --label-regex env --max-skips 0` | 启动、env、URL、skip 或非零均失败 |
| libcurl consistency p0 | `run_gate.py --suite p0 --build` | provider 非零或 required report 缺失即失败 |
| capability QtTest | `ctest_strict.py --label-regex capability --max-skips 0` | skip、无测试或非零即失败 |
| 归档 | manifest、日志、test-artifacts、tar.gz | tar 或 required evidence 缺失即失败 |

## 3. UCE 逐项映射

| 旧职责 | UCE 承接位置 | 自动证明 | 当前判定 |
| --- | --- | --- | --- |
| push/manual 触发 | `.github/workflows/uce_nightly.yml` 的 `push`（三分支）与 `workflow_dispatch` | `tests/test_uce_acceptance_contract.py::test_uce_nightly_preserves_acceptance_push_and_manual_triggers` | 已实现，待 workflow 运行证据 |
| offline | `ctest_gates.py::run_offline_ctest_gate()` | `test_offline_listing_failure_cannot_pass`、`test_real_ctest_qttest_skip_is_rejected[offline]` | 已实现并有负向断言 |
| env/httpbin | `httpbin.py::run_httpbin_gate()` + `ctest_gates.py::run_env_ctest_gate()` | `test_uce_httpbin_contract.py`、`test_real_ctest_qttest_skip_is_rejected[env]` | 已实现并有负向断言 |
| consistency p0 | `execute.py::run_libcurl_consistency_gates()` | `test_uce_consistency_acceptance.py` 的 provider/JUnit/report 断言 | 已实现并有负向断言 |
| public-api-slow | nightly/soak `public_api_slow` gate | `test_real_ctest_acceptance_result_is_fail_closed[public-api-slow]` | 已实现并有负向断言 |
| capability skip=fail | nightly/soak `capability` gate，`ctest_strict.py --max-skips 0` | `test_real_ctest_qttest_skip_is_rejected[capability]` | 已实现并有负向断言 |
| manifest/tar/log | `evidence.py` 与 `finalize.py` | `test_uce_failure_evidence.py`、`test_uce_manifest.py`、`test_uce_archive_validation.py` | 已实现并有负向断言 |

UCE nightly 同时增加 p1/p2 和专题 contract。这些是增强覆盖，不是旧 acceptance 的必要
替代理由；最低合同仍由上表全部项目组成。

## 4. 触发与工作流边界

| 入口 | 是否保留 | 说明 |
| --- | --- | --- |
| `push` 三分支 | 是 | 由 UCE nightly 承接完整 acceptance；不能只由 PR workflow 的 pr tier 代替 |
| `workflow_dispatch` | 是 | 手动运行同一 nightly acceptance |
| `schedule` | 是 | UCE nightly 的周期运行是额外保障 |
| PR fast | 是 | 继续提供低延迟 pr tier，不声称它覆盖 public-api-slow/capability |
| soak | 是 | 长跑增强，不替代 push/manual 的 nightly acceptance |

构建、recursive submodule checkout、consistency infrastructure 和依赖安装仍属于
workflow 层合同。只证明 UCE Python runner 成功，不能证明这些外围步骤已经被替代。

`tests/infrastructure/test_consistency_workflows.py` 结构化解析三个 workflow，校验构建、
基础设施门禁、旧触发路径集合、逐文件上传前校验与失败诊断上传。`actionlint` 校验
GitHub Actions 语法。两者证明配置合同，不冒充远端运行或远端持久化上传。

## 5. fail-closed 负向证明矩阵

| 旧失败分支 | UCE policy code / 状态 | 自动断言 |
| --- | --- | --- |
| offline 列表/命令失败、空集合或 skip | `gate_offline_failed` | `test_uce_ctest_gates.py::{test_offline_listing_failure_cannot_pass,test_real_ctest_acceptance_result_is_fail_closed,test_real_ctest_qttest_skip_is_rejected}` |
| httpbin 启动失败 | `env_preflight_httpbin_start_failed` | `test_uce_httpbin_contract.py::test_httpbin_start_failure_is_independent_of_env_failures` |
| httpbin env 缺失/解析失败 | `env_preflight_httpbin_env_missing` / `env_preflight_httpbin_env_parse_error` | `test_httpbin_successful_start_without_env_file_is_rejected`、`test_httpbin_malformed_env_is_rejected_without_running_ctest` |
| URL 缺失 | `env_preflight_httpbin_url_missing` | `test_httpbin_valid_env_without_url_is_rejected` |
| env 列表/命令失败、空集合或 skip | `gate_env_failed` | `test_env_listing_failure_cannot_pass`、真实 CTest 参数化测试的 env 分支 |
| httpbin 停止失败或清理异常 | `env_preflight_httpbin_stop_failed`，异常额外记 `gate_exception` | `test_httpbin_stop_failure_is_independently_rejected`、`test_httpbin_stops_owned_fixture_when_env_command_raises`、`test_cleanup_exception_preserves_the_primary_workload_error` |
| consistency p0 非零 | `gate_libcurl_consistency_p0_failed` | `test_p0_provider_nonzero_is_promoted_to_uce_failure` |
| p0 report/JUnit 各自缺失 | `artifact_required_missing` | `test_p0_success_without_each_required_report_is_rejected`；成功基线 `test_p0_success_requires_the_current_run_reports` |
| p0 JUnit skip/空集合/不可解析 | provider `skipped_tests` / `no_tests_executed` / `junit_parse_error` | `test_p0_junit_skip_and_empty_counts_are_policy_failures`，真实 XML 解析后判定 |
| public-api-slow 无测试/列表失败/命令非零 | `gate_public_api_slow_failed` | `test_acceptance_label_listing_failure_is_fail_closed`、真实 CTest 参数化测试的 public-api-slow 分支 |
| capability skip/无测试/命令非零 | `gate_capability_failed` | `test_real_ctest_qttest_skip_is_rejected`、`test_real_ctest_acceptance_result_is_fail_closed` 的 capability 分支 |
| workload/finalize 未预期异常 | `gate_exception` + `manifest.exception/exceptions` | `test_uce_workload_exception_is_structured_and_archived`、`test_failure_recovery_still_runs_redaction_gate` |
| 异常日志/两份元数据持续写入失败 | `gate_exception`，缺件仍非零 | `test_exception_log_write_failure_still_persists_structured_failure`、`test_metadata_write_failure_preserves_other_metadata_and_diagnostics`、`test_dual_metadata_write_failure_does_not_archive_stale_pass` |
| tar 打包失败 | `packaging_tar_gz_failed` + required archive 缺失 | `test_packaging_failure_retains_required_archive_signal` |
| archive source 读取失败 | `archive_envelope_source_missing` / `archive_envelope_read_failed` | `test_missing_archive_source_is_structured`、`test_archive_envelope_read_failure_is_structured` |
| envelope 写入失败 | `archive_envelope_write_failed` | `test_archive_envelope_write_failure_is_structured`、`test_packaging_failure_archives_the_final_failure_snapshot[envelope]` |
| required artifact 在 finalize 前或打包后消失 | `artifact_required_missing` | `test_required_artifact_deleted_before_validation_is_fail_closed`、`test_packaging_failure_archives_the_final_failure_snapshot[deleted_after_tar]` |
| 包内外状态不一致 | `gate_exception` 或上传前校验非零 | `test_packaged_manifest_matches_on_disk_manifest`、`test_packaging_failure_archives_the_final_failure_snapshot`、`test_packaged_metadata_must_match_external_bytes` |
| 任一必需上传文件单独丢失/空/损坏 | 上传前校验非零 | `test_each_required_upload_file_fails_closed` 的四文件×三故障分支 |
| envelope 身份/摘要不符、tar 必需成员缺失或 gzip 截断 | 上传前校验非零 | `test_envelope_identity_must_match_actual_archive`、`test_archive_must_contain_unambiguous_required_files`、`test_damaged_gzip_trailer_is_rejected_even_with_matching_digest` |
| 失败归档被误当作成功 acceptance | `--require-pass` 非零 | `test_failed_gate_archive_is_diagnostic_and_never_a_passing_acceptance` |
| 复用运行编号 | 创建前返回3，不启动门禁、不触碰旧证据 | `test_reused_run_id_is_rejected_without_touching_prior_evidence` |

覆盖矩阵中的“等价”只有在对应自动断言和证据存在时才成立；人工阅读源码不能单独关闭
任一行。

上述测试由 `tests/CMakeLists.txt` 的 `qcurl_uce_infrastructure` 注册到 offline 阻断路线。
真实 CTest 测试使用临时 CMake 工程和 QtTest 格式输出，调用生产 `ctest_strict.py`，
验证普通 CTest 的 skip 成功如何被转换为严格失败；不是用模拟返回码声称 skip 被拒绝。
`ctest -N --no-tests=error` 对空集合仍可返回0，空集合断言依赖真实执行阶段。
provider 非零和 I/O 故障采用隔离注入；真实网络/provider 成功由完整 nightly 证明。

## 6. 归档合同

UCE 成功运行必须持久化以下包外文件：

```text
build-*/evidence/uce/<run-id>/manifest.json
build-*/evidence/uce/<run-id>/policy_violations.json
build-*/evidence/uce/<run-id>.tar.gz
build-*/evidence/uce/<run-id>.archive-envelope.json
```

`manifest.artifacts` 只登记 `evidence_dir` 内部文件；tar 的路径、字节数和 sha256 由
包外 `qcurl-uce/archive-envelope@v1` 承载。GitHub Actions 的 nightly、soak 和 PR 上传
步骤均显式包含 envelope；上传前执行 `verify_uce_archive.py --require-pass`，逐个验证
四文件、完整 gzip、包内必需成员和元数据字节。`if-no-files-found: error` 仅防止整组
无匹配，并不能阻断单个文件缺失；此漏洞由独立校验器闭合。校验与上传都用 `always()`，
校验失败不能阻止保留其余诊断。失败归档只提供排障证据，不是成功验收。

## 7. 当前候选证据门槛

必须在当前 `master-tmp` 候选上重新生成并保存：

1. 当前 HEAD SHA、完整 index 条目、staged/unstaged patch、untracked 内容/可执行位、
   递归子模块与唯一正式发布合同摘要；排除构建/本次输出，避免证据自引用；
2. 完整 UCE nightly manifest，`result=pass` 且 `policy_violations=[]`；
3. manifest 中每个 required artifact 的存在性和内容摘要；
4. tar 与 envelope 的独立 sha256/字节数校验；
5. Python 全量、CMake 构建、C++ 全量 CTest、policy dictionary validator 输出。

历史候选 `c294fb18f97411d89593b5c9f457deaaef3a0f12`、历史 manifest、普通组件测试和
独立 release gate 都不能替代以上当前候选证明。

六树合同属于 release 路由：`release-shared`、`release-static`、`test-shared-gcc`、
`test-shared-clang`、`asan-ubsan-lsan`、`tsan` 必须分别提供真实 provenance，不能把
一个 nightly 构建树声称为“六树全覆盖”，HTTP3/external 也不是六树中的独立树。本次旧
acceptance 替代只使用明确的 `build-uce-acceptance` 树；不改变六树要求、不执行 release
或 baseline promotion。远端 GitHub Actions 的实际触发、sanitizer job 与持久化上传
没有在本地发生；本地测试、配置校验与归档校验不能冒充这些远端结果。

## 8. 删除与收尾判定

### 8.1 允许删除

以下条件全部满足后，删除旧文件是 hard-breaking 允许的终局动作：

- UCE nightly 触发、检查、异常、负向证明和归档合同均已闭合；
- 当前候选 fresh nightly E2E 通过并绑定 fingerprint；
- active requirements/plan/contract、release contract、代码、测试和文档一致；
- 无旧 runner/workflow 的生产引用、wrapper、alias、fallback 或双轨 CI。

### 8.2 当前状态

在第 7 节证据尚未写入前，状态保持 `NO-GO / REQUEST_CHANGES`。完成实现测试不等于
完成当前候选验收；完成当前候选验收也不授权 Git commit、tag、push 或 release。

## 9. 终局

闭合后只保留 UCE 单一路线：UCE nightly 在 push/manual/schedule 上执行完整 acceptance，
PR/soak 提供各自层级的增强证据，旧 runner/workflow 及其专属引用删除，不保留兼容入口。
