# NetworkFeaturesDemo

NetworkFeaturesDemo shows selected QCurl network capabilities on the current `1.0.0 first stable` line.

## Surface labels

- HTTP/3: Core / Stable capability, depending on runtime libcurl and server support.
- WebSocket compression: Other Extras / Preview.
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
2. WebSocket compression example.
3. Network diagnostics example.
4. Combined run.

## Requirements

- Qt6 Core.
- QCurl built with the relevant example targets.
- HTTP/3 requires libcurl with HTTP/3 / QUIC support and a server that supports HTTP/3.
- WebSocket compression requires WebSocket support and permessage-deflate support on the peer.
- Diagnostics may require network access depending on the selected probe.

## Related docs

- `docs/reference/http3.md`
- `docs/arch/1.0-first-stable-release-contract.md`
- `docs/arch/1.0.0-release-notes.md`
