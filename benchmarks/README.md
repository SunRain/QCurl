# QCurl 性能基准测试

**文档性质**：维护者参考资料，非公开发布声明

## 构建

```bash
cmake -S . -B build -DBUILD_BENCHMARKS=ON
cmake --build build --parallel
```

## 运行

```bash
./build/benchmarks/benchmark_scheduler
./build/benchmarks/benchmark_connectionpool
./build/benchmarks/benchmark_http2
./build/benchmarks/benchmark_http3
```

## 基准测试列表

- `benchmark_scheduler` — 调度器与请求优先级压力测试
- `benchmark_connectionpool` — 连接池性能测试
- `benchmark_http2` — HTTP/2 多路复用性能测试
- `benchmark_http3` — HTTP/3 QUIC 性能测试
- `benchmark_websocket_pool` — WebSocket 连接池性能测试（需要 WebSocket 支持）
- `benchmark_websocket_eventdriven` — WebSocket 事件驱动性能测试（需要 WebSocket 支持）

## TestSupport 依赖说明

⚠️ **benchmark_scheduler 使用 TestSupport**

`benchmark_scheduler` 使用 `QCNetworkMockHandler` 与 `TestSupport::setMockHandler()` 构造确定性调度场景，
因此链接了 `QCurlTestSupport`。这是用于基准测试的合理依赖，确保可重复的测试条件。

**TestSupport API 不是常规生产用法**：
- Mock API 仅用于测试与基准测试环境
- 生产环境应使用真实网络请求
- 不应将 `QCurlTestSupport` 链接到最终发布的应用程序中

## 文档规则

基准测试文档与 `docs/reference/benchmarks.md` 和 `docs/reference/performance.md` 对齐。
不包含固定吞吐量、延迟或通过率数值，除非绑定到日期报告和环境说明。
不重新引入旧的 pre-1.0、RC 或基于日期的发布叙述。
