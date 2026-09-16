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

QCurl::QCNetworkDefaultLogger *implementation = nullptr;
auto logger = QCurl::QCNetworkLoggerHandle::createWithBorrow(&implementation);
implementation->setMinLogLevel(QCurl::NetworkLogLevel::Info);
implementation->enableConsoleOutput(true);
manager.setLogger(logger);
```

Custom loggers override the `[[nodiscard]] QCNetworkLogResult
QCNetworkLogger::log(const NetworkLogEntry &entry)` contract.

## Related docs

- [当前发布合同](../../docs/dev/release/2.0.0-hard-break-release-contract.md)
- `docs/dev/api-docs.md`
