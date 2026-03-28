# 快速开始

本页只保留最小上手路径：构建 QCurl、可选安装，以及跑通一个最小请求示例。

## 1. 依赖

- CMake 3.16+
- Qt6（QtCore / QtNetwork）
- libcurl 7.85.0+（WebSocket 需 7.86.0+；HTTP/3 推荐 8.16.0+ 且带 QUIC backend）
- 编译器：GCC 11+ / Clang 14+（C++17）

## 2. 构建（默认启用 examples/tests）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

测试运行与门禁（offline / env / libcurl_consistency / basic-no-problem）统一参考：

- [`docs/dev/build-and-test.md`](../dev/build-and-test.md)

## 3. 安装（可选）

如需 `find_package(QCurl)` / `pkg-config qcurl`，可执行安装：

```bash
sudo cmake --install build
```

## 4. 在你的项目中使用

### CMake（推荐）

```cmake
find_package(QCurl CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE QCurl::QCurl)
```

### pkg-config

```bash
g++ your_app.cpp $(pkg-config --cflags --libs qcurl) -o your_app
```

## 5. 最小请求示例

示例代码可参考根目录 `README.md` 中的 “代码示例-简单 GET 请求”。

优先级提示：

- `High/VeryHigh`：与用户交互直接相关、希望优先处理且仍遵守并发/每主机限制的请求（例如页面数据加载、登录/下单/支付确认）。
- `Critical`：适用于少量需要尽快启动的控制/紧急请求；该优先级会绕过 pending 队列，且当前实现可能突破并发/每主机限制，建议仅在明确需要时使用。

最小示例（约 5 行）：

```cpp
QCNetworkAccessManager mgr;
mgr.enableRequestScheduler(true);
QCNetworkRequest req(QUrl("https://example.com")); // 默认 Normal
req.setPriority(QCNetworkRequestPriority::High);   // 推荐：High/VeryHigh
auto *reply = mgr.sendGet(req);
```

`reply` 仍应按 Qt 习惯 `deleteLater()` 释放，或在 `finished` 后由你的上层对象接管销毁。
