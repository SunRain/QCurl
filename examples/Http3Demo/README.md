# Http3Demo - HTTP/3 功能演示程序

## 📋 简介

这是 QCurl 库的 HTTP/3 功能演示程序，展示了如何使用 HTTP/3 协议进行网络请求。

## ✨ 功能演示

### 1. 基本 HTTP/3 请求
演示如何发送一个简单的 HTTP/3 请求。

```bash
./Http3Demo 1
```

### 2. HTTP/3 降级处理
演示当服务器不支持 HTTP/3 时的自动降级机制。

```bash
./Http3Demo 2
```

### 3. Http3Only 模式
演示强制使用 HTTP/3（不允许降级）的场景。

```bash
./Http3Demo 3
```

### 4. HTTP 版本性能对比
对比 HTTP/1.1、HTTP/2 和 HTTP/3 的性能差异。

```bash
./Http3Demo 4
```

### 5. HTTP 版本自动协商
演示让 libcurl 自动选择最优 HTTP 版本。

```bash
./Http3Demo 5
```

## 🔧 编译

```bash
cd build
cmake ..
cmake --build . --target Http3Demo
```

## 🚀 运行

### 默认运行（演示 1）
```bash
./examples/Http3Demo/Http3Demo
```

### 选择特定演示
```bash
./examples/Http3Demo/Http3Demo 4  # 运行性能对比
```

## 📊 依赖要求

### 必需
- Qt6 Core
- QCurl 库

### HTTP/3 支持要求
- libcurl >= 7.66.0
- 编译时支持 nghttp3/ngtcp2
- 支持 HTTP/3 的服务器

### 检查 HTTP/3 支持
```bash
curl --version | grep HTTP3
```

输出应包含 `HTTP3` 特性。

## 📝 示例输出

### 基本 HTTP/3 请求
```
========================================
演示 1: 基本 HTTP/3 请求
========================================
发送 HTTP/3 请求到: https://cloudflare-quic.com
✅ 请求成功!
   HTTP 状态码: 200
   响应大小: 1234 字节
   响应预览: <!DOCTYPE html>...
```

### 性能对比
```
========================================
演示 4: HTTP 版本性能对比
========================================
测试 URL: https://www.google.com
测试次数: 3 次

HTTP/1.1 迭代 1: 245 ms
HTTP/1.1 迭代 2: 238 ms
HTTP/1.1 迭代 3: 242 ms
HTTP/2 迭代 1: 189 ms
HTTP/2 迭代 2: 185 ms
HTTP/2 迭代 3: 187 ms
HTTP/3 迭代 1: 156 ms
HTTP/3 迭代 2: 152 ms
HTTP/3 迭代 3: 154 ms

========================================
性能对比结果:
========================================
HTTP/1.1 平均响应时间: 241 ms
HTTP/2 平均响应时间: 187 ms
HTTP/3 平均响应时间: 154 ms

🏆 最快版本: HTTP/3
```

## 💡 使用提示

### HTTP/3 最佳实践

1. **检查支持**
   程序启动时会自动检查 libcurl 是否支持 HTTP/3。

2. **选择合适的模式**
   - 一般场景：使用 `Http3`（允许降级）
   - 严格场景：使用 `Http3Only`（不允许降级）
   - 自动优化：使用 `HttpAny`（让 libcurl 选择）

3. **错误处理**
   HTTP/3 连接可能失败，应当正确处理错误：
   ```cpp
   if (reply->error() != NetworkError::NoError) {
       qDebug() << "错误:" << reply->errorString();
   }
   ```

4. **性能优化**
   - HTTP/3 首次连接可能较慢（QUIC 握手）
   - 后续请求会受益于 0-RTT 恢复
   - 适合高延迟网络环境

### 常见问题

#### Q: 为什么 HTTP/3 请求失败？
A: 可能的原因：
- libcurl 不支持 HTTP/3（需要 7.66.0+）
- 编译时未启用 nghttp3/ngtcp2
- 服务器不支持 HTTP/3
- 网络阻止 UDP 流量（HTTP/3 基于 QUIC/UDP）

#### Q: HTTP/3 比 HTTP/2 慢？
A: 可能的原因：
- 首次连接的握手开销
- 网络环境不稳定
- 服务器 HTTP/3 实现不够优化
- 建议多次测试取平均值

## 🔗 相关文档

- [QCurl HTTP/3 使用指南](../../docs/HTTP3_GUIDE.md)
- [CHANGELOG.md](../../CHANGELOG.md)
- [QCNetworkHttpVersion API 文档](../../src/QCNetworkHttpVersion.h)

## 📄 许可证

与 QCurl 主项目相同。

---

**版本**: v2.17.0  
**更新时间**: 2025-11-17
