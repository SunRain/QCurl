# QCurl Architecture overview

This document is a maintainer-facing overview for the current `QCurl 2.0.0` candidate.
It is intentionally short. Public usage guidance lives in `README.md` and `docs/user/`.

## Release identity

- Version: `2.0.0`
- Shared library loader namespace: `SOVERSION 2`
- ABI contract: non-stable; every downstream update requires rebuild and relink
- Core ABI baseline: none for 2.0
- Default library artifact: `libQCurl.so.2.0.0`
- Current release narrative: source-compatible Core, rebuild-required ABI

## Source-compatible scope

QCurl 2.0 stabilizes the Core component install surface at source level within 2.x.
Core is the default `find_package(QCurl)` / `QCurl::QCurl` consumer contract.
It covers the installed headers listed by `QCURL_INSTALL_HEADERS` plus generated `QCurlConfig.h`.
It does not promise binary compatibility between QCurl 2.x builds. `SOVERSION 2` is the current
loader namespace, and downstream consumers must rebuild and relink after every QCurl update.

Source-compatible Core includes:

- `QCNetworkAccessManager`
- `QCNetworkRequest`
- `QCNetworkReply`
- HTTP method, version, error, priority, and cache-policy types
- TLS, proxy, timeout, retry, redirect, and transfer configuration
- lane-aware scheduler
- cache lookup API
- multipart and body helpers
- download / transfer jobs
- logger, default logger, cancel token, and middleware base
- connection-pool configuration and management surface

## Opt-in consumer surfaces

QCurl has exactly four logical delivery components: Core, Blocking Extras, Other Extras, and Test Support.
Preview is a maturity attribute and Internal is a visibility attribute; neither creates another
target or package layer.

- Core is the default consumer target and exports `QCurl::QCurl` from a shared/static runtime library.
- Blocking Extras is an opt-in `INTERFACE` consumer target exported as `QCurl::BlockingExtras`;
  its implementation and public symbols are carried by the Core runtime library.
- Other Extras is an opt-in shared/static runtime library and exports `QCurl::OtherExtras`.
- Test Support is an opt-in development static library and exports `QCurl::TestSupport`; it is not
  a production runtime capability.

The physical package therefore contains three libraries: Core plus embedded Blocking Extras,
Other Extras, and static Test Support. There is no independent `libQCurlBlockingExtras` artifact.

Diagnostics and WebSocket are currently Preview APIs inside Other Extras. Middleware Extras is a
public Stable API inside the same component. Do not describe any non-Core component as part of the
default source-compatible Core contract.

## Main modules

`src/` contains the library implementation and installed public headers.
Private implementation details live in `_p.h` headers or `src/private/` and must not leak into the Core component install surface.

`tests/public_api/` protects install/export behavior and consumer contracts.
It is the first place to check when changing public headers, CMake exports, or component boundaries.

`tests/qcurl/` contains QtTest coverage for runtime behavior.
Use focused tests during development and release gates for final evidence.

`tests/libcurl_consistency/` validates behavior against libcurl and local fixtures.
It is required for full release confidence and protocol/capability evidence.

`examples/` demonstrates user-facing usage.
Examples must state their delivery component; Preview APIs must additionally state their maturity.

`docs/` is split into public docs, maintainer reference, and internal history.
Old pre-1.0 narrative belongs under `docs/internal/`.

## Request path

A typical async request starts with `QCNetworkRequest`.
The manager normalizes request state, applies policy and options, then creates a `QCNetworkReply`.
If scheduling is enabled, the reply is queued through `QCNetworkRequestScheduler`.
The curl multi owner drives the transfer and emits reply state, progress, headers, body, and completion signals.

The reply object is owner-thread bound.
Cross-thread operations must use explicit async dispatch or documented thread-safe entrypoints.
Do not add transparent blocking cross-thread getters to Core.

## Flow-control path

User-visible transport pause/resume is reply-level Core behavior.
It is not the same as scheduler defer/undefer.
Scheduler defer only changes pending scheduling state; it does not preserve an in-flight transfer.

Download backpressure and upload source pause are internal flow-control mechanisms.
They may expose diagnostics, but they must not be confused with user `ReplyState::Paused`.
The maintainer contract is `docs/arch/transport-pause-resume.md`.

## Blocking path

Blocking Extras must use value results and configuration snapshots.
They must not borrow live manager state across threads.
Blocking APIs should fail fast on unsafe main-thread or owner-thread scenarios unless the contract explicitly allows them.

Blocking cookie operations use snapshots or deltas.
They do not directly access the live Core cookie store.

## Public header boundary

The Core public surface must not include private headers, test hooks, or preview-only headers.
Installed headers should minimize transitive dependencies.
Pimpl and shared-data value types should hide implementation details while preserving normal value semantics.

The boundary source of truth is `docs/arch/public-header-boundary.md` and `src/CMakeLists.txt`.

## Build entrypoints

Release configure/build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
```

Static Core validation uses a separate `build-static` directory with `QCURL_BUILD_SHARED_LIBS=OFF`.

## Test and release gates

Core validation gates:

```bash
ctest --test-dir build -L '^public-api$' --output-on-failure
ctest --test-dir build -L '^public-api-slow$' --output-on-failure
python3 scripts/run_release_gate.py --tier full --abi-mode none \
  --release-shared-build-dir build-release-shared \
  --release-static-build-dir build-release-static \
  --test-shared-gcc-build-dir build-test-shared-gcc \
  --test-shared-clang-build-dir build-test-shared-clang \
  --asan-ubsan-lsan-build-dir build-asan-ubsan-lsan \
  --tsan-build-dir build-tsan
python3 scripts/run_release_gate.py --scan-metadata \
  --release-shared-build-dir build-release-shared
git diff --check
```

## Documentation policy

Public documentation must distinguish the published `1.0.0` history from the current `2.0.0`
source-compatible, rebuild-required candidate.
Do not reintroduce old pre-1.0 version, RC, or date-based development narrative.

`CHANGELOG.md` is the public release history.
`docs/internal/pre-1.0-history.md` is the maintainer history archive.
`docs/internal/archived-release/` stores old RC, 3.0, hard-break review, and task-log documents.

## Maintenance rules

When changing public headers, update public-api checks and release contract docs.
When changing CMake exports or components, run both shared and static public-api gates.
When changing flow control, update `docs/user/flow-control.md` and `docs/arch/transport-pause-resume.md`.
When changing gates, update `docs/test_gate.md` and UCE docs if evidence semantics change.
When changing examples, keep surface labels explicit and avoid stale performance or pass-rate claims.
