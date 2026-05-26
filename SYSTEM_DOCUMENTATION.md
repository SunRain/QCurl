# QCurl Architecture overview

This document is a maintainer-facing overview for `QCurl 1.0.0 first stable`.
It is intentionally short. Public usage guidance lives in `README.md` and `docs/user/`.

## Release identity

- Version: `1.0.0`
- Shared library ABI: `SOVERSION 1`
- Core ABI baseline: `abi/baseline/qcurl-core-v1.abi.xml`
- Default library artifact: `libQCurl.so.1.0.0`
- Current release narrative: first stable Core release

## Stable scope

QCurl 1.0.0 stabilizes the default Core install surface.
Core is the default `find_package(QCurl)` / `QCurl::QCurl` consumer contract.
It covers the installed headers listed by `QCURL_INSTALL_HEADERS` plus generated `QCurlConfig.h`.

Core / Stable includes:

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

## Non-default surfaces

Blocking Extras are package-shipped but not default Core.
They provide synchronous value-result utilities and must remain explicit opt-in.

Test Support is explicit opt-in.
It supports tests and fixtures, not production runtime capability claims.

Other Extras remain Preview.
This includes WebSocket, Diagnostics, and Middleware Extras unless a later contract promotes them.
Do not describe these surfaces as Core Stable in public docs.

## Main modules

`src/` contains the library implementation and installed public headers.
Private implementation details live in `_p.h` headers or `src/private/` and must not leak into the default install surface.

`tests/public_api/` protects install/export behavior and consumer contracts.
It is the first place to check when changing public headers, CMake exports, or component boundaries.

`tests/qcurl/` contains QtTest coverage for runtime behavior.
Use focused tests during development and release gates for final evidence.

`tests/libcurl_consistency/` validates behavior against libcurl and local fixtures.
It is required for full release confidence and protocol/capability evidence.

`examples/` demonstrates user-facing usage.
Examples must state whether they use Core, Blocking Extras, Test Support, or Preview surfaces.

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

Default Core must not include private headers, test hooks, or preview-only headers.
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
python3 scripts/run_release_gate.py --tier full --build-dir build --static-build-dir build-static
python3 scripts/run_release_gate.py --scan-metadata --build-dir build
git diff --check
```

## Documentation policy

Public documentation must describe only the current `1.0.0 first stable` line.
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