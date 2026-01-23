# QCurl HTTP/3 使用指南

> **版本**: v2.17.0  
> **更新**: 2025-11-17

---

## 📋 目录

1. [HTTP/3 简介](#http3-简介)
2. [依赖要求](#依赖要求)
3. [快速开始](#快速开始)
4. [使用场景](#使用场景)
5. [性能优化](#性能优化)
6. [故障排查](#故障排查)

---

## HTTP/3 简介

HTTP/3 是 HTTP 协议的最新版本，基于 QUIC 传输层协议（UDP），提供：

- ✅ **更快的连接建立** - 0-RTT 恢复
- ✅ **更好的丢包恢复** - 无队头阻塞
- ✅ **内置加密** - 强制 TLS 1.3
- ✅ **连接迁移** - 网络切换不断连

---

## 依赖要求

### libcurl 版本

```bash
# 检查 libcurl 版本和 HTTP/3 支持
curl --version | grep HTTP3

# 期望输出包含:
# libcurl/8.17.0 ... HTTP3
```

### 编译要求

- libcurl >= 7.66.0
- nghttp3 (HTTP/3 layer)
- ngtcp2 (QUIC implementation)

---

## 快速开始

### 1. 基本 HTTP/3 请求

```cpp
#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkHttpVersion.h"

using namespace QCurl;

QCNetworkAccessManager manager;

QCNetworkRequest request(QUrl("https://cloudflare-quic.com"));
request.setHttpVersion(QCNetworkHttpVersion::Http3);  // 启用 HTTP/3

auto *reply = manager.sendGet(request);

connect(reply, &QCNetworkReply::finished, [reply]() {
    if (reply->error() == NetworkError::NoError) {
        auto data = reply->readAll();
        qDebug() << "HTTP/3 请求成功!";
    }
    reply->deleteLater();
});

reply->execute();
```

### 2. Http3Only 模式（严格）

```cpp
// 仅使用 HTTP/3，失败则报错（不降级）
request.setHttpVersion(QCNetworkHttpVersion::Http3Only);
```

### 3. 自动协商

```cpp
// 让 libcurl 自动选择最优版本
request.setHttpVersion(QCNetworkHttpVersion::HttpAny);
```

---

## 使用场景

### 场景 1: 移动网络

HTTP/3 的连接迁移特性适合移动场景：

```cpp
// 移动应用推荐使用 Http3
request.setHttpVersion(QCNetworkHttpVersion::Http3);
```

### 场景 2: 高延迟网络

0-RTT 恢复显著减少延迟：

```cpp
// 跨国请求适合 HTTP/3
request.setHttpVersion(QCNetworkHttpVersion::Http3);
```

### 场景 3: 丢包网络

QUIC 的独立流恢复避免队头阻塞。

---

## 性能优化

### 1. 连接复用

```cpp
// 使用相同的 Manager 复用连接
QCNetworkAccessManager manager;  // 单例或成员变量

// 多次请求会自动复用 HTTP/3 连接
auto *reply1 = manager.sendGet(request1);
auto *reply2 = manager.sendGet(request2);
```

### 2. 连接池配置

```cpp
// HTTP/3 配合连接池效果更好
#include "QCNetworkConnectionPoolConfig.h"

auto poolConfig = QCNetworkConnectionPoolConfig::http2OptimizedConfig();
// HTTP/3 也会受益于连接池优化
```

---

## 故障排查

### Q: HTTP/3 请求失败？

**检查清单**:
1. libcurl 版本是否 >= 7.66.0
2. 编译时是否启用 HTTP/3
3. 服务器是否支持 HTTP/3
4. 防火墙是否阻止 UDP 流量（QUIC 基于 UDP）

**检查命令**:
```cpp
curl_version_info_data *ver = curl_version_info(CURLVERSION_NOW);
if (ver->features & CURL_VERSION_HTTP3) {
    qDebug() << "HTTP/3 supported";
} else {
    qDebug() << "HTTP/3 not supported";
}
```

### Q: HTTP/3 比 HTTP/2 慢？

**可能原因**:
- 首次连接的握手开销
- 网络环境不稳定
- 服务器实现不够优化

**建议**: 多次测试取平均值，关注后续请求性能。

### Q: UDP 被阻止？

HTTP/3 使用 UDP 端口 443，某些网络环境可能阻止。

**解决方案**:
```cpp
// 使用自动降级
request.setHttpVersion(QCNetworkHttpVersion::Http3);  // 会自动降级到 HTTP/2
```

---

## 参考资源

- [RFC 9114 - HTTP/3](https://www.rfc-editor.org/rfc/rfc9114)
- [RFC 9000 - QUIC](https://www.rfc-editor.org/rfc/rfc9000)
- [libcurl HTTP/3 文档](https://curl.se/docs/http3.html)
- [Cloudflare QUIC 测试](https://cloudflare-quic.com)

---

## 示例程序

完整示例请参考: `examples/Http3Demo/`

```bash
cd build
./examples/Http3Demo/Http3Demo 4  # 运行性能对比
```

---

**版本**: v2.17.0  
**作者**: QCurl Team  
**许可**: 与 QCurl 主项目相同
