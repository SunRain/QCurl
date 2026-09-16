# QCurl

基于 Qt6 / C++17 和 libcurl 的网络库：异步 HTTP、请求配置、lane 调度、缓存、流式传输，以及显式 opt-in 的扩展组件。

## 当前版本与消费边界

`v1.0.0` 是已发布历史，`2.0.0` 是当前开发候选。Core 在 2.x 内保证源码兼容，**ABI 非稳定；每次更新都必须重新编译和链接下游**。`SOVERSION 2` 不代表二进制兼容，候选说明不代表已经通过发布验收。

| 逻辑组件 | 消费方式与边界 |
| --- | --- |
| Core | 默认 `find_package(QCurl CONFIG REQUIRED)` / `QCurl::QCurl`；异步 HTTP 与公共配置、调度、缓存和传输 API |
| Blocking Extras | 显式 `COMPONENTS BlockingExtras` / `QCurl::BlockingExtras`；同步值结果，实现随 Core 物理库交付 |
| Other Extras | 显式 `COMPONENTS OtherExtras` / `QCurl::OtherExtras`；Diagnostics、WebSocket 为 Preview，Middleware Extras 为 Stable，均非默认 Core |
| Test Support | 显式 `COMPONENTS TestSupport` / `QCurl::TestSupport`；仅供开发测试的静态库，不是生产 Runtime |

Preview 是成熟度，Internal 是可见性，不是额外组件。精确安装面见[公共头边界](docs/dev/architecture/public-header-boundary.md)，发布承诺见[正式合同](docs/dev/release/2.0.0-hard-break-release-contract.md)。

## 依赖

- CMake 3.16+、C++17 编译器（GCC 11+ / Clang 14+ / MSVC 2019+）。
- **Qt 6.10.3+**；源码构建需要 QtCore、QtNetwork，默认 Core consumer 只依赖 QtCore。Qt 6.10.0–6.10.2 不受支持。
- libcurl 7.85.0+、zlib 开发包；WebSocket 需要 libcurl 7.86.0+，HTTP/3 还取决于 QUIC backend 与服务端能力，见[HTTP 版本配置](docs/user/configuration.md#http-version)。

## 用法预览

下面仅展示 Core 异步请求的发起，**不是独立程序**。它使用 [Quickstart 的完整 consumer 上下文](docs/user/quickstart.md#2-独立-core-consumer)：`app` 已创建并检查 URL 参数，manager 与 reply 在同一事件循环线程使用，完成信号负责响应与错误处理、回收 reply 和退出应用。

```cpp
QCurl::QCNetworkAccessManager manager;
QCurl::QCNetworkRequest request(QUrl{app.arguments().at(1)});
request.setTimeout(std::chrono::seconds(10));
auto *reply = manager.get(request);
```

manager 必须存活到请求结束。安装步骤、完整 CMakeLists/main.cpp、构建运行命令及本地 HTTP 成功/失败核对统一见 [Quickstart](docs/user/quickstart.md)，不要把本片段直接作为完整 main() 使用。

## 文档与社区

- [文档目录](docs/README.md)：用户与开发者/维护者两条路径。
- [版本变化](CHANGELOG.md) · [示例集合](examples/README.md)。
- [贡献指南](CONTRIBUTING.md) · [支持与反馈](SUPPORT.md) · [安全披露](SECURITY.md) · [行为准则](CODE_OF_CONDUCT.md)。
- [MIT 许可证](LICENSE) · [第三方许可](THIRD_PARTY_NOTICES.md)。

QCurl 使用 [Qt](https://www.qt.io/) 与 [libcurl](https://curl.se/libcurl/)。
