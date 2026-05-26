# Http3Demo

Http3Demo demonstrates QCurl HTTP/3 capability on the `1.0.0 first stable` line.
HTTP/3 is a Core capability when the runtime libcurl and target server support it.

## Build

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target Http3Demo
```

## Run

```bash
./build/examples/Http3Demo/Http3Demo
./build/examples/Http3Demo/Http3Demo 4
```

## Demos

1. Basic HTTP/3 request.
2. HTTP/3 fallback behavior.
3. `Http3Only` mode.
4. HTTP/1.1, HTTP/2, and HTTP/3 comparison for the selected environment.
5. Automatic HTTP version negotiation.

## Requirements

- Qt6 Core.
- QCurl Core.
- libcurl with HTTP/3 / QUIC support for HTTP/3-specific paths.
- A server and network path that allow HTTP/3.

## Notes

Performance output is local evidence for the current environment only. Do not copy one-off timing numbers into public release claims.

## Related docs

- `docs/reference/http3.md`
- `docs/arch/1.0-first-stable-release-contract.md`
