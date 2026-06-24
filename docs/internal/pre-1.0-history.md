# Pre-1.0 history index

本文是 QCurl pre-1.0、RC 与不兼容变更历史材料的内部索引。它只帮助维护者查找旧记录，不定义当前 `1.0.0 first stable` 发布合同。

当前发布合同请看：

- `../arch/1.0-first-stable-release-contract.md`
- `../arch/1.0.0-release-notes.md`
- `../arch/1.0-first-stable-readiness-report.md`
- `../arch/public-header-boundary.md`

## Raw archive

原始长流水已移动到：

- `archived-release/pre-1.0-history-raw.md`
- `archived-release/libcurl-binding-abi-comparison-evidence.md`
- `archived-release/lane-scheduler-typed-lane-recommendation.md`

该 raw archive 保留旧日期记录、迁移日志、RC 草稿和施工语气，便于审计与检索。公开入口不应要求新用户阅读它。

## 查找方式

- 查旧 release identity、RC 或不兼容变更背景：搜索 raw archive 中的版本号、日期或任务名。
- 查当前 public/install surface：优先看 `../arch/public-header-boundary.md` 与 `../../tests/public_api/surface_manifest.json`。
- 查当前 release gate：优先看 `../dev/build-and-test.md`、`../dev/release-procedure.md` 与 `../../scripts/run_release_gate.py`。
- 查测试证据和 label 口径：优先看 `../../tests/README.md` 与 `../test_gate.md`。

## 关键时期索引

| 时期 | 内容 | 当前处理方式 |
| --- | --- | --- |
| 早期 pre-1.0 开发 | 原始功能清单、旧 benchmark 表述、历史 API 叙事 | 仅保留在 raw archive；不作为当前 README 或 changelog 文案来源 |
| RC / 3.0 草稿 | 旧 RC release notes、migration guide、readiness 草稿 | 已移入 `archived-release/`，只作维护者参考 |
| 1.0.0 first stable 身份重置 | Core shared/static Stable、Blocking Extras 随包非默认、Test Support opt-in、WebSocket/Diagnostics Preview | 当前合同以 `../arch/1.0-*` 与 public API manifest 为准 |
| 发布前门禁与证据 | 本地 gate、ABI、public-api、metadata scan、CI 候选证据 | 当前流程以 `../dev/release-procedure.md` 为 SSOT |

## 维护规则

- 不在本索引继续追加逐日流水。
- 不把 raw archive 中的旧宣传数字、旧版本身份或施工结论复制回公开文档。
- 需要补充历史证据时，追加到 raw archive 或更具体的 internal maintainer 文档，并保持本索引只做导航。
