# QCurl Changelog

本文只记录公开 `1.0.0 first stable` 发布线。pre-1.0 日期流水、RC 草稿和旧 hard-break 过程已移入 `docs/internal/pre-1.0-history.md` 索引和内部 raw archive，不再作为当前用户-facing release history。

正式 tag 与 GitHub Release 尚未创建前，本文件使用 `Unreleased` 承接候选变更；发布流程完成后再改为 `## [1.0.0] - <release-date>`。

## [Unreleased]

### Changed
- 将 QCurl 当前对外发布身份重置为 `1.0.0 first stable`，并对齐 `PROJECT_VERSION=1.0.0`、`SOVERSION=1`、`qcurl-core-v1` 与 `libQCurl.so.1.0.0`。
- Core shared/static 是本次唯一 Stable 承诺；Blocking Extras 为随包发布但非默认 Core，Test Support 为显式 opt-in，WebSocket、Diagnostics 和 Other Extras 保持 Preview。
- 旧 pre-1.0、RC 与历史 hard-break 记录只作为内部参考，不再作为当前用户-facing release history 主线。

### Breaking
- 这是 QCurl 首个 stable line，允许 hard-break；不提供旧 pre-1.0 兼容层、alias、wrapper 或 ABI shim。

### Testing
- 当前 release gate 以 `scripts/run_release_gate.py --tier full --build-dir build --static-build-dir build-static`、v1 ABI baseline / diff 和 `git diff --check` 为准。
