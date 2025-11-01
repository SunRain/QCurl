# Network Features Demo

QCurl v2.17-v2.19 新功能综合演示程序。

## 功能演示

### 1. HTTP/3 支持 (v2.17.0)

演示如何使用 HTTP/3 协议发送请求:

- 自动检测 libcurl 的 HTTP/3 支持
- 使用 `QCNetworkHttpVersion::Http3` 尝试 HTTP/3
- 自动降级到 HTTP/2 或 HTTP/1.1(如果不支持)

### 2. WebSocket 压缩扩展 (v2.18.0)

演示 RFC 7692 permessage-deflate 压缩:

- 配置压缩参数(窗口大小、压缩级别等)
- 与服务器协商压缩设置
- 查看压缩统计信息(压缩率、原始/压缩大小等)

### 3. 网络诊断工具 (v2.19.0)

演示完整的网络诊断功能:

- DNS 解析(正向和反向)
- TCP 连接测试
- SSL 证书检查(有效期、颁发者等)
- HTTP 探测(时间分解)
- 综合诊断(一键诊断所有项目)

## 编译

```bash
cd build
cmake ..
cmake --build .
```

## 运行

```bash
./examples/NetworkFeaturesDemo/NetworkFeaturesDemo
```

## 示例输出

```
========================================
QCurl 网络功能综合示例
v2.17-v2.19 新功能演示
========================================

【演示菜单】
1. HTTP/3 请求示例
2. WebSocket 压缩示例
3. 网络诊断示例
4. 综合演示（依次执行上述所有示例）

========================================
1. HTTP/3 请求示例
========================================

libcurl 版本: 8.17.0
HTTP/3 支持: ✅ 是

发送 HTTP/3 请求到: https://www.cloudflare.com
HTTP 版本: Http3（尝试 HTTP/3，失败则降级）

✅ HTTP/3 请求成功!
状态码: 200
响应大小: 98765 字节
Alt-Svc 头: h3=":443"; ma=86400

... (更多输出)
```

## 注意事项

### HTTP/3 要求

- libcurl >= 7.66.0
- 编译时启用 nghttp3/ngtcp2 支持
- 服务器支持 HTTP/3 (QUIC)

### WebSocket 压缩要求

- 编译时定义 `QCURL_WEBSOCKET_SUPPORT`
- 服务器支持 permessage-deflate 扩展

### 网络诊断要求

- 需要网络连接
- 某些功能可能需要外网访问(如 SSL 证书检查)
- 防火墙可能阻止某些端口的连接测试

## 相关文档

- [HTTP/3 详细文档](../../docs/http3-guide.md)
- [WebSocket 压缩文档](../../docs/websocket-compression.md)
- [网络诊断文档](../../docs/diagnostics-guide.md)

## 版本历史

- v2.19.0 - 添加网络诊断演示
- v2.18.0 - 添加 WebSocket 压缩演示
- v2.17.0 - 添加 HTTP/3 演示
