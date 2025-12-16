# QCurl 系统文档

> **项目版本**: 2.0+
> **文档创建**: 2025-01-26
> **目标读者**: 系统维护者、开发者、架构师
> **文档目的**: 提供完整的系统架构、功能说明和开发指南

---

## 目录

1. [系统概述](#系统概述)
2. [核心架构](#核心架构)
3. [功能模块](#功能模块)
4. [API参考](#api参考)
5. [构建与部署](#构建与部署)
6. [开发指南](#开发指南)
7. [测试策略](#测试策略)
8. [性能与优化](#性能与优化)
9. [故障排查](#故障排查)
10. [扩展开发](#扩展开发)

---

## 系统概述

### 项目简介

QCurl 是一个基于 Qt6 和 libcurl 的现代 C++ 网络库，提供类似 `QNetworkAccessManager` 的 API 接口，用于执行 HTTP/WebSocket 网络请求。它封装了 libcurl 的底层细节，提供同步和异步两种网络请求方式，支持 HTTP/1.1、HTTP/2、HTTP/3 和 WebSocket 协议。

### 核心价值

- **高性能**: 基于 libcurl，支持 HTTP/2 多路复用、HTTP/3 QUIC 传输、连接池复用
- **类型安全**: C++17 标准，使用 `enum class`、`std::optional`、`[[nodiscard]]` 等现代特性
- **Qt 风格**: 提供与 Qt Network 模块相似的 API 设计，易于 Qt 开发者上手
- **功能完整**: 支持 SSL/TLS、代理、缓存、重试、优先级调度、网络诊断等企业级功能

### 技术栈

| 组件 | 版本要求 | 用途 |
|------|---------|------|
| **C++** | C++17 | 核心语言标准 |
| **Qt6** | 6.2+ | QtCore、QtNetwork 模块 |
| **libcurl** | 8.0+ | 底层网络传输（推荐 8.16.0+ 以支持完整特性） |
| **CMake** | 3.16+ | 构建系统 |
| **编译器** | GCC 11+、Clang 14+、MSVC 2019+ | 支持 C++17 |

### 系统特性概览

| 特性类别 | 功能列表 | 状态 |
|---------|---------|------|
| **协议支持** | HTTP/1.1、HTTP/2、HTTP/3、WebSocket | ✅ |
| **请求方式** | HEAD、GET、POST、PUT、DELETE、PATCH | ✅ |
| **执行模式** | 同步、异步 | ✅ |
| **SSL/TLS** | 证书验证、客户端证书、自定义 CA | ✅ |
| **代理** | HTTP、HTTPS、SOCKS4、SOCKS5 | ✅ |
| **缓存** | 内存缓存、磁盘缓存、策略控制 | ✅ |
| **连接池** | HTTP 连接复用、WebSocket 连接池 | ✅ |
| **高级功能** | 重试机制、优先级调度、中间件、日志、取消令牌、网络诊断 | ✅ |
| **文件操作** | 流式下载/上传、断点续传、Multipart 表单 | ✅ |
| **WebSocket** | 文本/二进制消息、压缩、自动重连、Ping/Pong | ✅ |

---

## 核心架构

### 架构分层

```
┌─────────────────────────────────────────────────────────┐
│                    应用层 (Application Layer)            │
│          用户代码 / 示例程序 / 测试程序                   │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│                    API 层 (Public API Layer)            │
│   ┌────────────────────────────────────────────────┐   │
│   │  核心 API                                       │   │
│   │  - QCNetworkAccessManager (网络访问管理器)      │   │
│   │  - QCNetworkRequest (请求配置)                 │   │
│   │  - QCNetworkReply (统一响应对象)               │   │
│   │  - QCWebSocket (WebSocket 客户端)              │   │
│   └────────────────────────────────────────────────┘   │
│   ┌────────────────────────────────────────────────┐   │
│   │  流式 API (Fluent API)                         │   │
│   │  - QCRequest (流式请求构建)                     │   │
│   │  - QCRequestBuilder (传统构建器)                │   │
│   │  - QCNetworkRequestBuilder (流式构建器)         │   │
│   └────────────────────────────────────────────────┘   │
│   ┌────────────────────────────────────────────────┐   │
│   │  辅助 API                                       │   │
│   │  - QCMultipartFormData (Multipart 表单)        │   │
│   │  - QCNetworkDiagnostics (网络诊断)             │   │
│   └────────────────────────────────────────────────┘   │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│               业务逻辑层 (Business Logic Layer)          │
│   ┌────────────────────────────────────────────────┐   │
│   │  请求管理                                       │   │
│   │  - QCCurlMultiManager (多请求管理器)            │   │
│   │  - QCNetworkRequestScheduler (请求调度器)       │   │
│   │  - QCNetworkRetryPolicy (重试策略)              │   │
│   │  - QCNetworkMiddleware (中间件系统)             │   │
│   │  - QCNetworkCancelToken (取消令牌)              │   │
│   └────────────────────────────────────────────────┘   │
│   ┌────────────────────────────────────────────────┐   │
│   │  性能优化                                       │   │
│   │  - QCNetworkCache (缓存基类)                   │   │
│   │  - QCNetworkMemoryCache (内存缓存)             │   │
│   │  - QCNetworkDiskCache (磁盘缓存)                │   │
│   │  - QCNetworkConnectionPoolManager (连接池)     │   │
│   └────────────────────────────────────────────────┘   │
│   ┌────────────────────────────────────────────────┐   │
│   │  监控与调试                                     │   │
│   │  - QCNetworkLogger (日志系统)                   │   │
│   │  - QCNetworkMockHandler (Mock 工具)            │   │
│   └────────────────────────────────────────────────┘   │
│   ┌────────────────────────────────────────────────┐   │
│   │  WebSocket 支持                                 │   │
│   │  - QCWebSocketPool (连接池)                     │   │
│   │  - QCWebSocketReconnectPolicy (重连策略)        │   │
│   │  - QCWebSocketCompressionConfig (压缩配置)      │   │
│   └────────────────────────────────────────────────┘   │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│              基础设施层 (Infrastructure Layer)           │
│   ┌────────────────────────────────────────────────┐   │
│   │  curl 封装                                      │   │
│   │  - QCCurlHandleManager (RAII 句柄管理)          │   │
│   │  - CurlGlobalConstructor (全局初始化)           │   │
│   └────────────────────────────────────────────────┘   │
│   ┌────────────────────────────────────────────────┐   │
│   │  配置类                                         │   │
│   │  - QCNetworkSslConfig (SSL 配置)               │   │
│   │  - QCNetworkProxyConfig (代理配置)              │   │
│   │  - QCNetworkTimeoutConfig (超时配置)            │   │
│   │  - QCNetworkHttpVersion (HTTP 版本)             │   │
│   └────────────────────────────────────────────────┘   │
│   ┌────────────────────────────────────────────────┐   │
│   │  错误处理与工具                                  │   │
│   │  - QCNetworkError (错误枚举)                    │   │
│   │  - QCUtility (工具函数)                         │   │
│   └────────────────────────────────────────────────┘   │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│                 底层依赖 (External Dependencies)         │
│              libcurl + Qt6 Core + Qt6 Network           │
└─────────────────────────────────────────────────────────┘
```

### 核心设计模式

#### 1. **统一 Reply 架构**

v2.0 重构将原有的 6 个 Reply 子类（`QCNetworkAsyncReply`、`QCNetworkAsyncHttpHeadReply`、`QCNetworkAsyncHttpGetReply`、`QCNetworkAsyncDataPostReply`、`QCNetworkSyncReply`、`CurlEasyHandleInitializtionClass`）整合为单一的 `QCNetworkReply` 类。

**优势**：
- 代码量减少 30%
- 消除继承层次复杂性
- 使用枚举区分 HTTP 方法和执行模式
- 更容易扩展新功能

```cpp
// 统一接口
enum class HttpMethod { Head, Get, Post, Put, Delete, Patch };
enum class ExecutionMode { Async, Sync };

class QCNetworkReply : public QObject {
    // 同时支持所有 HTTP 方法和同步/异步模式
};
```

#### 2. **RAII 资源管理**

使用 `QCCurlHandleManager` 自动管理 curl 句柄，确保资源安全释放：

```cpp
class QCCurlHandleManager {
public:
    QCCurlHandleManager() : m_handle(curl_easy_init()) {}
    ~QCCurlHandleManager() { if (m_handle) curl_easy_cleanup(m_handle); }

    CURL* get() const { return m_handle; }

private:
    CURL* m_handle;
};
```

#### 3. **Pimpl (Pointer to Implementation) 模式**

使用私有实现类隐藏实现细节，提供二进制兼容性：

```cpp
// 公共头文件
class QCNetworkReply : public QObject {
private:
    QCNetworkReplyPrivate *d_ptr;
    Q_DECLARE_PRIVATE(QCNetworkReply)
};

// 私有实现 (QCNetworkReply_p.h)
class QCNetworkReplyPrivate {
    // 实现细节
};
```

#### 4. **工厂模式**

`QCNetworkAccessManager` 作为工厂类创建各种请求对象：

```cpp
class QCNetworkAccessManager : public QObject {
public:
    QCNetworkReply* sendGet(const QCNetworkRequest &request);
    QCNetworkReply* sendPost(const QCNetworkRequest &request, const QByteArray &data);
    QCNetworkReply* sendPut(const QCNetworkRequest &request, const QByteArray &data);
    // ...
};
```

#### 5. **策略模式**

- **缓存策略**: `QCNetworkCachePolicy` (AlwaysCache、PreferCache、PreferNetwork、OnlyNetwork、OnlyCache)
- **重试策略**: `QCNetworkRetryPolicy` (指数退避、最大重试次数、可重试错误)
- **重连策略**: `QCWebSocketReconnectPolicy` (重连延迟、最大尝试次数)

#### 6. **中间件模式**

`QCNetworkMiddleware` 实现请求/响应拦截：

```cpp
class QCNetworkMiddleware {
public:
    virtual void onRequest(QCNetworkRequest &request) = 0;
    virtual void onResponse(QCNetworkReply *reply) = 0;
};

// 应用: 认证中间件、日志中间件、监控中间件
```

#### 7. **单例模式**

- `CurlGlobalConstructor`: 确保 libcurl 全局初始化只执行一次
- `QCCurlMultiManager`: 管理所有异步请求的多句柄

---

## 功能模块

### 1. HTTP 请求核心

#### QCNetworkAccessManager

**职责**: 网络访问管理器，作为工厂创建请求对象

**主要方法**:
```cpp
// HTTP 方法
QCNetworkReply* sendHead(const QCNetworkRequest &request);
QCNetworkReply* sendGet(const QCNetworkRequest &request);
QCNetworkReply* sendPost(const QCNetworkRequest &request, const QByteArray &data);
QCNetworkReply* sendPut(const QCNetworkRequest &request, const QByteArray &data);
QCNetworkReply* sendDelete(const QCNetworkRequest &request);
QCNetworkReply* sendPatch(const QCNetworkRequest &request, const QByteArray &data);

// Multipart 上传
QCNetworkReply* postMultipart(const QUrl &url, const QCMultipartFormData &formData);

// Cookie 管理
void setCookieFilePath(const QString &cookieFilePath, CookieFileModeFlag flag);

// 缓存设置
void setCache(QCNetworkCache *cache);

// 调度器设置
void setScheduler(QCNetworkRequestScheduler *scheduler);

// 日志设置
void setLogger(QCNetworkLogger *logger);

// 连接池设置
void setConnectionPoolConfig(const QCNetworkConnectionPoolConfig &config);
```

**使用示例**:
```cpp
auto *manager = new QCNetworkAccessManager();
manager->setCookieFilePath("/tmp/cookies.txt");
manager->setCache(new QCNetworkMemoryCache());

QCNetworkRequest request(QUrl("https://api.example.com/data"));
request.setRawHeader("Authorization", "Bearer token123");

QCNetworkReply *reply = manager->sendGet(request);
connect(reply, &QCNetworkReply::finished, [reply]() {
    if (reply->error() == QCurl::NetworkError::NoError) {
        qDebug() << "Response:" << reply->readAll().value();
    }
    reply->deleteLater();
});
```

#### QCNetworkRequest

**职责**: 请求配置类，包含 URL、Header、SSL、代理、超时等配置

**主要方法**:
```cpp
// URL
void setUrl(const QUrl &url);
QUrl url() const;

// HTTP Headers
void setRawHeader(const QByteArray &headerName, const QByteArray &headerValue);
QByteArray rawHeader(const QByteArray &headerName) const;
QList<QByteArray> rawHeaderList() const;

// SSL 配置
void setSslConfig(const QCNetworkSslConfig &config);
QCNetworkSslConfig sslConfig() const;

// 代理配置
void setProxyConfig(const QCNetworkProxyConfig &config);
QCNetworkProxyConfig proxyConfig() const;

// 超时配置
void setTimeoutConfig(const QCNetworkTimeoutConfig &config);
QCNetworkTimeoutConfig timeoutConfig() const;

// HTTP 版本
void setHttpVersion(const QCNetworkHttpVersion &version);
QCNetworkHttpVersion httpVersion() const;

// 重定向
void setFollowLocation(bool followLocation);
bool followLocation() const;

// Range 请求
void setRange(qint64 start, qint64 end);
```

**流式 API**:
```cpp
QCNetworkRequest request = QCNetworkRequest(url)
    .withHeader("User-Agent", "QCurl/2.0")
    .withTimeout(30)
    .withFollowRedirects(true)
    .withHttpVersion(QCNetworkHttpVersion::Http2);
```

#### QCNetworkReply

**职责**: 统一的网络响应对象，支持所有 HTTP 方法和同步/异步模式

**主要信号**:
```cpp
signals:
    void finished();
    void readyRead();
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void uploadProgress(qint64 bytesSent, qint64 bytesTotal);
    void error(QCurl::NetworkError errorCode);
    void stateChanged(QCurl::ReplyState state);
```

**主要方法**:
```cpp
// 执行请求
void execute();
void abort();

// 读取数据
std::optional<QByteArray> readAll();
std::optional<QByteArray> peek(qint64 maxSize);
qint64 bytesAvailable() const;

// 状态查询
bool isRunning() const;
bool isFinished() const;
QCurl::ReplyState state() const;

// 错误处理
QCurl::NetworkError error() const;
QString errorString() const;

// HTTP 响应
int httpStatusCode() const;
QList<RawHeaderPair> rawHeaderPairs() const;
QByteArray rawHeader(const QByteArray &headerName) const;

// 元数据
QUrl url() const;
QUrl redirectUrl() const;
```

**同步回调函数** (可选，用于同步模式):
```cpp
// 数据接收回调
void setWriteFunction(const DataFunction &func);
void setCustomHeaderFunction(const DataFunction &func);

// 数据发送回调
void setReadFunction(const DataFunction &func);
void setSeekFunction(const SeekFunction &func);

// 进度回调
void setProgressFunction(const ProgressFunction &func);
```

### 2. 流式 API

#### QCRequest (简洁流式 API)

**特点**: 极简链式调用，适合快速构建请求

```cpp
// GET 请求
auto *reply = QCRequest::get("https://api.example.com/users")
    .withHeader("Authorization", "Bearer token")
    .withTimeout(std::chrono::seconds(30))
    .send();

// POST JSON
auto *reply = QCRequest::post("https://api.example.com/users")
    .withJson({{"name", "John"}, {"age", 30}})
    .send();

// POST 表单
auto *reply = QCRequest::post("https://api.example.com/login")
    .withFormData({{"username", "admin"}, {"password", "123456"}})
    .send();

// 文件上传
auto *reply = QCRequest::post("https://api.example.com/upload")
    .withFile("avatar", "/path/to/photo.jpg")
    .send();
```

#### QCRequestBuilder (传统构建器)

**特点**: 可变状态，适合复杂配置场景

```cpp
QCRequestBuilder builder;
builder.setUrl("https://api.example.com/users");
builder.addHeader("Authorization", "Bearer token");
builder.setMethod(HttpMethod::Get);
builder.setTimeout(30);

QCNetworkRequest request = builder.build();
QCNetworkReply *reply = manager->sendGet(request);
```

#### QCNetworkRequestBuilder (流式构建器)

**特点**: 结合流式 API 和传统构建器的优点

```cpp
auto *reply = QCNetworkRequestBuilder::create(manager)
    .get("https://api.example.com/users")
    .header("Authorization", "Bearer token")
    .timeout(30)
    .cache(QCNetworkCachePolicy::PreferCache)
    .execute();
```

### 3. 协议支持

#### HTTP/1.1、HTTP/2、HTTP/3

**HTTP 版本控制**:
```cpp
QCNetworkHttpVersion version;

// HTTP/1.1
version.setVersion(QCNetworkHttpVersion::Version::Http1_1);

// HTTP/2
version.setVersion(QCNetworkHttpVersion::Version::Http2);

// HTTP/3 (需要 libcurl 8.0+)
version.setVersion(QCNetworkHttpVersion::Version::Http3);

// 自动降级 (HTTP/3 → HTTP/2 → HTTP/1.1)
version.setVersion(QCNetworkHttpVersion::Version::Http3);
version.setFallbackEnabled(true);

request.setHttpVersion(version);
```

**性能对比**:

| 指标 | HTTP/1.1 | HTTP/2 | HTTP/3 |
|------|---------|--------|--------|
| 单请求延迟 | 530 ms | 145 ms (-73%) | ~100 ms (-81%) |
| 5 并发请求 | 31,000 ms | ~15,000 ms | ~10,000 ms |
| 连接数 | 5 | 1 | 1 |
| 头部压缩 | 无 | HPACK | QPACK |
| 传输层 | TCP | TCP | QUIC (UDP) |

#### WebSocket

**QCWebSocket**: 完整 WebSocket 客户端实现

```cpp
auto *socket = new QCWebSocket(QUrl("wss://echo.websocket.org"));

// 启用压缩 (RFC 7692 permessage-deflate)
socket->setCompressionConfig(QCWebSocketCompressionConfig::defaultConfig());

// 设置自动重连
auto reconnectPolicy = QCWebSocketReconnectPolicy::standardReconnect();
reconnectPolicy.setMaxAttempts(5);
reconnectPolicy.setInitialDelay(1000);
socket->setReconnectPolicy(reconnectPolicy);

// 连接信号
connect(socket, &QCWebSocket::connected, [socket]() {
    qDebug() << "Connected!";
    socket->sendTextMessage("Hello WebSocket!");
});

connect(socket, &QCWebSocket::textMessageReceived, [](const QString &msg) {
    qDebug() << "Received:" << msg;
});

connect(socket, &QCWebSocket::binaryMessageReceived, [](const QByteArray &data) {
    qDebug() << "Received binary data:" << data.size() << "bytes";
});

connect(socket, &QCWebSocket::disconnected, []() {
    qDebug() << "Disconnected";
});

connect(socket, &QCWebSocket::error, [](const QString &error) {
    qDebug() << "Error:" << error;
});

socket->open();
```

**WebSocket 连接池**:
```cpp
auto *pool = new QCWebSocketPool();
pool->setMaxConnections(10);

auto *socket = pool->acquireConnection(QUrl("wss://example.com/ws"));
socket->sendTextMessage("Hello from pool!");
pool->releaseConnection(socket);
```

**WebSocket 压缩**:
```cpp
QCWebSocketCompressionConfig config;
config.setEnabled(true);
config.setServerMaxWindowBits(15);  // 压缩窗口大小
config.setClientMaxWindowBits(15);
config.setServerNoContextTakeover(false);  // 上下文复用
config.setClientNoContextTakeover(false);

socket->setCompressionConfig(config);

// 预设配置
socket->setCompressionConfig(QCWebSocketCompressionConfig::defaultConfig());  // 平衡
socket->setCompressionConfig(QCWebSocketCompressionConfig::maxCompressionConfig());  // 最大压缩
socket->setCompressionConfig(QCWebSocketCompressionConfig::minMemoryConfig());  // 最小内存
```

### 4. 性能优化

#### HTTP 缓存

**缓存策略**:
```cpp
// 创建内存缓存 (10 MB)
auto *cache = new QCNetworkMemoryCache();
cache->setMaximumSize(10 * 1024 * 1024);
manager->setCache(cache);

// 创建磁盘缓存 (100 MB)
auto *cache = new QCNetworkDiskCache();
cache->setCacheDirectory("/tmp/qcurl_cache");
cache->setMaximumSize(100 * 1024 * 1024);
manager->setCache(cache);

// 设置缓存策略
request.setCachePolicy(QCNetworkCachePolicy::PreferCache);  // 默认
request.setCachePolicy(QCNetworkCachePolicy::OnlyNetwork);  // 跳过缓存
request.setCachePolicy(QCNetworkCachePolicy::AlwaysCache);  // 总是返回缓存
```

**缓存策略选择**:

| 策略 | 行为 | 适用场景 |
|------|------|---------|
| `PreferCache` | 优先返回未过期缓存 | **默认**，大多数场景 |
| `OnlyNetwork` | 不读写缓存 | 敏感数据、实时数据 |
| `PreferNetwork` | 优先网络，失败后返回缓存 | 需要最新数据但允许降级 |
| `AlwaysCache` | 忽略过期时间，总是返回缓存 | 离线模式、静态资源 |
| `OnlyCache` | 仅使用缓存，无缓存则报错 | 强制离线模式 |

#### HTTP 连接池

**自动启用** (零配置):
```cpp
auto *manager = new QCNetworkAccessManager();
// 连接池已自动启用，默认配置：每个主机 6 个连接，最大 10 个主机
```

**自定义配置**:
```cpp
QCNetworkConnectionPoolConfig config;
config.setMaxConnectionsPerHost(10);        // 每个主机最大连接数
config.setMaxTotalConnections(50);          // 总连接数
config.setConnectionIdleTime(120);          // 连接空闲超时 (秒)
config.setPipeliningEnabled(true);          // HTTP/1.1 管道化
config.setMaxPipelineLength(5);             // 管道长度

manager->setConnectionPoolConfig(config);

// 预设配置
manager->setConnectionPoolConfig(QCNetworkConnectionPoolConfig::conservativeConfig());  // 保守
manager->setConnectionPoolConfig(QCNetworkConnectionPoolConfig::aggressiveConfig());    // 激进
manager->setConnectionPoolConfig(QCNetworkConnectionPoolConfig::http2OptimizedConfig()); // HTTP/2 优化
```

**性能提升**:
- 连接复用避免 TCP/TLS 握手开销
- 典型提升: 60-80% (连续请求相同主机)

### 5. 请求管理

#### 请求重试

```cpp
QCNetworkRetryPolicy policy;
policy.setMaxRetries(3);                    // 最大重试次数
policy.setInitialDelay(1000);               // 初始延迟 (毫秒)
policy.setBackoffMultiplier(2.0);           // 退避倍数
policy.setMaxDelay(10000);                  // 最大延迟
policy.setJitterEnabled(true);              // 启用抖动

// 设置可重试的错误
policy.setRetriableErrors({
    QCurl::NetworkError::Timeout,
    QCurl::NetworkError::ConnectionFailed,
    QCurl::NetworkError::TemporaryError
});

request.setRetryPolicy(policy);

// 预设策略
request.setRetryPolicy(QCNetworkRetryPolicy::conservativeRetry());  // 保守重试
request.setRetryPolicy(QCNetworkRetryPolicy::aggressiveRetry());    // 激进重试
request.setRetryPolicy(QCNetworkRetryPolicy::noRetry());            // 不重试
```

#### 优先级调度

```cpp
// 创建调度器
auto *scheduler = new QCNetworkRequestScheduler();
scheduler->setMaxConcurrent(5);             // 最大并发数
scheduler->setStrategy(QCNetworkRequestScheduler::Strategy::Priority);  // 优先级策略

manager->setScheduler(scheduler);

// 设置请求优先级
request.setPriority(QCNetworkRequestPriority::High);

// 优先级级别
enum class QCNetworkRequestPriority {
    Highest,    // 最高优先级
    High,       // 高优先级
    Normal,     // 正常优先级 (默认)
    Low,        // 低优先级
    Lowest,     // 最低优先级
    Background  // 后台优先级
};
```

**调度策略**:
- **FIFO**: 先进先出 (默认)
- **Priority**: 按优先级调度
- **Fair**: 公平调度 (轮询)

#### 取消令牌

```cpp
// 创建取消令牌
auto *token = new QCNetworkCancelToken();

// 将令牌关联到多个请求
QCNetworkReply *reply1 = manager->sendGet(request1);
reply1->setCancelToken(token);

QCNetworkReply *reply2 = manager->sendGet(request2);
reply2->setCancelToken(token);

// 一键取消所有关联请求
token->cancel();

// 设置超时自动取消
token->setCancelAfter(std::chrono::seconds(30));

// 监听取消事件
connect(token, &QCNetworkCancelToken::cancelled, []() {
    qDebug() << "All requests cancelled";
});
```

#### 中间件

```cpp
// 自定义中间件
class AuthMiddleware : public QCNetworkMiddleware {
public:
    void onRequest(QCNetworkRequest &request) override {
        // 自动添加认证头
        request.setRawHeader("Authorization", "Bearer " + m_token);
    }

    void onResponse(QCNetworkReply *reply) override {
        // 检查认证状态
        if (reply->httpStatusCode() == 401) {
            qDebug() << "Token expired, refreshing...";
            refreshToken();
        }
    }

private:
    QByteArray m_token;
    void refreshToken() { /* ... */ }
};

// 注册中间件
manager->addMiddleware(new AuthMiddleware());
manager->addMiddleware(new LoggingMiddleware());
manager->addMiddleware(new RetryMiddleware());
```

### 6. 监控与调试

#### 日志系统

```cpp
// 创建日志器
auto *logger = new QCNetworkLogger();
logger->setLevel(QCNetworkLogger::Level::Debug);
logger->setOutputFile("/tmp/qcurl.log");

manager->setLogger(logger);

// 自定义日志处理器
logger->setCustomHandler([](QCNetworkLogger::Level level, const QString &msg) {
    if (level >= QCNetworkLogger::Level::Warning) {
        // 发送到监控系统
        sendToMonitoring(msg);
    }
});

// 日志级别
enum class Level {
    Debug,      // 调试信息
    Info,       // 一般信息
    Warning,    // 警告
    Error,      // 错误
    Critical    // 严重错误
};
```

#### Mock 工具

```cpp
// 创建 Mock 处理器
auto *mockHandler = new QCNetworkMockHandler();

// 配置 Mock 响应
mockHandler->addMockResponse(
    "https://api.example.com/users",
    200,
    R"({"users": [{"id": 1, "name": "John"}]})",
    {{"Content-Type", "application/json"}}
);

mockHandler->addMockResponse(
    "https://api.example.com/error",
    500,
    "Internal Server Error"
);

manager->setMockHandler(mockHandler);

// 延迟模拟
mockHandler->setDelay(100);  // 模拟 100ms 延迟
```

### 7. 网络诊断

```cpp
auto *diagnostics = new QCNetworkDiagnostics();

// DNS 解析
auto dnsResult = diagnostics->resolveDNS("example.com");
qDebug() << "IPv4:" << dnsResult.ipv4Addresses;
qDebug() << "IPv6:" << dnsResult.ipv6Addresses;

// TCP 连接测试
auto tcpResult = diagnostics->testConnection("example.com", 443);
qDebug() << "Connected:" << tcpResult.success;
qDebug() << "Time:" << tcpResult.connectionTime << "ms";

// SSL 证书检查
auto sslResult = diagnostics->checkSSL("https://example.com");
qDebug() << "Valid:" << sslResult.valid;
qDebug() << "Issuer:" << sslResult.issuer;
qDebug() << "Expiry:" << sslResult.expiryDate;

// HTTP 探测 (时间分解)
auto httpResult = diagnostics->probeHTTP("https://example.com");
qDebug() << "DNS lookup:" << httpResult.dnsLookupTime << "ms";
qDebug() << "TCP connect:" << httpResult.connectTime << "ms";
qDebug() << "SSL handshake:" << httpResult.sslTime << "ms";
qDebug() << "First byte:" << httpResult.firstByteTime << "ms";
qDebug() << "Total:" << httpResult.totalTime << "ms";

// Ping 测试 (ICMP)
auto pingResult = diagnostics->ping("example.com", 4);
qDebug() << "Sent:" << pingResult.packetsSent;
qDebug() << "Received:" << pingResult.packetsReceived;
qDebug() << "Loss:" << pingResult.packetLoss << "%";
qDebug() << "Min/Avg/Max:" << pingResult.minTime << "/"
         << pingResult.avgTime << "/" << pingResult.maxTime << "ms";

// Traceroute
auto traceResult = diagnostics->traceroute("example.com", 30);
for (const auto &hop : traceResult.hops) {
    qDebug() << "Hop" << hop.hopNumber << ":" << hop.address
             << "(" << hop.time << "ms)";
}

// 一键综合诊断
auto fullDiag = diagnostics->diagnose("https://example.com");
qDebug() << fullDiag.toJson();  // 完整诊断报告
```

### 8. 文件操作

#### 流式下载/上传

```cpp
// 流式下载到文件
QFile file("/tmp/download.bin");
file.open(QIODevice::WriteOnly);

QCNetworkReply *reply = manager->downloadToDevice(
    QUrl("https://example.com/largefile.zip"),
    &file
);

connect(reply, &QCNetworkReply::finished, [&file]() {
    file.close();
    qDebug() << "Download complete";
});

// 流式上传
QFile uploadFile("/tmp/upload.bin");
uploadFile.open(QIODevice::ReadOnly);

QCNetworkReply *reply = manager->uploadFromDevice(
    QUrl("https://example.com/upload"),
    &uploadFile,
    "application/octet-stream"
);
```

#### 断点续传

```cpp
QCNetworkReply *reply = manager->downloadFileResumable(
    QUrl("https://example.com/largefile.zip"),
    "/tmp/download.zip"
);

connect(reply, &QCNetworkReply::downloadProgress, [](qint64 received, qint64 total) {
    qDebug() << "Progress:" << (received * 100 / total) << "%";
});

// 如果下载中断，再次调用相同方法会自动从断点继续
```

#### Multipart 表单

```cpp
QCMultipartFormData formData;

// 添加文本字段
formData.addTextField("userId", "12345");
formData.addTextField("description", "Profile photo");

// 添加文件
formData.addFileField("avatar", "/path/to/photo.jpg");
formData.addFileField("document", "/path/to/doc.pdf", "application/pdf");

// 发送
QCNetworkReply *reply = manager->postMultipart(
    QUrl("https://api.example.com/upload"),
    formData
);
```

---

## API 参考

### 核心类概览

| 类名 | 职责 | 头文件 |
|------|------|-------|
| `QCNetworkAccessManager` | 网络访问管理器 | `QCNetworkAccessManager.h` |
| `QCNetworkRequest` | 请求配置 | `QCNetworkRequest.h` |
| `QCNetworkReply` | 统一响应对象 | `QCNetworkReply.h` |
| `QCWebSocket` | WebSocket 客户端 | `QCWebSocket.h` |
| `QCRequest` | 流式 Request API | `QCRequest.h` |
| `QCRequestBuilder` | 传统构建器 API | `QCRequestBuilder.h` |
| `QCNetworkRequestBuilder` | 流式构建器 API | `QCNetworkRequestBuilder.h` |
| `QCMultipartFormData` | Multipart 表单数据 | `QCMultipartFormData.h` |
| `QCNetworkDiagnostics` | 网络诊断工具 | `QCNetworkDiagnostics.h` |

### 配置类

| 类名 | 用途 | 头文件 |
|------|------|-------|
| `QCNetworkSslConfig` | SSL/TLS 配置 | `QCNetworkSslConfig.h` |
| `QCNetworkProxyConfig` | 代理配置 | `QCNetworkProxyConfig.h` |
| `QCNetworkTimeoutConfig` | 超时配置 | `QCNetworkTimeoutConfig.h` |
| `QCNetworkHttpVersion` | HTTP 版本配置 | `QCNetworkHttpVersion.h` |
| `QCNetworkRetryPolicy` | 重试策略 | `QCNetworkRetryPolicy.h` |
| `QCNetworkConnectionPoolConfig` | 连接池配置 | `QCNetworkConnectionPoolConfig.h` |
| `QCWebSocketReconnectPolicy` | WebSocket 重连策略 | `QCWebSocketReconnectPolicy.h` |
| `QCWebSocketCompressionConfig` | WebSocket 压缩配置 | `QCWebSocketCompressionConfig.h` |

### 性能优化类

| 类名 | 用途 | 头文件 |
|------|------|-------|
| `QCNetworkCache` | 缓存基类 | `QCNetworkCache.h` |
| `QCNetworkMemoryCache` | 内存缓存 | `QCNetworkMemoryCache.h` |
| `QCNetworkDiskCache` | 磁盘缓存 | `QCNetworkDiskCache.h` |
| `QCNetworkConnectionPoolManager` | 连接池管理器 | `QCNetworkConnectionPoolManager.h` |
| `QCNetworkRequestScheduler` | 请求调度器 | `QCNetworkRequestScheduler.h` |
| `QCWebSocketPool` | WebSocket 连接池 | `QCWebSocketPool.h` |

### 监控与调试类

| 类名 | 用途 | 头文件 |
|------|------|-------|
| `QCNetworkLogger` | 日志系统 | `QCNetworkLogger.h` |
| `QCNetworkMiddleware` | 中间件基类 | `QCNetworkMiddleware.h` |
| `QCNetworkCancelToken` | 取消令牌 | `QCNetworkCancelToken.h` |
| `QCNetworkMockHandler` | Mock 工具 | `QCNetworkMockHandler.h` |

### 枚举类型

#### NetworkError

```cpp
enum class NetworkError {
    NoError = 0,                    // 无错误
    ConnectionFailed = 7,           // 连接失败
    Timeout = 28,                   // 超时
    SslHandshakeFailed = 35,        // SSL 握手失败
    TooManyRedirects = 47,          // 重定向过多
    HttpError = 1000,               // HTTP 错误基础值
    // HTTP 状态码映射: HttpError + 状态码
    // 例如: 404 Not Found = 1404
};
```

#### HttpMethod

```cpp
enum class HttpMethod {
    Head,       // HEAD 请求
    Get,        // GET 请求
    Post,       // POST 请求
    Put,        // PUT 请求
    Delete,     // DELETE 请求
    Patch       // PATCH 请求
};
```

#### ExecutionMode

```cpp
enum class ExecutionMode {
    Async,      // 异步执行
    Sync        // 同步执行
};
```

#### ReplyState

```cpp
enum class ReplyState {
    Idle,       // 空闲 (未开始)
    Running,    // 运行中
    Finished,   // 已完成
    Cancelled,  // 已取消
    Error       // 错误
};
```

#### QCNetworkCachePolicy

```cpp
enum class QCNetworkCachePolicy {
    AlwaysCache,      // 总是返回缓存
    PreferCache,      // 优先缓存 (默认)
    PreferNetwork,    // 优先网络
    OnlyNetwork,      // 仅网络
    OnlyCache         // 仅缓存
};
```

#### QCNetworkRequestPriority

```cpp
enum class QCNetworkRequestPriority {
    Highest,          // 最高优先级
    High,             // 高优先级
    Normal,           // 正常优先级 (默认)
    Low,              // 低优先级
    Lowest,           // 最低优先级
    Background        // 后台优先级
};
```

---

## 构建与部署

### 系统要求

| 组件 | 最低版本 | 推荐版本 | 说明 |
|------|---------|---------|------|
| CMake | 3.16 | 3.20+ | 构建系统 |
| Qt6 | 6.2 | 6.5+ | QtCore、QtNetwork |
| libcurl | 8.0 | 8.16+ | HTTP/3 需要 8.0+，WebSocket 需要 7.86+ |
| GCC | 11.0 | 12.0+ | Linux |
| Clang | 14.0 | 15.0+ | macOS |
| MSVC | 2019 | 2022 | Windows |

### 依赖安装

#### Ubuntu/Debian

```bash
sudo apt update
sudo apt install -y \
    cmake \
    build-essential \
    qt6-base-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    zlib1g-dev
```

#### Fedora/RHEL

```bash
sudo dnf install -y \
    cmake \
    gcc-c++ \
    qt6-qtbase-devel \
    libcurl-devel \
    openssl-devel \
    zlib-devel
```

#### macOS

```bash
brew install cmake qt@6 curl openssl zlib
```

#### Windows

```powershell
# 使用 vcpkg 安装依赖
vcpkg install qt6:x64-windows curl:x64-windows openssl:x64-windows zlib:x64-windows
```

### 构建步骤

#### 标准构建

```bash
git clone <repository-url> && cd QCurl
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)
```

#### CMake 选项

| 选项 | 默认值 | 说明 |
|------|-------|------|
| `BUILD_EXAMPLES` | ON | 构建示例程序 |
| `BUILD_TESTING` | ON | 构建测试套件 |
| `BUILD_BENCHMARKS` | ON | 构建性能基准测试 |
| `CMAKE_BUILD_TYPE` | Debug | 构建类型 (Debug/Release/RelWithDebInfo) |
| `CMAKE_INSTALL_PREFIX` | /usr/local | 安装路径 |

**示例**:
```bash
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_TESTING=OFF \
    -DCMAKE_INSTALL_PREFIX=/opt/qcurl
```

### 安装

```bash
sudo cmake --install build

# 验证安装
pkg-config --modversion qcurl
```

### 集成到项目

#### CMake 集成

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp)

find_package(QCurl REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE QCurl::QCurl Qt6::Core Qt6::Network)
```

#### pkg-config 集成

```bash
g++ -std=c++17 main.cpp $(pkg-config --cflags --libs qcurl qt6-core qt6-network) -o myapp
```

#### qmake 集成 (遗留)

```qmake
QT += core network
LIBS += -lQCurl
include(/path/to/qcurl/src/qcurl.pri)
```

### 条件编译

libcurl 版本检测会自动启用/禁用特性：

```cmake
# WebSocket 支持 (libcurl >= 7.86.0)
if(CURL_VERSION_STRING VERSION_GREATER_EQUAL "7.86.0")
    add_compile_definitions(QCURL_WEBSOCKET_SUPPORT)
endif()

# HTTP/2 支持 (libcurl >= 7.66.0)
if(CURL_VERSION_STRING VERSION_GREATER_EQUAL "7.66.0")
    add_compile_definitions(QCURL_HTTP2_SUPPORT)
endif()
```

代码中使用条件编译：

```cpp
#ifdef QCURL_WEBSOCKET_SUPPORT
    auto *socket = new QCWebSocket(url);
#else
    #error "WebSocket support requires libcurl >= 7.86.0"
#endif
```

### 打包

#### DEB 包 (Ubuntu/Debian)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build
cpack -G DEB
```

#### RPM 包 (Fedora/RHEL)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build
cpack -G RPM
```

#### TGZ 包 (通用)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build
cpack -G TGZ
```

---

## 开发指南

### 编码规范

**详细规范**: 参见 `Qt6_CPP17_Coding_Style.md`

#### 命名规范

```cpp
// 类名: PascalCase
class QCNetworkAccessManager { };

// 成员变量: m_camelCase
QString m_cookieFilePath;
int m_maxRetries;

// 私有实现: d_ptr / q_ptr
QCNetworkReplyPrivate *d_ptr;

// 函数/方法: camelCase
void sendRequest();
QByteArray readAll();

// 常量: k 前缀 + PascalCase
const int kDefaultTimeout = 30;

// 枚举类: enum class + PascalCase
enum class HttpMethod { Get, Post };

// 命名空间: PascalCase
namespace QCurl { }
```

#### 代码风格

```cpp
// 大括号风格: K&R
if (condition) {
    doSomething();
} else {
    doOtherwis();
}

// 缩进: 4 空格
void function() {
    if (condition) {
        doSomething();
    }
}

// 指针/引用符号位置: 靠近类型
void function(int *ptr, const QString &str);

// Qt 信号槽关键字
signals:
    void finished();

public slots:
    void onButtonClicked();
```

#### C++17 特性使用

```cpp
// [[nodiscard]] - 强制检查返回值
[[nodiscard]] std::optional<QByteArray> readAll();

// std::optional - 可空返回值
std::optional<int> parseInt(const QString &str);

// std::chrono - 时间类型
void setTimeout(std::chrono::milliseconds timeout);

// if-init - 初始化语句
if (auto result = calculate(); result > 0) {
    use(result);
}

// structured bindings - 结构化绑定
auto [success, value] = parse(str);

// constexpr - 编译期计算
constexpr int kBufferSize = 4096;
```

### 内存管理

#### Qt 对象树

```cpp
// 正确: parent 会自动删除子对象
auto *manager = new QCNetworkAccessManager(parent);

// 正确: 使用 deleteLater() 延迟删除
connect(reply, &QCNetworkReply::finished, [reply]() {
    // 处理响应
    reply->deleteLater();  // 安全删除
});

// 错误: 不要在信号处理中直接 delete
connect(reply, &QCNetworkReply::finished, [reply]() {
    delete reply;  // ❌ 不安全，可能导致崩溃
});
```

#### RAII

```cpp
// 正确: 使用 RAII 管理 curl 句柄
class QCCurlHandleManager {
public:
    QCCurlHandleManager() : m_handle(curl_easy_init()) {}
    ~QCCurlHandleManager() { if (m_handle) curl_easy_cleanup(m_handle); }

private:
    CURL* m_handle;
};

// 正确: QCNetworkCache 是 QObject，使用父子树管理生命周期
auto *cache = new QCNetworkMemoryCache(manager);
manager->setCache(cache);  // cache 由 manager 析构自动释放
```

### 错误处理

```cpp
// 使用 NetworkError 枚举
if (reply->error() != NetworkError::NoError) {
    qWarning() << "Request failed:" << reply->errorString();
    return;
}

// HTTP 错误检查
int statusCode = reply->httpStatusCode();
if (statusCode >= 400) {
    qWarning() << "HTTP error:" << statusCode;
}

// 使用 std::optional
auto data = reply->readAll();
if (!data) {
    qWarning() << "No data available";
    return;
}
qDebug() << "Received:" << data->size() << "bytes";
```

### 信号槽

```cpp
// 推荐: 使用新式信号槽 (编译期检查)
connect(reply, &QCNetworkReply::finished, this, &MyClass::onFinished);

// Lambda 捕获注意事项
connect(reply, &QCNetworkReply::finished, [reply, this]() {
    // reply 和 this 都需要显式捕获
    handleResponse(reply);
});

// 自动断开连接
connect(reply, &QCNetworkReply::finished, reply, [reply]() {
    // reply 作为 context 对象，当 reply 销毁时自动断开
    qDebug() << "Finished";
});
```

### 文档注释

```cpp
/**
 * @brief 发送 GET 请求
 *
 * @param request 请求配置对象
 * @return 网络响应对象，调用者需要调用 deleteLater() 释放
 *
 * @note 请求会自动启动 (已调用 execute())
 *
 * @par 示例
 * @code
 * QCNetworkRequest request(QUrl("https://example.com"));
 * QCNetworkReply *reply = manager->sendGet(request);
 * connect(reply, &QCNetworkReply::finished, [reply]() {
 *     qDebug() << reply->readAll().value();
 *     reply->deleteLater();
 * });
 * @endcode
 */
QCNetworkReply* sendGet(const QCNetworkRequest &request);
```

---

## 测试策略

### 测试分层

```
集成测试 (10%)
    ├── 真实网络请求
    ├── 完整功能验证
    └── 端到端测试

功能测试 (20%)
    ├── HTTP/2、HTTP/3
    ├── WebSocket
    ├── 文件传输
    └── 网络诊断

单元测试 (70%)
    ├── 请求配置
    ├── 响应处理
    ├── 错误处理
    ├── 缓存机制
    ├── 连接池
    └── 重试机制
```

### 测试覆盖

| 模块 | 测试文件 | 用例数 | 覆盖率 | 状态 |
|------|---------|-------|-------|------|
| 请求配置 | tst_QCNetworkRequest | 31 | 95% | ✅ |
| 响应处理 | tst_QCNetworkReply | 27 | 90% | ✅ |
| 错误处理 | tst_QCNetworkError | 15 | 100% | ✅ |
| 流式 API | tst_QCRequest | 25 | 85% | ✅ |
| Multipart | tst_QCMultipartFormData | 24 | 100% | ✅ |
| 文件传输 | tst_QCNetworkFileTransfer | 3 | 75% | ✅ |
| 重试机制 | tst_QCNetworkRetry | 18 | 85% | ✅ |
| HTTP/2 | tst_QCNetworkHttp2 | 15 | 70% | ✅ |
| HTTP/3 | tst_QCNetworkHttp3 | 10 | 65% | ⚠️ |
| WebSocket | tst_QCWebSocket | 20 | 80% | ✅ |
| 缓存 | tst_QCNetworkCache | 20 | 85% | ✅ |
| 连接池 | tst_QCNetworkConnectionPool | 18 | 80% | ✅ |
| 网络诊断 | tst_QCNetworkDiagnostics | 20 | 85% | ✅ |
| 集成测试 | tst_Integration | 27 | 90% | ✅ |
| **总计** | **25 个文件** | **~550** | **~83%** | **96.3%** |

### 运行测试

#### 准备测试环境

```bash
# 启动 httpbin 服务 (集成测试需要)
docker run -d -p 8935:80 --name qcurl-httpbin kennethreitz/httpbin

# 启动 WebSocket 服务器 (WebSocket 测试需要)
cd tests
npm install ws
node websocket_server.js &
```

#### 运行所有测试

```bash
cd build
ctest --output-on-failure
```

#### 运行特定测试

```bash
# 单元测试 (无需网络)
./tests/tst_QCNetworkRequest
./tests/tst_QCNetworkReply
./tests/tst_QCNetworkError

# 集成测试 (需要 httpbin)
./tests/tst_Integration

# 显示详细输出
./tests/tst_Integration -v2

# 只运行特定测试用例
./tests/tst_Integration testRealHttpGetRequest
```

#### 代码覆盖率

```bash
# 启用覆盖率统计
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage"
cmake --build build

# 运行测试
cd build
ctest

# 生成覆盖率报告
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
xdg-open coverage_report/index.html
```

#### 内存泄漏检测

```bash
valgrind --leak-check=full --show-leak-kinds=all \
    ./tests/tst_QCNetworkRequest
```

### 编写测试

#### 单元测试模板

```cpp
#include <QtTest/QtTest>
#include <QCNetworkRequest.h>

class TestQCNetworkRequest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // 测试套件初始化 (执行一次)
    }

    void init() {
        // 每个测试用例前执行
    }

    void cleanup() {
        // 每个测试用例后执行
    }

    void cleanupTestCase() {
        // 测试套件清理 (执行一次)
    }

    // 测试用例
    void testBasicFunctionality() {
        QCurl::QCNetworkRequest request(QUrl("https://example.com"));
        QCOMPARE(request.url().toString(), QString("https://example.com"));
    }

    void testHeaderManagement() {
        QCurl::QCNetworkRequest request(QUrl("https://example.com"));
        request.setRawHeader("User-Agent", "TestAgent/1.0");
        QCOMPARE(request.rawHeader("User-Agent"), QByteArray("TestAgent/1.0"));
    }

    void testErrorHandling() {
        QCurl::QCNetworkRequest request;
        QVERIFY(!request.url().isValid());
    }
};

QTEST_MAIN(TestQCNetworkRequest)
#include "tst_QCNetworkRequest.moc"
```

#### 异步测试 (信号间谍)

```cpp
void testAsyncRequest() {
    auto *manager = new QCNetworkAccessManager(this);
    QCurl::QCNetworkRequest request(QUrl("http://localhost:8935/get"));
    QCNetworkReply *reply = manager->sendGet(request);

    // 使用 QSignalSpy 等待信号
    QSignalSpy finishedSpy(reply, &QCNetworkReply::finished);
    QSignalSpy errorSpy(reply, &QCNetworkReply::error);

    // 等待 finished 信号 (最多 5 秒)
    QVERIFY(finishedSpy.wait(5000));

    // 验证无错误
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(reply->error(), QCurl::NetworkError::NoError);

    // 验证数据
    auto data = reply->readAll();
    QVERIFY(data.has_value());
    QVERIFY(data->size() > 0);

    reply->deleteLater();
}
```

---

## 性能与优化

### 性能基准

#### HTTP/1.1 vs HTTP/2 vs HTTP/3

**测试场景**: 真实网络请求 (Google、nghttp2.org、Cloudflare)

| 指标 | HTTP/1.1 | HTTP/2 | HTTP/3 | 改进 |
|------|---------|--------|--------|------|
| 单请求延迟 | 530 ms | 145 ms | ~100 ms | **-81%** |
| 5 并发请求 | 31,000 ms | ~15,000 ms | ~10,000 ms | **-68%** |
| 连接数 | 5 | 1 | 1 | **-80%** |
| 头部大小 | 100% | ~40% (HPACK) | ~35% (QPACK) | **-65%** |

#### 连接池性能

**测试场景**: 100 个连续请求到同一主机

| 配置 | 总时间 | 平均延迟 | 吞吐量 | 改进 |
|------|-------|---------|--------|------|
| 无连接池 | 53 秒 | 530 ms | 1.9 req/s | - |
| 连接池 (默认) | 18 秒 | 180 ms | 5.6 req/s | **+66%** |
| 连接池 (激进) | 12 秒 | 120 ms | 8.3 req/s | **+77%** |

#### WebSocket 连接池

**测试场景**: 1000 次 WebSocket 连接和消息发送

| 配置 | 总时间 | 平均延迟 | 改进 |
|------|-------|---------|------|
| 无连接池 | 2000 秒 | 2000 ms/conn | - |
| 连接池 (10 连接) | 10 秒 | 10 ms/conn | **-99.5%** |

#### 缓存性能

**测试场景**: 1000 个重复请求

| 配置 | 总时间 | 缓存命中率 | 网络请求数 | 改进 |
|------|-------|-----------|----------|------|
| 无缓存 | 53 秒 | 0% | 1000 | - |
| 内存缓存 | 5.3 秒 | 90% | 100 | **-90%** |
| 磁盘缓存 | 8.1 秒 | 90% | 100 | **-85%** |

### 性能优化建议

#### 1. 启用 HTTP/2 或 HTTP/3

```cpp
QCNetworkHttpVersion version;
version.setVersion(QCNetworkHttpVersion::Version::Http2);
// 或
version.setVersion(QCNetworkHttpVersion::Version::Http3);
version.setFallbackEnabled(true);  // 自动降级

request.setHttpVersion(version);
```

**收益**: 延迟降低 70-80%，连接数减少 80%

#### 2. 配置连接池

```cpp
// 使用激进配置
manager->setConnectionPoolConfig(
    QCNetworkConnectionPoolConfig::aggressiveConfig()
);
```

**收益**: 连续请求性能提升 60-80%

#### 3. 启用缓存

```cpp
// 内存缓存 (高频访问)
auto *cache = new QCNetworkMemoryCache();
cache->setMaximumSize(50 * 1024 * 1024);  // 50 MB
manager->setCache(cache);

// 磁盘缓存 (持久化)
auto *cache = new QCNetworkDiskCache();
cache->setCacheDirectory("/tmp/qcurl_cache");
cache->setMaximumSize(500 * 1024 * 1024);  // 500 MB
manager->setCache(cache);
```

**收益**: 缓存命中时性能提升 90%+

#### 4. 使用优先级调度

```cpp
auto *scheduler = new QCNetworkRequestScheduler();
scheduler->setMaxConcurrent(10);  // 增加并发数
scheduler->setStrategy(QCNetworkRequestScheduler::Strategy::Priority);
manager->setScheduler(scheduler);

// 关键请求使用高优先级
criticalRequest.setPriority(QCNetworkRequestPriority::Highest);
```

**收益**: 关键请求延迟降低 30-50%

#### 5. WebSocket 压缩

```cpp
socket->setCompressionConfig(
    QCWebSocketCompressionConfig::maxCompressionConfig()
);
```

**收益**: 数据传输量减少 60-80% (文本消息)

#### 6. 批量请求优化

```cpp
// 错误: 顺序请求 (总时间 = n * 延迟)
for (const auto &url : urls) {
    auto *reply = manager->sendGet(QCNetworkRequest(url));
    // 等待完成...
}

// 正确: 并发请求 (总时间 ≈ 延迟)
QVector<QCNetworkReply*> replies;
for (const auto &url : urls) {
    replies.append(manager->sendGet(QCNetworkRequest(url)));
}
// 异步等待所有完成...
```

**收益**: 批量请求总时间降低 80-90%

### 内存优化

#### 1. 控制缓存大小

```cpp
cache->setMaximumSize(10 * 1024 * 1024);  // 限制内存使用
```

#### 2. 使用流式下载

```cpp
// 错误: 一次性读取大文件到内存
auto data = reply->readAll();  // 可能占用数百 MB

// 正确: 流式写入文件
QFile file("/tmp/download.bin");
file.open(QIODevice::WriteOnly);
manager->downloadToDevice(url, &file);
```

#### 3. 及时释放对象

```cpp
connect(reply, &QCNetworkReply::finished, [reply]() {
    // 处理完成后立即释放
    reply->deleteLater();
});
```

---

## 故障排查

### 常见问题

#### Q1: 编译错误: "curl/curl.h: No such file or directory"

**原因**: 未安装 libcurl 开发包

**解决方法**:
```bash
# Ubuntu/Debian
sudo apt install libcurl4-openssl-dev

# Fedora/RHEL
sudo dnf install libcurl-devel

# macOS
brew install curl
```

#### Q2: 运行时错误: "undefined symbol: curl_easy_init"

**原因**: 未链接 libcurl 库

**解决方法**:
```cmake
# CMakeLists.txt
find_package(CURL REQUIRED)
target_link_libraries(your_app PRIVATE CURL::libcurl)
```

#### Q3: WebSocket 不可用

**原因**: libcurl 版本 < 7.86.0

**解决方法**:
```bash
# 检查版本
curl --version

# 升级或从源码编译
# 参见 docs/HTTP3_GUIDE.md
```

#### Q4: HTTP/3 不工作

**原因**: libcurl 未启用 HTTP/3 支持

**解决方法**:
```bash
# 检查 HTTP/3 支持
curl --version | grep HTTP3

# 需要从源码编译 libcurl + nghttp3 + ngtcp2
# 详见 docs/HTTP3_GUIDE.md
```

#### Q5: 请求超时

**原因**: 默认超时太短或网络问题

**解决方法**:
```cpp
QCNetworkTimeoutConfig timeout;
timeout.setConnectTimeout(std::chrono::seconds(10));  // 连接超时
timeout.setTotalTimeout(std::chrono::seconds(60));    // 总超时
request.setTimeoutConfig(timeout);
```

#### Q6: SSL 证书错误

**原因**: 证书验证失败

**解决方法**:
```cpp
QCNetworkSslConfig ssl;
ssl.setPeerVerification(false);  // 跳过验证 (不推荐生产环境)
// 或
ssl.setCaCertificatePath("/path/to/ca-bundle.crt");  // 指定 CA 证书
request.setSslConfig(ssl);
```

#### Q7: 集成测试失败: "Connection refused"

**原因**: httpbin 服务未启动

**解决方法**:
```bash
docker run -d -p 8935:80 --name qcurl-httpbin kennethreitz/httpbin
```

#### Q8: 内存泄漏

**原因**: 忘记调用 `deleteLater()`

**解决方法**:
```cpp
// 正确: 使用 deleteLater()
connect(reply, &QCNetworkReply::finished, [reply]() {
    reply->deleteLater();
});

// 错误: 忘记释放
connect(reply, &QCNetworkReply::finished, []() {
    // reply 泄漏
});
```

#### Q9: 程序崩溃

**原因**: 可能的原因
1. 在信号处理中直接 delete 对象
2. 访问已删除的对象
3. 线程安全问题

**调试方法**:
```bash
# 使用调试器
gdb --args ./your_app

# 使用 valgrind
valgrind --track-origins=yes ./your_app

# 启用核心转储
ulimit -c unlimited
./your_app
gdb ./your_app core
```

### 日志调试

#### 启用详细日志

```cpp
auto *logger = new QCNetworkLogger();
logger->setLevel(QCNetworkLogger::Level::Debug);
logger->setOutputFile("/tmp/qcurl_debug.log");
manager->setLogger(logger);

// libcurl 详细输出
setenv("CURLOPT_VERBOSE", "1", 1);
```

#### 分析日志

```bash
# 查看请求日志
grep "Request:" /tmp/qcurl_debug.log

# 查看错误
grep "ERROR" /tmp/qcurl_debug.log

# 统计请求类型
grep "Method:" /tmp/qcurl_debug.log | sort | uniq -c
```

### 网络诊断

```cpp
auto *diagnostics = new QCNetworkDiagnostics();

// 全面诊断
auto report = diagnostics->diagnose("https://api.example.com");
qDebug() << report.toJson();

// 具体诊断
auto dnsResult = diagnostics->resolveDNS("api.example.com");
auto tcpResult = diagnostics->testConnection("api.example.com", 443);
auto sslResult = diagnostics->checkSSL("https://api.example.com");
auto httpResult = diagnostics->probeHTTP("https://api.example.com");
```

---

## 扩展开发

### 添加新 HTTP 方法

```cpp
// 1. 在 HttpMethod 枚举中添加
enum class HttpMethod {
    // ...
    Options  // 新方法
};

// 2. 在 QCNetworkAccessManager 中添加方法
QCNetworkReply* sendOptions(const QCNetworkRequest &request) {
    return createReply(request, HttpMethod::Options, ExecutionMode::Async);
}

// 3. 在 QCNetworkReply 中处理
void QCNetworkReplyPrivate::setupCurlOptions() {
    switch (m_method) {
    case HttpMethod::Options:
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "OPTIONS");
        break;
    // ...
    }
}
```

### 添加自定义中间件

```cpp
class CustomMiddleware : public QCNetworkMiddleware {
public:
    void onRequest(QCNetworkRequest &request) override {
        // 请求前处理
        request.setRawHeader("X-Custom-Header", "value");
        qDebug() << "Request:" << request.url();
    }

    void onResponse(QCNetworkReply *reply) override {
        // 响应后处理
        qDebug() << "Response:" << reply->httpStatusCode();

        // 错误处理
        if (reply->error() != NetworkError::NoError) {
            handleError(reply);
        }
    }

private:
    void handleError(QCNetworkReply *reply) {
        // 自定义错误处理逻辑
    }
};

// 注册
manager->addMiddleware(new CustomMiddleware());
```

### 添加自定义缓存策略

```cpp
class CustomCache : public QCNetworkCache {
public:
    void insert(const QString &key, const QByteArray &data,
                const QCNetworkCacheMetadata &metadata) override {
        // 自定义存储逻辑 (如 Redis)
        redis->set(key, data);
    }

    std::optional<CacheEntry> get(const QString &key) override {
        // 自定义读取逻辑
        if (auto data = redis->get(key)) {
            return CacheEntry{*data, metadata};
        }
        return std::nullopt;
    }

    void remove(const QString &key) override {
        redis->del(key);
    }

    void clear() override {
        redis->flushdb();
    }

    qint64 size() const override {
        return redis->dbsize();
    }
};

// 使用
manager->setCache(new CustomCache());
```

### 扩展网络诊断

```cpp
// 添加新诊断方法
class QCNetworkDiagnostics {
public:
    // 新方法: 带宽测试
    struct BandwidthResult {
        double downloadSpeed;  // Mbps
        double uploadSpeed;    // Mbps
        qint64 latency;        // ms
    };

    BandwidthResult testBandwidth(const QUrl &url, int duration = 10);

    // 新方法: MTU 探测
    struct MtuResult {
        int mtu;               // 最大传输单元
        bool pathMtuDiscovery; // 是否支持路径 MTU 发现
    };

    MtuResult probeMtu(const QString &host);
};
```

### 贡献代码

#### 1. Fork 和克隆

```bash
# Fork 项目到你的 GitHub 账号
# 然后克隆
git clone https://github.com/your-username/QCurl.git
cd QCurl
git remote add upstream <原始仓库URL>
```

#### 2. 创建特性分支

```bash
git checkout -b feature/my-awesome-feature
```

#### 3. 遵循编码规范

- 查看 `Qt6_CPP17_Coding_Style.md`
- 使用 `clang-format` 格式化代码
- 添加 Doxygen 注释

#### 4. 编写测试

```cpp
// tests/tst_MyFeature.cpp
class TestMyFeature : public QObject {
    Q_OBJECT
private slots:
    void testBasicFunctionality();
    void testEdgeCases();
    void testErrorHandling();
};
```

#### 5. 运行测试

```bash
cmake --build build
cd build && ctest --output-on-failure
```

#### 6. 提交代码

```bash
git add .
git commit -m "feat: Add awesome feature

- Implemented feature X
- Added tests
- Updated documentation"

git push origin feature/my-awesome-feature
```

#### 7. 创建 Pull Request

- 在 GitHub 上创建 PR
- 描述变更和动机
- 等待代码审查

---

## 附录

### 项目结构

```
QCurl/
├── CMakeLists.txt                 # 主 CMake 配置
├── README.md                      # 项目总览
├── CLAUDE.md                      # AI 上下文文档
├── CHANGELOG.md                   # 版本历史
├── Qt6_CPP17_Coding_Style.md      # 编码规范
├── src/                           # 源代码目录
│   ├── CMakeLists.txt
│   ├── QCNetworkAccessManager.{h,cpp}
│   ├── QCNetworkRequest.{h,cpp}
│   ├── QCNetworkReply.{h,cpp}
│   ├── QCWebSocket.{h,cpp}
│   ├── QCRequest.{h,cpp}
│   ├── QCMultipartFormData.{h,cpp}
│   ├── QCNetworkDiagnostics.{h,cpp}
│   ├── QCNetworkCache.{h,cpp}
│   ├── QCNetworkConnectionPoolManager.{h,cpp}
│   ├── QCNetworkLogger.{h,cpp}
│   ├── QCNetworkMiddleware.{h,cpp}
│   └── ... (72 个源文件)
├── tests/                         # 测试套件
│   ├── CMakeLists.txt
│   ├── tst_QCNetworkRequest.cpp
│   ├── tst_QCNetworkReply.cpp
│   ├── tst_Integration.cpp
│   └── ... (25 个测试文件，~550 测试用例)
├── examples/                      # 示例程序
│   ├── QCurl/                     # 基础 GUI 示例
│   ├── WebSocketDemo/             # WebSocket 示例
│   ├── FileTransferDemo/          # 文件传输示例
│   ├── Http2Demo/                 # HTTP/2 示例
│   ├── Http3Demo/                 # HTTP/3 示例
│   └── ... (19 个示例程序)
├── benchmarks/                    # 性能基准测试
│   ├── benchmark_http2.cpp
│   ├── benchmark_http3.cpp
│   ├── benchmark_websocket_pool.cpp
│   └── ... (7 个基准测试)
└── docs/                          # 技术文档
    ├── HTTP2_BENCHMARK_REPORT.md
    ├── HTTP3_GUIDE.md
    ├── CACHE_AND_CONNECTION_POOL_DESIGN.md
    └── ...
```

### 文件统计

| 类别 | 数量 | 说明 |
|------|-----|------|
| 源文件 (.h/.cpp) | 72 | 核心库代码 |
| 测试文件 | 25 | 约 550 个测试用例 |
| 示例程序 | 19 | 涵盖各种使用场景 |
| 基准测试 | 7 | 性能测试 |
| 文档 | 14+ | Markdown 文档 |
| **总代码量** | **约 50,000 行** | 包括测试和示例 |

### 关键术语表

| 术语 | 说明 |
|------|------|
| **libcurl** | 底层 HTTP 传输库 |
| **RAII** | Resource Acquisition Is Initialization (资源获取即初始化) |
| **Pimpl** | Pointer to Implementation (指向实现的指针) |
| **Fluent API** | 流式 API，支持链式调用 |
| **Multipart** | MIME multipart/form-data 格式 |
| **HPACK** | HTTP/2 头部压缩算法 |
| **QPACK** | HTTP/3 头部压缩算法 (基于 HPACK) |
| **QUIC** | 快速 UDP 互联网连接 (HTTP/3 传输层) |
| **permessage-deflate** | WebSocket 压缩扩展 (RFC 7692) |
| **LRU** | Least Recently Used (最近最少使用) 缓存淘汰策略 |
| **Exponential Backoff** | 指数退避算法 (重试延迟策略) |

### 相关资源

#### 官方文档

- [Qt6 官方文档](https://doc.qt.io/qt-6/)
- [libcurl 官方文档](https://curl.se/libcurl/)
- [HTTP/2 RFC 7540](https://datatracker.ietf.org/doc/html/rfc7540)
- [HTTP/3 RFC 9114](https://datatracker.ietf.org/doc/html/rfc9114)
- [WebSocket RFC 6455](https://datatracker.ietf.org/doc/html/rfc6455)
- [WebSocket 压缩 RFC 7692](https://datatracker.ietf.org/doc/html/rfc7692)

#### 相关项目

- [Qt Network](https://doc.qt.io/qt-6/qtnetwork-index.html) - Qt 原生网络模块
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) - C++ HTTP 库
- [Boost.Beast](https://www.boost.org/doc/libs/1_81_0/libs/beast/) - HTTP 和 WebSocket 库
- [libcpr](https://github.com/libcpr/cpr) - C++ 的 curl 封装

---

**文档版本**: 1.0
**最后更新**: 2025-01-26
**维护者**: QCurl 项目团队
**许可证**: MIT License

---

**如有疑问或需要帮助，请参考**:
- [GitHub Issues](https://github.com/user/QCurl/issues)
- [项目 Wiki](https://github.com/user/QCurl/wiki)
- 邮件联系: qcurl@example.com
