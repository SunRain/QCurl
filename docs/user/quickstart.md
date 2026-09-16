# 快速开始

从源码安装 QCurl，在独立 CMake 工程中只消费 Core。以下命令在仓库根目录执行；构建目录名可自行指定，安装前缀必须使用绝对路径。

## 1. 准备与安装

需要 CMake 3.16+、C++17 编译器、Qt **6.10.3+**（源码构建需要 Core、Network）、libcurl 7.85.0+ 与 zlib 开发包。Core consumer 不需要 QtNetwork；WebSocket 需 libcurl 7.86.0+，HTTP/3 能力边界见[配置](configuration.md#http-version)。

尚未检出源码时：

```bash
git clone https://github.com/SunRain/QCurl.git
cd QCurl
```

```bash
QCURL_STAGE="$PWD/stage"
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF \
  -DCMAKE_INSTALL_PREFIX="$QCURL_STAGE" -DCMAKE_INSTALL_LIBDIR=lib
cmake --build build-core --parallel
cmake --install build-core
```

在 configure 阶段设置 `CMAKE_INSTALL_PREFIX`，让安装命令使用一致的 staging 目录。当前 qcurl.pc 通过 pcfiledir 推导前缀，可随安装树整体重定位；固定 `CMAKE_INSTALL_LIBDIR=lib` 便于下面的命令跨发行版复用。Qt SDK 不在系统路径时，在 configure 和 consumer configure 中为 `CMAKE_PREFIX_PATH` 提供所需前缀。

无过滤 `cmake --install` 安装 Core、内嵌 Blocking 实现、Other Extras、Test Support：四个逻辑消费面、三个物理库。默认 consumer 仍只加载 `QCurl::QCurl`。需要系统安装时另选系统前缀，不默认使用 sudo。

## 2. 独立 Core consumer

建立 `consumer/CMakeLists.txt` 和 `consumer/main.cpp`。下面给出完整安装后 consumer，包括首页预览所省略的应用、事件循环、生命周期和结果处理上下文；不依赖源码树 include 或测试 fixture：

```cmake
cmake_minimum_required(VERSION 3.16)
project(qcurl_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(QCurl CONFIG REQUIRED)
add_executable(qcurl_consumer main.cpp)
target_link_libraries(qcurl_consumer PRIVATE QCurl::QCurl)
```

```cpp
#include <QCNetworkAccessManager.h>
#include <QCNetworkReply.h>
#include <QCNetworkRequest.h>

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QUrl>

#include <chrono>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2) {
        qCritical() << "usage: qcurl_consumer <http-or-https-url>";
        return 2;
    }

    QCurl::QCNetworkAccessManager manager;
    QCurl::QCNetworkRequest request(QUrl{app.arguments().at(1)});
    request.setTimeout(std::chrono::seconds(10));
    auto *reply = manager.get(request);
    QObject::connect(reply, &QCurl::QCNetworkReply::finished, &app, [&app, reply]() {
        const auto body = reply->readAll();
        const bool ok = reply->error() == QCurl::NetworkError::NoError
                        && reply->httpStatusCode() >= 200 && reply->httpStatusCode() < 300
                        && body.has_value();
        qInfo() << "HTTP" << reply->httpStatusCode();
        if (ok) {
            qInfo().noquote() << QString::fromUtf8(*body);
        } else {
            qCritical() << reply->errorString();
        }
        reply->deleteLater();
        app.exit(ok ? 0 : 1);
    });
    QTimer::singleShot(std::chrono::seconds(15), &app, [&app]() { app.exit(2); });
    return app.exec();
}
```

```bash
cmake -S consumer -B consumer-build -DCMAKE_PREFIX_PATH="$QCURL_STAGE"
cmake --build consumer-build
./consumer-build/qcurl_consumer https://example.com
```

manager 与 reply 在 `QCoreApplication` 所在线程使用，事件循环驱动请求。reply 由 manager 持有；在其返回后连接完成信号，读取最终结果后使用 `deleteLater()`。示例接受一个 HTTP/HTTPS URL，2xx 且能读取正文才返回 0；传输/HTTP 失败返回 1，参数错误或 15 秒整体期限返回 2。请求本身另设 10 秒超时。

`diagnosticCurlCode()` 是辅助诊断，不能代替 `error()` 与 HTTP 状态判断；例如 HTTP 404 也可能有 curl code 0。完整生命周期和线程约束见[公开头注释](../../src/QCNetworkReply.h)，本教程不复制 API 参考。

<a id="local-fixture"></a>
## 3. 用本地 fixture 核对响应

需要 Python 3。先在仓库根目录准备静态响应（不要覆盖自己的同名文件）：

```bash
mkdir -p build-core/doc-fixture
printf 'qcurl-docs-ok\n' > build-core/doc-fixture/response.txt
python3 -m http.server 18080 --bind 127.0.0.1 --directory build-core/doc-fixture
```

保持该终端运行，在另一个终端的仓库根目录执行：

```bash
./consumer-build/qcurl_consumer http://127.0.0.1:18080/response.txt
```

应输出 `HTTP 200` 和正文 `qcurl-docs-ok`，退出码为 0；访问 `/missing` 应输出 `HTTP 404` 并返回 1。端口被占用时同时改服务端和 URL 的端口。验证后在服务端终端按 Ctrl-C 停止，仅停止自己的 fixture。此验证不证明公网、TLS 或 HTTP/3 可用。

## 4. 显式启用扩展组件

在 consumer 的 CMakeLists 中选择实际使用的组件：

```cmake
find_package(QCurl CONFIG REQUIRED COMPONENTS BlockingExtras)
target_link_libraries(your_app PRIVATE QCurl::BlockingExtras)
```

`OtherExtras` / `QCurl::OtherExtras`、`TestSupport` / `QCurl::TestSupport` 同理；多个组件可在同一次 `find_package` 中列出。非 Core 组件不属于默认 Core 源码兼容承诺；Diagnostics、WebSocket 为 Preview，Middleware Extras 为 Stable。

若只需分组件部署，先安装 Core 的 `Runtime` 与 `Development`，再安装所需扩展：

| 消费面 | 额外 install component | 物理形态 |
| --- | --- | --- |
| Blocking Extras | `BlockingExtrasDevelopment` | INTERFACE target，实现在 Core 库内 |
| Other Extras | `OtherExtrasRuntime`、`OtherExtrasDevelopment` | 独立 shared/static 库 |
| Test Support | `TestSupportDevelopment` | 开发静态库 |

例如 `cmake --install build-core --component BlockingExtrasDevelopment`；组件过滤不会自动补装它所依赖的 Core。完整安装、默认 Core 负向 consumer 和导出约束见[公共头边界](../dev/architecture/public-header-boundary.md)。

## 5. Static library 与初始化

Static 必须使用独立目录并关闭测试：

```bash
cmake -S . -B build-core-static -DCMAKE_BUILD_TYPE=Release \
  -DQCURL_BUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF \
  -DCMAKE_INSTALL_PREFIX="$PWD/stage-static" -DCMAKE_INSTALL_LIBDIR=lib
cmake --build build-core-static --parallel
cmake --install build-core-static
```

为 static consumer 使用新的构建目录（例如 consumer-static-build），将 `CMAKE_PREFIX_PATH` 指向该独立前缀，避免复用已缓存 shared QCurl_DIR 的 consumer-build。OFF producer 不运行 CTest，安装包验证见[发布操作](../dev/release/release-procedure.md#package-evidence)。

shared 加载时通常已注册公共 Qt 元类型；static consumer 若只使用头文件枚举但需要 queued connection、QVariant 或名称查找，应 include `<QCGlobal.h>` 并在 `main()` 中、首次连接前调用 `QCurl::initialize()`。它幂等，不创建网络对象或启动 scheduler。

## 6. pkg-config

使用第 1 节配置期指定的前缀。在仓库根目录编译同一 `consumer/main.cpp`：

```bash
export PKG_CONFIG_PATH="$QCURL_STAGE/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
c++ -std=c++17 -fPIC consumer/main.cpp $(pkg-config --cflags --libs qcurl) \
  -Wl,-rpath,"$QCURL_STAGE/lib" -o consumer-build/qcurl_consumer-pc
./consumer-build/qcurl_consumer-pc http://127.0.0.1:18080/response.txt
```

此处为 Linux GCC/Clang 命令；`-fPIC` 避免手工编译触发 Qt protected data symbol 的 copy relocation（CMake target 会携带适用的 Qt 编译选项）。rpath 用于本地 shared staging 运行；部署时按目标平台的动态加载规则处理。static 前缀改用 `pkg-config --cflags --libs --static qcurl` 以携带必要的传递链接依赖，且遵循上面的显式初始化约束。

继续阅读：[配置](configuration.md) · [调度](lane-scheduler.md) · [流控](flow-control.md) · [从 1.0 迁移](migration-2.0.md)。
