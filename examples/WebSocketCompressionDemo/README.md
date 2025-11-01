# WebSocketCompressionDemo

**版本**: v2.18.0  
**功能**: 演示 WebSocket permessage-deflate 压缩扩展（RFC 7692）

---

## 功能说明

演示 QCurl WebSocket 压缩功能的使用和效果。

### 演示 1: 压缩效果对比

对比启用和禁用压缩时的数据传输量。

```bash
./WebSocketCompressionDemo 1
```

**输出示例**:
```
--- 禁用压缩 ---
✅ 已连接（无压缩）
压缩协商: 否
收到回显: 1300 字节
统计: Compression not negotiated

--- 启用压缩 ---
✅ 已连接（启用压缩）
压缩协商: 是
收到回显: 1300 字节
统计: Sent: 1300 bytes -> 450 bytes (65.4% compression)
      Recv: 450 bytes <- 1300 bytes (65.4% compression)
```

### 演示 2: 不同压缩配置

测试三种预设配置的压缩效果：
- **默认配置**: 平衡压缩率和性能
- **低内存配置**: 较小窗口和压缩级别
- **最大压缩配置**: 最高压缩率

```bash
./WebSocketCompressionDemo 2
```

### 演示 3: 大消息压缩

测试不同大小消息（1KB, 10KB, 100KB）的压缩效果。

```bash
./WebSocketCompressionDemo 3
```

---

## 构建和运行

### 构建

```bash
cd build
cmake --build . --target WebSocketCompressionDemo
```

### 运行

```bash
# 运行演示 1
./examples/WebSocketCompressionDemo/WebSocketCompressionDemo 1

# 运行演示 2
./examples/WebSocketCompressionDemo/WebSocketCompressionDemo 2

# 运行演示 3
./examples/WebSocketCompressionDemo/WebSocketCompressionDemo 3
```

---

## 测试服务器

默认使用 `wss://echo.websocket.org`，该服务器支持 permessage-deflate 扩展。

**注意**: 
- 需要互联网连接
- 服务器必须支持 RFC 7692 压缩扩展

---

## 代码示例

### 启用默认压缩

```cpp
QCWebSocket socket(QUrl("wss://example.com"));

// 启用默认压缩配置
socket.setCompressionConfig(QCWebSocketCompressionConfig::defaultConfig());

connect(&socket, &QCWebSocket::connected, [&]() {
    qDebug() << "压缩协商:" << socket.isCompressionNegotiated();
    socket.sendTextMessage("Hello with compression!");
});

socket.open();
```

### 自定义压缩配置

```cpp
QCWebSocketCompressionConfig config;
config.enabled = true;
config.clientMaxWindowBits = 12;      // 4KB 窗口
config.compressionLevel = 6;           // 平衡
config.clientNoContextTakeover = false; // 保留上下文

socket.setCompressionConfig(config);
```

### 查看压缩统计

```cpp
connect(&socket, &QCWebSocket::textMessageReceived, [&](const QString &msg) {
    qDebug() << socket.compressionStats();
    // 输出: "Sent: 1000 bytes -> 400 bytes (60.0% compression)"
});
```

---

## 依赖

- Qt6 Core
- QCurl (v2.18.0+)
- libcurl with HTTP/3 support
- zlib

---

## 相关文档

- [RFC 7692 - Compression Extensions for WebSocket](https://www.rfc-editor.org/rfc/rfc7692)
- [QCWebSocketCompressionConfig API 文档](../../docs/WEBSOCKET_COMPRESSION_GUIDE.md)

---

**作者**: QCurl Team  
**许可**: 与 QCurl 主项目相同
