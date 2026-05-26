# LoggingDemo

LoggingDemo shows the QCurl Core / Stable logger surface in `QCurl 1.0.0 first stable`.

## Build and run

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target LoggingDemo
./build/examples/LoggingDemo/LoggingDemo
```

## Demonstrated behavior

- Default logger setup.
- Console and file output.
- Custom logger implementation.
- Basic log formatting.

## API sketch

```cpp
#include <QCNetworkDefaultLogger.h>
#include <QCNetworkLogger.h>

auto *logger = new QCurl::QCNetworkDefaultLogger();
logger->setMinLogLevel(QCurl::NetworkLogLevel::Info);
logger->enableConsoleOutput(true);
manager.setLogger(logger);
```

Custom loggers override `QCNetworkLogger::log(const NetworkLogEntry &entry)`.

## Related docs

- `docs/arch/1.0-first-stable-release-contract.md`
- `docs/dev/api-docs.md`
