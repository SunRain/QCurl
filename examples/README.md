# QCurl examples

This directory contains examples for the current QCurl 2.1.0 development candidate. Build and run them against the same candidate; an example build does not establish release qualification.

## Surface labels

- Core / Stable: examples that use the default `QCurl::QCurl` install surface.
- Blocking Extras / explicit opt-in: synchronous examples; implementation is embedded in Core.
- Test Support / explicit opt-in: examples or fixtures for tests only.
- Other Extras / explicit opt-in: WebSocket and Diagnostics are Preview; Middleware Extras is Stable.

## Build

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --parallel
```

Run examples from `build/examples/<ExampleName>/` after building.

## Core / Stable examples

- `CanonicalRequestDemo` — canonical `QCNetworkRequest + QCNetworkAccessManager::send*()` flow.
- `SchedulerDemo` — lane-aware scheduler and request priority behavior.
- `FileTransferDemo` — stream download/upload and resumable download flow.
- `FileDownloadDemo` — basic file download manager.
- `BatchRequestDemo` — batch request coordination.
- `ApiClientDemo` — simple REST API client wrapper.
- `Http2Demo` — HTTP/2 capability use when libcurl supports it.
- `Http3Demo` — HTTP/3 capability use when libcurl and the server support it.
- `ProxyDemo` — proxy configuration.
- `StressTest` — scheduler and request stress exercise.
- `QCurl` — Qt Widgets demo for basic requests.

## Other Extras / Preview examples

- `WebSocketDemo`
- `WebSocketPoolDemo`
- `NetworkFeaturesDemo` diagnostics and WebSocket sections

These examples require Other Extras explicitly. Preview denotes maturity; its published Public APIs receive the same backward compatibility protection within a major.minor patch line. Cross-minor compatibility is not promised. See the [versioning policy](../docs/dev/release/versioning-and-compatibility.md).

## Documentation rules

Keep example README files focused on current usage, build steps, runtime requirements, and surface labels.
Do not add old feature-version labels, stale update dates, fixed pass-rate claims, or one-off release history.
