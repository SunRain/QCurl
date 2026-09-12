# QCurl documentation

This documentation describes the latest published `QCurl 1.0.0` and the current `QCurl 2.0.0` release candidate.

## Public entrypoints

- User guide: `docs/user/README.md`
- Quick start: `docs/user/quickstart.md`
- Configuration: `docs/user/configuration.md`
- Flow control: `docs/user/flow-control.md`
- Lane scheduler: `docs/user/lane-scheduler.md`
- Build and test: `docs/dev/build-and-test.md`
- Current release contract: `docs/arch/2.0.0-hard-break-release-contract.md`
- Current release notes: `docs/arch/2.0.0-release-notes.md`
- Migration guide: `docs/arch/2.0.0-migration-guide.md`
- Release contract: `docs/arch/1.0-first-stable-release-contract.md`
- Release notes: `docs/arch/1.0.0-release-notes.md`
- Readiness report: `docs/arch/1.0-first-stable-readiness-report.md`
- Release procedure: `docs/dev/release-procedure.md`
- Future stable ABI project: `docs/roadmap/stable-abi-contract-and-baseline.md`

## Maintainer reference

- Architecture index: `docs/arch/README.md`
- Historical 2.0 comprehensive review (its ABI-baseline conclusion is superseded by the current release contract): `docs/reviews/2026-08-05-qcurl-2.0.0-comprehensive-readonly-review-conclusion.md`
- Current Qt6/C++17 source review: `docs/reviews/2026-08-12-qcurl-qt6-cpp17-current-src-review-comprehensive-conclusion.md`
- Current tests and libcurl consistency review: `docs/reviews/2026-08-15-qcurl-tests-libcurl-consistency-review-conclusion.md`
- Current libcurl consistency remediation WIP review: `docs/reviews/2026-08-17-qcurl-libcurl-consistency-remediation-wip-comprehensive-readonly-review-conclusion.md`
- Current overdesign / compatibility-layer / workaround review (`49c2276`): `docs/reviews/2026-08-31-qcurl-overdesign-compat-workaround-readonly-review-conclusion.md`
- **P1+P2+P3 cleanup & contract revision report**: `docs/reviews/2026-09-03-cleanup-p1-p2-p3-execution-report.md` — Executed P1-1, P1-2, P2-1, P2-2, P2-3, P3-1~P3-5 cleanup, fixed three false-green gates, removed five compatibility stubs, revised release authority handling with fail-loud + explicit declaration, verification passed
- **P2-C/P2-B/P3 followup execution**: `docs/reviews/2026-09-03-cleanup-p1-p2-p3-followup-execution.md` — Completed P2-C (gate coverage for examples/benchmarks), P2-B (envelope failure path & exception handling), P3 (five low-risk renames & conftest cleanup), verification passed
- **P3-6/RawHeaderPair/conftest final cleanup (historical snapshot)**: `docs/reviews/2026-09-03-cleanup-p3-6-final-execution.md` — Records the 2026-09-03 partial execution. Its P3-6 decommission conclusion is superseded by the current conditional UCE acceptance contract; fresh current-candidate evidence is still required before deletion is submit-ready.
- Historical Qt6/libcurl lifecycle review (`a1bafb7`): `docs/reviews/2026-08-07-qcurl-qt6-libcurl-lifecycle-review-comprehensive-conclusion.md`
- Developer docs: `docs/dev/README.md`
- Supply-chain notes: `docs/dev/supply-chain.md`
- Reference docs: `docs/reference/README.md`
- Gate contract: `docs/test_gate.md`
- UCE evidence contract: `docs/uce/README.md`
- Architecture overview: `SYSTEM_DOCUMENTATION.md`

## Internal history

Pre-1.0 changelog history and old RC / 3.0 / incompatible-change documents live under `docs/internal/`.
They are kept for audit/reference and should not be used as current public release guidance.
