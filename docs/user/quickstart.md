# 快速开始

本页给出从源码构建 QCurl、安装到 staging prefix，并在独立 consumer 项目中使用 Core API 的最小路径。

## 1. 依赖

- CMake 3.16+
- Qt6（Core consumer 需要 QtCore；Other Extras diagnostics 需要 QtNetwork）
- libcurl 7.85.0+（WebSocket 需 7.86.0+；HTTP/3 推荐 8.16.0+ 且带 QUIC backend）
- 编译器：GCC 11+ / Clang 14+ / MSVC 2019+（C++17）

## 2. 构建并安装到 staging prefix

```bash
git clone https://github.com/SunRain/QCurl.git
cd QCurl
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
cmake --install build --prefix "$PWD/stage"
```

`stage/` 是本地安装前缀，可用于验证 `find_package(QCurl CONFIG REQUIRED)` 和 `pkg-config qcurl`。上述无 `--component` 命令会安装四个逻辑消费面及三个物理库；无组件 `find_package` 仍只加载默认 consumer target `QCurl::QCurl`。需要系统级安装时再选择合适的系统 prefix，不建议在快速开始里默认使用 `sudo`。

测试运行与门禁（offline / env / libcurl_consistency / release gate）统一参考：

- [`docs/dev/build-and-test.md`](../dev/build-and-test.md)

## 3. 独立 consumer 项目

目录结构：

```text
consumer/
├── CMakeLists.txt
└── main.cpp
```

`CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.16)
project(qcurl_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(QCurl CONFIG REQUIRED)

add_executable(qcurl_consumer main.cpp)
target_link_libraries(qcurl_consumer PRIVATE Qt6::Core QCurl::QCurl)
```

`main.cpp`：

```cpp
#include <QCNetworkAccessManager.h>
#include <QCNetworkReply.h>
#include <QCNetworkRequest.h>

#include <QCoreApplication>
#include <QDebug>
#include <QString>
#include <QTimer>

#include <chrono>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCurl::QCNetworkAccessManager manager;
    QCurl::QCNetworkRequest request(QUrl(QStringLiteral("https://example.com")));
    request.setTimeout(std::chrono::seconds(30));

    auto *reply = manager.get(request);
    QObject::connect(reply, &QCurl::QCNetworkReply::finished, &app, [&app, reply]() {
        qDebug() << "error:" << static_cast<int>(reply->error());
        qDebug() << "HTTP:" << reply->httpStatusCode();
        qDebug() << "libcurl:" << reply->diagnosticCurlCode();
        if (const auto body = reply->readAll(); body.has_value()) {
            qDebug() << "bytes:" << body->size();
        }
        reply->deleteLater();
        app.quit();
    });

    QTimer::singleShot(std::chrono::seconds(60), &app, &QCoreApplication::quit);
    return app.exec();
}
```

`diagnosticCurlCode()` 是最终结果对应的实际 libcurl 传输返回码，不代替高层错误或 HTTP
状态。`0` 既可能表示 `CURLE_OK`，也可能表示没有适用的传输结果，例如缓存、模拟结果、
执行前拒绝或直接取消；完整收到 HTTP 404 也可能返回 `0`，不能仅凭此值判断成功。
非终态返回 `0`，重试只保留最终尝试的值；回调取消或设备错误已经取得原生码时仍保留该码。
值在首次终态通知前确定，只能在 reply 所属线程读取；复制出的整数可在 reply 销毁后保留。
Blocking Extras 的 `QCBlockingNetworkResult::diagnosticCurlCode()` 使用同一含义，
但不同取消机制不保证产生相同数值。

构建 consumer：

```bash
cmake -S consumer -B consumer-build -DCMAKE_PREFIX_PATH=/path/to/QCurl/stage
cmake --build consumer-build
./consumer-build/qcurl_consumer
```

## 4. 可选组件

默认 `find_package(QCurl CONFIG REQUIRED)` 只承诺 Core Stable。非默认组件需要显式 opt-in。

### Blocking Extras

```cmake
find_package(QCurl CONFIG REQUIRED COMPONENTS BlockingExtras)
target_link_libraries(your_app PRIVATE QCurl::BlockingExtras)
```

### Test Support

```cmake
find_package(QCurl CONFIG REQUIRED COMPONENTS TestSupport)
target_link_libraries(your_tests PRIVATE QCurl::TestSupport)
```

### Other Extras

```cmake
find_package(QCurl CONFIG REQUIRED COMPONENTS OtherExtras)
target_link_libraries(your_app PRIVATE QCurl::OtherExtras)
```

Other Extras 包含 Diagnostics、Middleware Extras、WebSocket 等非默认能力；它们可随包发布，但不属于 `2.0.0` 默认 Core 承诺。Diagnostics 与 WebSocket 标记为 Preview，Middleware Extras 标记为 Stable；Preview 是成熟度，不是独立组件。

Test Support 始终是开发静态库。Blocking Extras 是由 `QCurl::BlockingExtras` 暴露的逻辑消费面，
实现随 Core runtime 交付，分组件安装只需 `BlockingExtrasDevelopment`；Other Extras 是独立生产
runtime 编译库，需要对应的 Runtime 与 Development component。

## 5. Static library 初始化

shared library 被加载时通常会自动注册 QCurl 公共 Qt 元类型；普通 shared consumer 无需手动初始化。

static library 没有“库加载”这一步。如果程序只使用 `QCNetworkRequestPriority` 等头文件枚举，但又需要 queued connection、`QSignalSpy`、`QVariant` 或 `QMetaType::fromName()`，请在 `main()` 早期调用一次：

```cpp
#include <QCGlobal.h>

#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCurl::initialize();
    return app.exec();
}
```

`QCurl::initialize()` 是幂等的；多次调用不会重复创建网络对象，也不会启动 scheduler。

## 6. pkg-config

安装到 staging prefix 后，可按实际系统设置 `PKG_CONFIG_PATH`：

```bash
export PKG_CONFIG_PATH=/path/to/QCurl/stage/lib/pkgconfig:$PKG_CONFIG_PATH
g++ main.cpp $(pkg-config --cflags --libs qcurl) -o qcurl_consumer
```
