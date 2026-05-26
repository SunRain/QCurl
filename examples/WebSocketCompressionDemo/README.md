# WebSocketCompressionDemo

WebSocketCompressionDemo demonstrates RFC 7692 permessage-deflate support.
This example belongs to Other Extras / Preview and is not part of the default Core Stable contract.

## Build

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target WebSocketCompressionDemo
```

## Run

```bash
./build/examples/WebSocketCompressionDemo/WebSocketCompressionDemo 1
./build/examples/WebSocketCompressionDemo/WebSocketCompressionDemo 2
./build/examples/WebSocketCompressionDemo/WebSocketCompressionDemo 3
```

## Demos

1. Compare compressed and uncompressed echo messages.
2. Try preset compression configurations.
3. Send larger messages and inspect negotiated compression statistics.

## Requirements

- Qt6 Core.
- QCurl Other Extras / Preview WebSocket support.
- libcurl with WebSocket support.
- zlib.
- A WebSocket peer that supports permessage-deflate.

## Related docs

- `docs/arch/1.0-first-stable-release-contract.md`
- RFC 7692: https://www.rfc-editor.org/rfc/rfc7692
