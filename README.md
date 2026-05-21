# QCurl

> 基于 Qt6 和 libcurl 的现代 C++ 网络库，提供高性能、类型安全的 HTTP Core API 与可选 Extras / 显式 Extras / Preview。

[![Qt6](https://img.shields.io/badge/Qt-6.2+-41CD52?logo=qt)](https://www.qt.io/)
[![C++17](https://img.shields.io/badge/C++-17-00599C?logo=cplusplus)](https://en.cppreference.com/w/cpp/17)
[![libcurl](https://img.shields.io/badge/libcurl-7.85%2B-073551?logo=curl)](https://curl.se/libcurl/)
[![CMake](https://img.shields.io/badge/CMake-3.16+-064F8C?logo=cmake)](https://cmake.org/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

---

## 发布状态与 RC 边界

`QCurl 3.0.0-rc.1` 的稳定承诺范围以默认安装面为准：`QCURL_INSTALL_HEADERS + QCurlConfig.h`。这些头文件构成 Core API，进入默认 `find_package(QCurl)` / `QCurl::QCurl` consumer contract。

| 层级 | 发布含义 | 当前范围 |
| --- | --- | --- |
| **Core** | 默认安装，作为 `3.0.0-rc.1` 稳定候选维护 API / ABI | `QCNetworkAccessManager`、`QCCookieAsyncResult`、`QCNetworkRequest`、`QCNetworkRequestConfig`、`QCNetworkReply`、TLS / proxy / timeout / retry / redirect / transfer 配置、HTTP method / version / error / priority、lane-aware scheduler、cache policy type header、Cache lookup concrete API、Multipart builder、`QCNetworkLogger`、`QCNetworkDefaultLogger`、`QCNetworkCancelToken`、Middleware base、ConnectionPool 管理面 |
| **Blocking Extras** | 显式安装，提供同步 value-result 工具；不混入默认 Core | `QCBlockingNetworkClient`、`QCBlockingNetworkResult`、`QCBlockingCookieStore` |
| **Test Support** | 显式安装，用于测试支持，不作为生产运行时网络栈能力表述 | `QCNetworkMockHandler`、`QCNetworkCapturedRequest`、`QCNetworkTestSupport` |
| **Other Extras / Preview** | 显式安装或条件安装；不属于默认 Core 稳定承诺 | Diagnostics、Middleware Extras、WebSocket |

除非文档明确标注为 Core，示例中引用 `QCURL_INSTALL_HEADERS_EXTRAS` 的头文件时，都应视为显式 opt-in 的非默认发行面。完整边界见 `docs/arch/public-header-boundary.md` 与 `docs/arch/rc-maturity-review.md`。

## 为什么选择 QCurl？

| **现代化架构** | **Core HTTP** | **可选扩展** | **Qt 友好** |
|:-------------:|:-------------:|:------------:|:-----------:|
| CMake + RAII + C++17 | HTTP/1.1、HTTP/2、HTTP/3 capability | 显式 Extras / Preview | QObject 线程归属与事件循环合同 |

---

## 功能分层

### Core

- **CMake 构建系统** - 跨平台支持，自动依赖检测
- **统一 Reply 架构** - 1 个类替代 6 个子类，代码量减少 30%
- **RAII 资源管理** - `QCCurlHandleManager` 自动管理 curl 句柄，零内存泄漏
- **C++17 特性** - `std::optional`、`std::chrono`、`[[nodiscard]]`、`enum class`
- **HTTP/1.1、HTTP/2、HTTP/3 capability** - HTTP/3 取决于运行时 libcurl / QUIC backend
- **SSL/TLS** - 可配置证书验证、客户端证书、CA 路径
- **代理支持** - HTTP、HTTPS、SOCKS4/4A、SOCKS5
- **Canonical Request API** - `QCNetworkRequest` + `QCNetworkAccessManager::send*()` 一套入口覆盖配置与发送
- **请求对象配置** - `QCNetworkRequest::setRawHeader()/setTimeout()/setPriority()/setLane()` 支持链式配置；`QCNetworkRedirectConfig` 与 `QCNetworkTransferConfig` 聚合重定向和传输配置
- **请求重试** - 指数退避算法，自动处理临时性错误
- **lane-aware 调度** - lane reservation + DRR 公平调度 + 按 lane 精准取消
- **缓存策略类型** - `QCNetworkCachePolicy` 是 `QCNetworkRequest` 的 Core 配置类型
- **Cache lookup API** - `QCNetworkCache`、`QCNetworkMemoryCache`、`QCNetworkDiskCache` 提供 `lookup(url, ReadMode)`，返回 `Miss / FreshHit / StaleHit`
- **Multipart/form-data builder** - `QCMultipartFormData` / `QCNetworkMultipartBody` 生成 body，再通过 `sendPost()` 发送
- **日志接口** - `QCNetworkLogger` 提供 Core 级日志抽象与 debug trace 脱敏入口
- **默认日志实现** - `QCNetworkDefaultLogger` 提供 Core 级默认 logger helper
- **取消令牌** - `QCNetworkCancelToken` 提供 reply-level 批量取消和自动超时取消
- **Middleware base** - `QCNetworkMiddleware` 作为 Core 拦截与观测基类进入默认安装面；通用具体 middleware 通过 Other Extras opt-in 使用
- **ConnectionPool 管理面** - 连接池配置、统计和资源控制接口使用 accessor / shared-data API
- **流式下载/上传** - `QCNetworkDownloadToDeviceJob` 与 manager-level `sendPost()/sendPut()` raw-body device overload 支持大文件
- **断点续传** - `QCNetworkResumableDownloadJob` 基于 HTTP Range 请求恢复下载
- **Cookie async result** - `QCCookieOperationResult` / `QCCookieExportResult` 是 manager cookie async signal 与 `QFuture` 的 Core 值结果

### Blocking Extras

- **同步 value-result client** - `QCBlockingNetworkClient` / `QCBlockingNetworkResult` 通过显式 `BlockingExtrasDevelopment` 安装，不随默认 Core 安装。
- **受限内存响应体** - `QCBlockingRequestOptions::maxInMemoryBodyBytes()` 默认限制内存响应体；超过上限返回 `NetworkError::BodyTooLarge`。
- **大响应下载** - 大响应使用 `QCBlockingNetworkClient::downloadToDevice()` 写入调用方提供的 `QIODevice`，`body()` 保持为空，`bytesReceived()` 记录实际接收字节数。
- **诊断错误边界** - Blocking Extras 使用 `BodyTooLarge`、`OutputDeviceError`、`InputDeviceError`、`ReplayNotSupported` 等明确错误；curl code 只通过 `diagnosticCurlCode()` 作为辅助诊断，不作为主判断 API。
- **Cookie snapshot / delta** - `QCBlockingCookieStore` 提供 Blocking Extras cookie 边界，不访问 live manager cookie store。

### Test Support

- **MockHandler Test Support** - `QCNetworkMockHandler`、`QCNetworkCapturedRequest` 与 `QCNetworkTestSupport` 通过显式 `TestSupportDevelopment` 安装，供测试程序 opt-in 使用。

### Other Extras / Preview

- **Diagnostics 扩展诊断** - `QCNetworkDiagnostics` 通过显式 `OtherExtrasDevelopment` 安装；`ping/traceroute` 与 `details` schema 仍不作为默认 Core 稳定合同。
- **Middleware Extras** - `QCNetworkMiddlewareExtras` 通过显式 `OtherExtrasDevelopment` 安装；默认 Core 只承诺 `QCNetworkMiddleware` base。
- **WebSocket** - 客户端实现包含压缩、自动重连和连接池能力，条件进入 Other Extras；当前仍不属于默认 Core install surface。

### 性能基准说明

性能数字需要绑定具体 benchmark 版本、依赖版本、网络环境和日期。进入 RC 前，未绑定证据的数字不应作为稳定发布承诺。

---

## 📦 系统要求

| 依赖          | 版本要求  | 说明                           |
| ----------- | ----- | ---------------------------- |
| **CMake**   | 3.16+ | 构建系统                         |
| **Qt6**     | 6.2+  | QtCore、QtNetwork             |
| **libcurl** | 7.85.0+ | WebSocket 需 7.86.0+；HTTP/3 推荐 8.16.0+ 且带 QUIC backend |
| **编译器**     | C++17 | GCC 11+、Clang 14+、MSVC 2019+ |

---

## 🚀 快速开始

### 构建安装

```bash
git clone https://github.com/user/QCurl.git && cd QCurl
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

发布合同提示：

- `3.0.0-rc.1` 只承诺 Core install surface。
- Blocking Extras / Test Support / Other Extras 需要发行包显式安装对应 component，不随默认 Core 隐式安装。
- 当前 lane-aware scheduler 的 breaking ABI 变更以 `QCurl 3.0.0 / SOVERSION 3` 发布。

### 代码示例

#### 1. 简单 GET 请求

```cpp
#include <QCNetworkAccessManager.h>
#include <QCNetworkRequest.h>

QCurl::QCNetworkAccessManager manager;
QCurl::QCNetworkRequest request(QUrl("https://api.example.com/data"));
request.setRawHeader("Authorization", "Bearer token")
    .setTimeout(std::chrono::seconds(30));

auto *reply = manager.sendGet(request);

connect(reply, &QCurl::QCNetworkReply::finished, [reply]() {
    if (reply->error() == QCurl::NetworkError::NoError) {
        if (const auto data = reply->readAll(); data.has_value()) {
            qDebug() << "Response:" << *data;
        }
    }
    reply->deleteLater();
});
```

#### 2. WebSocket 连接（Preview）

> 说明：WebSocket 使用 `QCURL_INSTALL_HEADERS_EXTRAS` 中的扩展头。
> 它可以作为 Preview 功能使用，但不属于 `3.0.0-rc.1` 的 Core 稳定承诺。

```cpp
#include <QCWebSocket.h>

auto *socket = new QCurl::QCWebSocket(QUrl("wss://echo.websocket.org"));

// 启用压缩和自动重连
socket->setCompressionConfig(QCurl::QCWebSocketCompressionConfig::defaultConfig());
socket->setReconnectPolicy(QCurl::QCWebSocketReconnectPolicy::standardReconnect());

connect(socket, &QCurl::QCWebSocket::connected, [socket]() {
    socket->sendTextMessage("Hello WebSocket!");
});

connect(socket, &QCurl::QCWebSocket::textMessageReceived, [](const QString &msg) {
    qDebug() << "Received:" << msg;
});

socket->open();
```

#### 3. 文件上传（Core Multipart）

`QCMultipartFormData.h` 属于 Core install surface，默认 installed consumer 可以直接使用该 builder。

```cpp
#include <QCMultipartFormData.h>
#include <QCNetworkMultipartBody.h>

QCurl::QCMultipartFormData formData;
formData.addTextField("userId", "12345");
formData.addFileField("avatar", "/path/to/photo.jpg");

QCurl::QCNetworkRequest uploadRequest(QUrl("https://api.example.com/upload"));
auto body = QCurl::QCNetworkMultipartBody::fromFormData(formData);
uploadRequest.setRawHeader("Content-Type", body.contentType());
auto *reply = manager.sendPost(uploadRequest, body.data());
```

#### 4. 内存请求体（JSON / form-urlencoded）

`QCNetworkBody` 会随请求体保存匹配的 `Content-Type`。`sendPost()` / `sendPut()` /
`sendPatch()` 接收 `QCNetworkBody` 时，若请求尚未显式设置 `Content-Type`，会自动补齐；若已设置，
则尊重请求里的显式值。

```cpp
#include <QCNetworkBody.h>

QCurl::QCNetworkRequest formRequest(QUrl("https://api.example.com/form"));
auto formBody = QCurl::QCNetworkBody::fromFormUrlEncoded(
    QList<QPair<QString, QString>>{
        {QStringLiteral("tag"), QStringLiteral("one")},
        {QStringLiteral("tag"), QStringLiteral("two")},
    });
auto *formReply = manager.sendPost(formRequest, formBody);
```

#### 5. Lane-aware Scheduler

`QCNetworkRequestScheduler.h` 属于显式公开的调度能力；默认 Core 不提供透明跨线程阻塞 getter。

```cpp
#include <QCNetworkRequestScheduler.h>

// 建议在 manager owner thread 的初始化阶段获取并配置 scheduler。
auto *scheduler = manager.scheduler();

QCurl::QCNetworkRequestScheduler::LaneConfig controlLane;
controlLane.setWeight(3);
controlLane.setQuantum(1);
controlLane.setReservedGlobal(1);
controlLane.setReservedPerHost(1);
scheduler->setLaneConfig(QStringLiteral("Control"), controlLane);

QCurl::QCNetworkRequestScheduler::LaneConfig transferLane;
transferLane.setWeight(1);
transferLane.setQuantum(1);
scheduler->setLaneConfig(QStringLiteral("Transfer"), transferLane);

QCurl::QCNetworkRequest controlRequest(QUrl("https://api.example.com/manifest"));
controlRequest.setLane(QStringLiteral("Control"))
    .setPriority(QCurl::QCNetworkRequestPriority::High);

QCurl::QCNetworkRequest transferRequest(QUrl("https://cdn.example.com/chunk.bin"));
transferRequest.setLane(QStringLiteral("Transfer"))
    .setPriority(QCurl::QCNetworkRequestPriority::Low);

QObject::connect(scheduler,
                 &QCurl::QCNetworkRequestScheduler::requestAboutToStart,
                 [](QCurl::QCNetworkReply *, const QString &lane, const QString &hostKey) {
                     qDebug() << "about-to-start lane=" << lane << "hostKey=" << hostKey;
                 });

QObject::connect(scheduler,
                 &QCurl::QCNetworkRequestScheduler::requestStarted,
                 [](QCurl::QCNetworkReply *, const QString &lane, const QString &hostKey) {
                     qDebug() << "started lane=" << lane << "hostKey=" << hostKey;
                 });

scheduler->cancelLaneRequests(QStringLiteral("Transfer"),
                              QCurl::QCNetworkRequestScheduler::CancelLaneScope::PendingAndRunning);
```

简要提示：

- `lane` 可以理解成“请求车道”：先按车道分组，再在车道内按优先级排序。
- `Critical` 现在只影响同一条 lane 内的启动顺序；若需要给控制类请求留保底名额，请使用 lane reservation。
- 调度器信号携带 `lane + hostKey`，用于按车道和 host 做观测。
- `cancelLaneRequests()` 语义：`PendingOnly` 只清 pending/deferred，`PendingAndRunning` 会连 running 一并取消。
- `manager.scheduler()` 是 owner-thread only：跨线程误用会 warning 并返回 `nullptr`；跨线程配置时把“配置动作”显式投递到 manager owner thread 执行。

完整说明、请求时序图和 `Control / Transfer / Background` 配置建议统一参考：

- `docs/user/lane-scheduler.md`

---

## ⚡ 性能回归入口

性能数字只在绑定具体 benchmark 版本、依赖版本、网络环境和日期时才作为发布证据。
当前 README 不把固定延迟或吞吐数字写成 `3.0.0-rc.1` 稳定承诺。

性能回归与能力证据以以下入口为准：

- `docs/reference/performance.md`
- `docs/reference/benchmarks.md`
- `tests/libcurl_consistency/run_gate.py --suite all --with-ext --build`

---

## 🧪 测试覆盖

- **100+ 测试用例** - 单元测试 + 集成测试
- **96.3% 通过率** - 自动化验证
- **13 个示例程序** - 涵盖各种使用场景
- **6 个性能基准** - HTTP/2、HTTP/3、WebSocket、连接池、调度器

测试运行与门禁（offline/env/全量回归/libcurl_consistency）请以 `docs/dev/build-and-test.md` 为准；测试目录入口见 `tests/README.md`。

---

## 📚 文档

| 文档                                                 | 说明                    |
| -------------------------------------------------- | --------------------- |
| [docs/README.md](docs/README.md)                   | 文档入口（按读者角色分层）        |
| [SYSTEM_DOCUMENTATION.md](SYSTEM_DOCUMENTATION.md) | 详细系统文档（全量说明/实现细节）     |
| [examples/README.md](examples/README.md)           | 示例集合与运行方式              |

---

## 🔧 项目集成

### CMake

```cmake
find_package(QCurl CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE QCurl::QCurl)
```

默认构建发布 shared library。Static library 需要显式 opt-in：

```bash
cmake -S . -B build-static -DCMAKE_BUILD_TYPE=Release -DQCURL_BUILD_STATIC=ON
cmake --build build-static --target QCurl qcurl_public_api_self_compile
ctest --test-dir build-static -L '^public-api$' --output-on-failure
ctest --test-dir build-static -L '^public-api-slow$' --output-on-failure
```

Static 路径通过前只能说明 static opt-in 构建链路可用；在 static export、consumer smoke、pkg-config 和 release gate 证据齐备前，不声明 whole project static library ready。

### pkg-config

```bash
g++ your_app.cpp $(pkg-config --cflags --libs qcurl) -o your_app
```

---

## 🤝 贡献

欢迎 Pull Request！

- 贡献指南：`CONTRIBUTING.md`
- 行为准则：`CODE_OF_CONDUCT.md`
- 安全策略：`SECURITY.md`
- 支持与反馈：`SUPPORT.md`

---

## 📜 许可证

[MIT License](LICENSE) - 自由使用、修改、分发

---

## 🙏 致谢

- **[libcurl](https://curl.se/)** - 强大的网络传输库
- **[Qt](https://www.qt.io/)** - 优雅的跨平台 C++ 框架

---

## ⭐ Star History

如果 QCurl 对你有帮助，请给个 Star 支持一下！

---

**QCurl** - 现代、高效、易用的 Qt6 网络库 🚀
