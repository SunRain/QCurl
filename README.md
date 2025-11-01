# QCurl 🚀

> 基于 Qt6 和 libcurl 的现代 C++ 网络库，提供高性能、类型安全的 HTTP/WebSocket 客户端 API

[![Qt6](https://img.shields.io/badge/Qt-6.2+-41CD52?logo=qt)](https://www.qt.io/)
[![C++17](https://img.shields.io/badge/C++-17-00599C?logo=cplusplus)](https://en.cppreference.com/w/cpp/17)
[![libcurl](https://img.shields.io/badge/libcurl-8.0+-073551?logo=curl)](https://curl.se/libcurl/)
[![CMake](https://img.shields.io/badge/CMake-3.16+-064F8C?logo=cmake)](https://cmake.org/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

---

## 为什么选择 QCurl？

| 🏗️ **现代化架构**        | 🌐 **协议完整**              | ⚡ **高性能**    | 🏢 **企业就绪** |
|:--------------------:|:------------------------:|:------------:|:-----------:|
| CMake + RAII + C++17 | HTTP/1.1/2/3 + WebSocket | 连接池提升 60-80% | 日志/中间件/诊断   |

---

## ✨ 核心特性

### 🏗️ 现代化架构

- **CMake 构建系统** - 跨平台支持，自动依赖检测
- **统一 Reply 架构** - 1 个类替代 6 个子类，代码量减少 30%
- **RAII 资源管理** - `QCCurlHandleManager` 自动管理 curl 句柄，零内存泄漏
- **C++17 特性** - `std::optional`、`std::chrono`、`[[nodiscard]]`、`enum class`

### 🌐 完整协议支持

- **HTTP/1.1、HTTP/2、HTTP/3 (QUIC)** - 三层自动降级策略
- **WebSocket** - 完整客户端实现，支持压缩（RFC 7692）、自动重连、连接池
- **SSL/TLS** - 可配置证书验证、客户端证书、CA 路径
- **代理支持** - HTTP、HTTPS、SOCKS4/4A、SOCKS5

### ⚡ 性能优化

| 特性            | 性能提升              | 说明                      |
| ------------- | ----------------- | ----------------------- |
| HTTP 连接池      | **60-80%**        | 零配置自动启用，避免重复 TCP/TLS 握手 |
| HTTP/2 多路复用   | **延迟 -73%**       | 单连接处理多请求，HPACK 头部压缩     |
| WebSocket 连接池 | **2000ms → 10ms** | 连接复用，避免重复握手             |
| 事件驱动接收        | **延迟 -98%**       | 替代轮询，CPU 占用降低 60%       |

### 🏢 企业级能力

- **统一日志系统** - `QCNetworkLogger` 支持多级别日志、自定义处理器
- **中间件系统** - `QCNetworkMiddleware` 请求/响应拦截、认证注入、监控埋点
- **取消令牌** - `QCNetworkCancelToken` 批量请求管理、超时控制
- **网络诊断** - DNS 解析、Ping 测试、Traceroute、SSL 证书检查
- **Mock 工具** - `QCNetworkMockHandler` 单元测试必备

### 📁 文件操作

- **流式下载/上传** - `downloadToDevice()` / `uploadFromDevice()` 支持大文件
- **断点续传** - HTTP Range 请求自动恢复下载
- **Multipart/form-data** - RFC 7578 兼容，自动 MIME 类型推断

### 🎯 开发体验

- **流式链式 API** - `QCRequest::get(url).withHeader().send()` 极简语法
- **传统构建器 API** - `QCRequestBuilder` 适合复杂配置
- **请求重试** - 指数退避算法，自动处理临时性错误
- **优先级调度** - 6 级优先级，并发控制，带宽限制

---

## 📦 系统要求

| 依赖          | 版本要求  | 说明                           |
| ----------- | ----- | ---------------------------- |
| **CMake**   | 3.16+ | 构建系统                         |
| **Qt6**     | 6.2+  | QtCore、QtNetwork             |
| **libcurl** | 8.0+  | 推荐 8.16.0+（HTTP/3 支持）        |
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

### 代码示例

#### 1. 简单 GET 请求

```cpp
#include <QCRequest.h>

auto *reply = QCurl::QCRequest::get("https://api.example.com/data")
    .withHeader("Authorization", "Bearer token")
    .withTimeout(std::chrono::seconds(30))
    .send();

connect(reply, &QCurl::QCNetworkReply::finished, [reply]() {
    if (reply->error() == QCurl::NetworkError::NoError) {
        qDebug() << "Response:" << reply->readAll().value();
    }
    reply->deleteLater();
});
```

#### 2. WebSocket 连接

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

#### 3. 文件上传（Multipart）

```cpp
#include <QCMultipartFormData.h>

QCurl::QCMultipartFormData formData;
formData.addTextField("userId", "12345");
formData.addFileField("avatar", "/path/to/photo.jpg");

auto *reply = manager->postMultipart(QUrl("https://api.example.com/upload"), formData);
```

---

## ⚡ 性能基准

基于真实网络测试（Google、nghttp2.org、Cloudflare）：

| 场景     | HTTP/1.1  | HTTP/2            | HTTP/3             |
| ------ | --------- | ----------------- | ------------------ |
| 单请求延迟  | 530 ms    | 145 ms (**-73%**) | ~100 ms (**-81%**) |
| 5 并发请求 | 31,000 ms | ~15,000 ms        | ~10,000 ms         |
| 连接数    | 5         | 1                 | 1                  |

---

## 🧪 测试覆盖

- **100+ 测试用例** - 单元测试 + 集成测试
- **96.3% 通过率** - 自动化验证
- **13 个示例程序** - 涵盖各种使用场景
- **6 个性能基准** - HTTP/2、HTTP/3、WebSocket、连接池、调度器

```bash
# 运行测试
docker run -d -p 8935:80 --name httpbin kennethreitz/httpbin
cd build && ctest --output-on-failure
```

---

## 📚 文档

| 文档                                                 | 说明                    |
| -------------------------------------------------- | --------------------- |
| [SYSTEM_DOCUMENTATION.md](SYSTEM_DOCUMENTATION.md) | 详细系统文档                |
| [docs/](docs/)                                     | 技术文档（HTTP/3 指南、缓存设计等） |

---

## 🔧 项目集成

### CMake

```cmake
find_package(QCurl REQUIRED)
target_link_libraries(your_app PRIVATE QCurl::QCurl)
```

### pkg-config

```bash
g++ your_app.cpp $(pkg-config --cflags --libs qcurl) -o your_app
```

---

## 🤝 贡献

欢迎 Pull Request！请确保：

1. 遵循 [编码规范](Qt6_CPP17_Coding_Style.md)
2. 添加测试覆盖新功能
3. 通过所有测试 (`ctest`)

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
