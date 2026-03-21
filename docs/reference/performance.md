# 性能回归检测参考

本文只保留性能回归检测的稳定入口、输入输出和阈值合同；CI 的具体历史、单次评论样例和时点化结果不在这里维护。

## 1. 目标

性能回归检测回答的是“本次改动相对基线是否退化”，而不是“QCurl 的绝对性能是多少”。

## 2. 统一入口

优先使用仓库脚本，而不是临时拼接命令：

```bash
./scripts/run_benchmark_regression.sh
```

常见参数：

```bash
# 与默认基线比较
./scripts/run_benchmark_regression.sh

# 指定对比分支
./scripts/run_benchmark_regression.sh develop

# 指定迭代次数
./scripts/run_benchmark_regression.sh master 500

# 指定回归阈值（百分比）
./scripts/run_benchmark_regression.sh master 100 15
```

## 3. 基本流程

1. 构建 benchmark 可执行文件。
2. 生成 baseline 与 current 两组结果。
3. 解析 Qt benchmark 输出。
4. 比较差异并按阈值判断是否回归。

相关脚本：

- `scripts/run_benchmark_regression.sh`
- `scripts/parse_benchmark_results.py`
- `scripts/compare_benchmarks.py`

## 4. 输入与输出合同

### 4.1 输入

- `benchmark_*` 可执行文件的输出
- 基线分支或基线结果文件
- 可选阈值参数

### 4.2 输出

- 结构化汇总结果
- 差异报告
- 非零退出码表示检测到超过阈值的回归

调用方如果把该流程接入 CI，应以退出码为准，而不是解析文档里的示例文本。

## 5. 阈值解释

- 阈值越低，越容易把噪声误判为回归。
- 阈值越高，越可能放过真实退化。
- 网络相关 benchmark 的波动通常高于纯 CPU 或纯内存型 benchmark。

建议做法：

- 日常回归：先用较稳妥阈值筛出明显退化。
- 发布前复验：增加迭代次数，再用更严格阈值复查。
- 任何需要写进结论的数字，都应来自同一套门禁环境。

## 6. 故障排查

### 6.1 benchmark 超时或无结果

先检查：

- benchmark 是否已构建
- 本地依赖服务是否已启动
- 测试环境是否允许端口绑定或本地网络回环

构建与服务准备入口见：

- `docs/dev/build-and-test.md`

### 6.2 结果波动过大

优先处理：

1. 提高迭代次数
2. 减少机器负载干扰
3. 固定比较环境

### 6.3 CI 与本地结论不一致

优先相信统一门禁环境的结果，再检查本地是否使用了相同脚本、相同依赖和相同阈值。

## 7. 非目标

本文不维护：

- 某次 PR 的评论截图
- 某个 commit 的 benchmark 数字
- 某台机器的硬件/内核快照

这些内容应留在 CI 工件或单次报告中，而不是长期 reference 文档。
