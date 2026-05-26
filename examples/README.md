# QCurl examples

This directory contains examples for the `QCurl 1.0.0 first stable` line.

## Surface labels

- Core / Stable: examples that use the default `QCurl::QCurl` install surface.
- Blocking Extras / package-shipped non-default Core: examples that require explicit Blocking Extras components.
- Test Support / explicit opt-in: examples or fixtures for tests only.
- Other Extras / Preview: WebSocket, Diagnostics, Middleware Extras, and other non-default surfaces.

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
- `WebSocketCompressionDemo`
- `NetworkFeaturesDemo` diagnostics and WebSocket sections

Preview examples can be built and packaged, but they are not part of the default Core Stable contract.

## Documentation rules

Keep example README files focused on current usage, build steps, runtime requirements, and surface labels.
Do not add old feature-version labels, stale update dates, fixed pass-rate claims, or one-off release history.
