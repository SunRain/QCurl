# QCurl 基准测试说明

本文说明 `benchmarks/` 目录回答什么问题、如何解读结果，以及哪些结论不能从单次跑分中推出。

## 1. 基准测试的定位

`benchmarks/` 用于比较实现路径、连接复用、调度器开销和协议选择的相对变化趋势，不用于发布固定性能承诺。

当前仓库中的基准程序包括：

- `benchmark_http2.cpp`
- `benchmark_http3.cpp`
- `benchmark_connectionpool.cpp`
- `benchmark_scheduler.cpp`
- `benchmark_websocket_*.cpp`

## 2. 正确解读方式

- 基准结果的首要用途是“同一台机器、同一套依赖、同一份脚本”下的前后对比。
- 不同机器、不同网络、不同 libcurl/Qt 构建的结果不可直接横向比较。
- 单次结果只能作为线索，不能直接写成“HTTP/2 永远快多少”“连接池一定提升多少”这类结论。
- 网络型 benchmark 必须与测试环境一起记录；否则数字只有局部意义。

## 3. 推荐执行方式

构建基准程序：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
cmake --build build --parallel
```

运行单个基准：

```bash
./build/benchmarks/benchmark_http2 -iterations 100
```

运行回归脚本时，优先使用仓库脚本和统一入口，避免手工命令导致口径漂移：

- `scripts/run_benchmark_regression.sh`
- `docs/reference/performance.md`

## 4. 写结论时必须保留的上下文

任何需要保留的 benchmark 结论，都应至少说明：

1. 比较对象是什么。
2. 运行环境是否稳定且可复现。
3. 数字来自单次样本还是回归门禁。
4. 结论是“趋势判断”还是“交付门槛”。

如果这四点无法同时说明，文档中应保留方法和入口，而不是保留过期数字。

## 5. 常见误用

- 把一次公网 benchmark 写成长期 reference。
- 把 HTTP/2 或 HTTP/3 的单次胜负写成无条件规则。
- 只保留百分比，不保留环境和命令。
- 把 benchmark 当成功能正确性的证据。

## 6. 相关入口

- `docs/reference/performance.md`
- `docs/dev/build-and-test.md`
- `benchmarks/CMakeLists.txt`
