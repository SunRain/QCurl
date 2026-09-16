# 历史文档索引

维护者从此处追溯旧发布、审查、任务和 ABI 证据。历史中的“当前”、PASS/NO-GO、版本名和待办只适用于原始时点，不能覆盖[现行发布合同](../release/2.0.0-hard-break-release-contract.md)或替代新的候选验收。

## 时期索引

| 时期 | 内容与去向 | 阅读边界 |
| --- | --- | --- |
| 早期 pre-1.0 开发 | [原始 history](pre-1.0/pre-1.0-history-raw.md)：日期流水、功能/迁移与旧 benchmark 叙事 | 原文保留，不将旧宣传数字复制回当前 README |
| RC / 3.0 草稿 | 原始 history 与下面的历史审查/任务证据 | 旧身份不是当前版本；仅供历史追溯 |
| 1.0.0 first stable | [发布合同](1.0/first-stable-release-contract.md)、[readiness snapshot](1.0/first-stable-readiness-report.md)、[release notes](1.0/1.0.0-release-notes.md) | 1.0 是已发布历史，snapshot 保留其自身日期与适用限制，不反推 tag 当时或 2.0 通过 |
| 2.0 开发与整改 | 下表的 dated review / 执行记录，以及 [2026-08-19 发布资格快照](reviews/2026-08-19-qcurl-2.0.0-release-readiness-snapshot.md) | 各自候选独立，不拼接不同基准为一次综合 PASS |
| 发布前 ABI/任务/状态 | 下方专题证据 | 旧统计没有 commit 绑定时保留该限制，不用收录 HEAD 补造身份 |

## 专题证据与 raw 入口

- [pre-1.0 原始长流水](pre-1.0/pre-1.0-history-raw.md)。
- [first-stable hard-break 最终审查](topics/first-stable-hard-break-final-review.md)。
- [typed lane 决策](topics/lane-scheduler-typed-lane-recommendation.md)。
- [libcurl binding ABI 对比证据](topics/libcurl-binding-abi-comparison-evidence.md)。
- [传输 pause/resume 旧任务](topics/transport-pause-resume-tasks.md)。
- [libcurl consistency 旧状态板](topics/libcurl-consistency.md)：只读历史，不再作为任务输出或更新目标。

## Dated review 与执行记录

以下 15 份文档保留独立正文、时点与结论；标题中的“current/final”也仅是当时语境。

| 日期 | 原记录 |
| --- | --- |
| 2026-07-24 | [QCurl 未提交代码审查最终综合结论](reviews/2026-07-24-qcurl-uncommitted-code-style-review-conclusion.md) |
| 2026-07-26 | [commit `54f0fc144eb27264cb8d58a5da24c99ab9d429b5` 成员命名审核与逐代码复核最终结论](reviews/2026-07-26-qcurl-54f0fc-member-naming-review-final-conclusion.md) |
| 2026-07-27 | [QCurl 最终综合架构审核结论](reviews/2026-07-27-qcurl-final-integrated-architecture-review-conclusion.md) |
| 2026-07-31 | [QCurl 企业级 Qt6/libcurl 工程审核结论：发布阻断与唯一修订路线](reviews/2026-07-31-qcurl-engineering-review-release-blockers.md) |
| 2026-08-02 | [QCurl 2.0.0 只读发布审查完整结论](reviews/2026-08-02-qcurl-2.0.0-readonly-release-review-conclusion.md) |
| 2026-08-05 | [QCurl 2.0.0 综合只读工程审查结论](reviews/2026-08-05-qcurl-2.0.0-comprehensive-readonly-review-conclusion.md) |
| 2026-08-07 | [QCurl Qt6/libcurl 生命周期完整审核结论](reviews/2026-08-07-qcurl-qt6-libcurl-lifecycle-review-comprehensive-conclusion.md) |
| 2026-08-12 | [QCurl 当前 `src/` Qt6/C++17 规范完整审核结论](reviews/2026-08-12-qcurl-qt6-cpp17-current-src-review-comprehensive-conclusion.md) |
| 2026-08-15 | [QCurl 测试与 libcurl 外部可观测一致性审查结论](reviews/2026-08-15-qcurl-tests-libcurl-consistency-review-conclusion.md) |
| 2026-08-17 | [QCurl 未提交一致性整改代码综合只读审核结论](reviews/2026-08-17-qcurl-libcurl-consistency-remediation-wip-comprehensive-readonly-review-conclusion.md) |
| 2026-08-31 | [QCurl 过度设计 / 兼容层 / workaround 综合只读审查结论](reviews/2026-08-31-qcurl-overdesign-compat-workaround-readonly-review-conclusion.md) |
| 2026-09-03 | [basic-no-problem 到 UCE 的覆盖矩阵](reviews/2026-09-03-basic-no-problem-to-uce-coverage-matrix.md) |
| 2026-09-03 | [P1+P2+P3 兼容层清理与合同修订执行报告](reviews/2026-09-03-cleanup-p1-p2-p3-execution-report.md) |
| 2026-09-03 | [P2-C、P2-B、P3 后续修复执行报告](reviews/2026-09-03-cleanup-p1-p2-p3-followup-execution.md) |
| 2026-09-03 | [P3-6 + RawHeaderPair + conftest 最终清理执行报告](reviews/2026-09-03-cleanup-p3-6-final-execution.md) |

## 查找与维护规则

- 查旧版本、日期、RC/不兼容背景或任务名：先查 raw history，再查对应时期与专题。不要从一个后续 PASS 推翻或补齐另一基准的缺失证据。
- 当前公共安装面看[公共头边界](../architecture/public-header-boundary.md)和[surface manifest](../../../tests/public_api/surface_manifest.json)，当前命令看[构建与测试](../build-and-test.md)、[发布操作](../release/release-procedure.md)、[UCE](../uce/README.md)。
- 索引不继续追加逐日流水；有独立追溯价值的材料放在已有 raw/对应历史正文，保持原始基准和结论，不为每个删除段落另建档案。
- 历史事实中的旧代码路径可以原样保留；可点击链接须可达，无法恢复的原附件路径标为历史文本，不让它冒充当前文件。
- 回到[文档目录](../../README.md)进入当前用户或维护者正文。
