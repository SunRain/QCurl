# QCurl 示例程序

本目录包含 QCurl 库的实用示例程序，展示如何在实际场景中使用 QCurl 进行网络编程。

## 📦 示例列表

### 1. FileDownloadDemo - 文件下载管理器

**位置**: `examples/FileDownloadDemo/`
**功能**: 演示如何实现一个完整的文件下载管理器

**核心特性**:
- ✅ 断点续传（使用 HTTP Range 请求）
- ✅ 下载队列管理（FIFO）
- ✅ 并发下载控制（默认 3 个，可调整 1-10）
- ✅ 实时进度跟踪和速度计算
- ✅ 暂停/恢复/取消操作
- ✅ 交互式命令行界面

**使用示例**:
```bash
./build/examples/FileDownloadDemo/FileDownloadDemo

# 选择操作：
# 1. 添加下载任务
# 2. 查看所有任务
# 3. 暂停所有任务
# 4. 恢复所有任务
# 8. 快速测试（下载3个示例文件）
```

**代码结构**:
- `DownloadTask` - 单个下载任务管理
- `DownloadManager` - 队列和并发控制
- `main.cpp` - 交互式演示

---

### 2. FileTransferDemo - 流式/断点续传示例

**位置**: `examples/FileTransferDemo/`
**功能**: 演示 `downloadToDevice()`、`uploadFromDevice()` 与 `downloadFileResumable()` 的组合用法

**核心特性**:
- ✅ `downloadToDevice()`：流式下载 64KB 数据到本地文件
- ✅ `uploadFromDevice()`：将下载的文件以 multipart/form-data 方式回传至 httpbin
- ✅ `downloadFileResumable()`：先人为取消，再自动继续下载，展示断点续传
- ✅ 环境自适应：可通过 `QCURL_HTTPBIN_BASE` 环境变量自定义 httpbin 地址

**运行前提**:
- 本地 httpbin 服务（`docker run -p 8935:80 kennethreitz/httpbin`）
- 或者可访问的远程 httpbin 实例

**使用示例**:
```bash
./build/examples/FileTransferDemo/FileTransferDemo
# 控制台将依次输出：流式下载 -> 流式上传 -> 断点续传
```

**代码结构**:
- `FileTransferDemo` - 顺序执行三个步骤，输出进度/结果
- `main.cpp` - 通过 `QTimer` 启动 demo 并在结束时退出

---

### 3. BatchRequestDemo - 批量请求管理器

**位置**: `examples/BatchRequestDemo/`
**功能**: 演示如何批量管理 HTTP 请求，适用于需要同时发起大量 API 调用的场景

**核心特性**:
- ✅ 批量 HTTP 请求（GET/POST/HEAD）
- ✅ 请求优先级控制（高/中/低）
- ✅ 并发数限制（默认 5 个，可调整 1-20）
- ✅ 自动重试机制（默认 3 次，可调整 0-10）
- ✅ 进度统计和回调
- ✅ 请求队列管理（按优先级排序）

**使用示例**:
```bash
./build/examples/BatchRequestDemo/BatchRequestDemo

# 选择操作：
# 1. 添加请求（可指定方法、优先级）
# 2. 查看所有请求
# 3. 开始执行
# a. 快速测试（添加10个示例请求）
```

**代码结构**:
- `BatchRequest` - 批量请求管理器
- `main.cpp` - 交互式演示

**适用场景**:
- 批量数据抓取
- 多个 API 端点并发调用
- 大规模数据同步

---

### 4. ApiClientDemo - RESTful API 客户端

**位置**: `examples/ApiClientDemo/`
**功能**: 演示如何封装一个简洁易用的 RESTful API 客户端

**核心特性**:
- ✅ 简洁的 REST API 接口（GET/POST/PUT/DELETE）
- ✅ 自动 JSON 序列化/反序列化
- ✅ 请求头管理（Authorization、Content-Type 等）
- ✅ Bearer Token 支持
- ✅ 基础 URL 配置
- ✅ 超时配置（默认 30 秒）
- ✅ 回调式错误处理

**使用示例**:
```bash
./build/examples/ApiClientDemo/ApiClientDemo

# 示例 API: https://jsonplaceholder.typicode.com
# 选择操作：
# 1. GET /posts - 获取所有文章
# 2. GET /posts/1 - 获取单个文章
# 3. POST /posts - 创建新文章
# 4. PUT /posts/1 - 更新文章
# 9. 设置 Bearer Token
```

**代码结构**:
- `ApiClient` - RESTful API 客户端封装
- `main.cpp` - 交互式演示

**代码示例**:
```cpp
ApiClient client("https://api.example.com");
client.setBearerToken("your_token_here");

client.get("users/123",
    // Success callback
    [](const QJsonDocument &response) {
        QJsonObject user = response.object();
        qDebug() << "User:" << user["name"].toString();
    },
    // Error callback
    [](int code, const QString &error) {
        qDebug() << "Error:" << code << error;
    }
);
```

---

## 🔧 编译和运行

### 编译所有示例

```bash
cd build
cmake ..
cmake --build . -j4
```

### 单独编译某个示例

```bash
# 编译 FileDownloadDemo
cmake --build . --target FileDownloadDemo -j4

# 编译 BatchRequestDemo
cmake --build . --target BatchRequestDemo -j4

# 编译 ApiClientDemo
cmake --build . --target ApiClientDemo -j4
```

### 运行示例

```bash
# FileDownloadDemo
./build/examples/FileDownloadDemo/FileDownloadDemo

# BatchRequestDemo
./build/examples/BatchRequestDemo/BatchRequestDemo

# ApiClientDemo
./build/examples/ApiClientDemo/ApiClientDemo
```

---

## 📚 更多示例

除了上述 4 个实用示例外，项目还包含以下示例：

- **QCurl** - 基础 GUI 示例（Qt Widgets）
- **SchedulerDemo** - 请求优先级调度器演示
- **StressTest** - 调度器压力测试
- **Http2Demo** - HTTP/2 示例（需要 libcurl 支持）
- **ProxyDemo** - 代理配置示例
- **WebSocketDemo** - WebSocket 基础示例（需要 libcurl 8.0+）
- **WebSocketPoolDemo** - WebSocket 连接池示例

---

## ⚙️ 技术栈

- **Qt6** (QtCore)
- **libcurl** 8.16.0+
- **C++17**
- **CMake** 3.16+

---

## 📖 学习路径

**推荐学习顺序**:

1. **FileDownloadDemo** - 了解基本的 GET 请求和数据写入
2. **BatchRequestDemo** - 学习批量请求和并发控制
3. **ApiClientDemo** - 掌握 RESTful API 客户端封装

**进阶学习**:
- 查看 `WebSocketDemo` 学习 WebSocket 实时通信
- 查看 `SchedulerDemo` 学习请求优先级调度
- 查看 `Http2Demo` 学习 HTTP/2 多路复用

---

## 🤝 贡献

欢迎提交更多实用示例！请确保：

- 代码遵循项目编码规范（参见 `Qt6_CPP17_Coding_Style.md`）
- 包含完整的 CMakeLists.txt
- 提供清晰的注释和文档
- 演示真实的使用场景

---

## 📄 许可

与 QCurl 主项目相同的许可证。

---

**版本**: v2.8.0
**更新日期**: 2025-11-06
