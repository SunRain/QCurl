# NetworkFeaturesDemo

NetworkFeaturesDemo shows selected QCurl network capabilities on the current `1.0.0 first stable` line.

## Surface labels

- HTTP/3: Core / Stable capability, depending on runtime libcurl and server support.
- WebSocket bounded asynchronous send/receive: Other Extras / Preview.
- Diagnostics: Other Extras / Preview.

## Build

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target NetworkFeaturesDemo
```

## Run

```bash
./build/examples/NetworkFeaturesDemo/NetworkFeaturesDemo
```

## Demo menu

1. HTTP/3 request example.
2. WebSocket Preview echo example with explicit buffer and close-timeout limits.
3. Network diagnostics example.
4. Combined run.

## Requirements

- Qt6 Core.
- QCurl built with the relevant example targets.
- HTTP/3 requires libcurl with HTTP/3 / QUIC support and a server that supports HTTP/3.
- WebSocket requires a reachable `ws://` or `wss://` peer and remains outside the Core Stable contract.
- Diagnostics may require network access depending on the selected probe.

## Related docs

- [HTTP 版本配置](../../docs/user/configuration.md#http-version)
- [当前发布合同](../../docs/dev/release/2.0.0-hard-break-release-contract.md)
- [当前版本变化](../../CHANGELOG.md#unreleased)
