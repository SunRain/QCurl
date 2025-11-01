# LoggingDemo - Logger 系统演示

## 功能说明

演示 QCurl v2.15.0 的 Logger 系统功能:

1. **默认 Logger** - 使用内置的控制台日志
2. **文件日志** - 将日志写入文件
3. **自定义 Logger** - 实现自己的日志处理逻辑
4. **日志格式** - 自定义日志输出格式

## 编译和运行

```bash
# 编译
cd build
cmake --build . --target LoggingDemo

# 运行
./examples/LoggingDemo/LoggingDemo
```

## 输出示例

```
=== QCurl Logger 系统演示 ===

>>> 示例 1: 使用默认 Logger
Logger 已设置，最小日志级别: 1

>>> 示例 2: 启用文件日志
文件日志已启用: /tmp/qcurl-demo.log

>>> 示例 3: 自定义 Logger (统计)
[INFO] Request: GET http://example.com
[INFO] Response: Status: 200 OK
[INFO] Request: POST http://api.example.com/users
[ERROR] Response: Status: 404 Not Found
[INFO] Request: GET http://api.example.com/data
[INFO] Response: Status: 200 OK

=== 统计信息 ===
总请求数: 6
错误数: 1
成功率: 83.33 %

>>> 示例 4: 自定义日志格式
已设置自定义日志格式

=== 演示完成 ===
```

## API 参考

### QCNetworkDefaultLogger

```cpp
// 创建默认 Logger
auto *logger = new QCNetworkDefaultLogger();

// 设置日志级别
logger->setMinLogLevel(NetworkLogLevel::Info);

// 启用控制台输出
logger->enableConsoleOutput(true);

// 启用文件输出
logger->enableFileOutput("/tmp/qcurl.log", 1024 * 1024, 3);

// 自定义格式
logger->setLogFormat("[%{time}] %{level} - %{message}");

// 设置到 Manager
manager->setLogger(logger);
```

### 自定义 Logger

```cpp
class MyLogger : public QCNetworkLogger
{
public:
    void log(NetworkLogLevel level, const QString &category,
             const QString &message) override {
        // 实现自己的日志逻辑
    }

    void setMinLogLevel(NetworkLogLevel level) override {
        m_minLevel = level;
    }

    NetworkLogLevel minLogLevel() const override {
        return m_minLevel;
    }

private:
    NetworkLogLevel m_minLevel = NetworkLogLevel::Info;
};
```

## 相关文档

- [QCNetworkLogger API 文档](../../docs/API-Guide.md#logger)
- [v2.15.0 发布说明](../../CHANGELOG.md#v2150)
