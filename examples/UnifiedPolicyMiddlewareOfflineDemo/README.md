# UnifiedPolicyMiddlewareOfflineDemo

**Surface label**: Other Extras / Preview + Test Support / explicit opt-in

## 功能说明

演示统一策略 Middleware 与 Logger 的离线集成模式：

- 使用 `QCNetworkMockHandler` 与 `TestSupport::setMockHandler()` 构造纯离线场景
- 展示 `QCRedactingLoggingMiddleware`（脱敏日志）
- 展示 `QCObservabilityMiddleware`（结构化观测事件）
- 展示自定义重试策略 Middleware

## 构建要求

该示例依赖 **Test Support**（explicit opt-in），必须显式链接 `QCurlTestSupport`。

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --parallel
```

## 运行

```bash
./build/examples/UnifiedPolicyMiddlewareOfflineDemo/UnifiedPolicyMiddlewareOfflineDemo
```

## 依赖性质说明

⚠️ **TestSupport 依赖声明**

本示例使用 `QCNetworkMockHandler` 与 `TestSupport::setMockHandler()` 构造离线 mock 场景，
因此链接了 `QCurlTestSupport`。这是用于演示目的的合理依赖，但 **TestSupport API 不是常规生产用法**。

在生产代码中：
- Mock API 仅用于测试环境
- 生产环境应使用真实网络请求或其他隔离方案
- 不应将 `QCurlTestSupport` 链接到最终发布的应用程序中

## 文档规则

本示例文档说明当前 `QCurl 1.0.0 first stable` 的 Middleware Extras（Preview）用法，
不包含旧版本历史、过时特性或一次性发布叙述。
