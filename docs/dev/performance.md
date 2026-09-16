# 性能基准与回归

本页面向维护者，用同环境前后对比判断实现路径、连接复用、调度开销和协议选择的变化趋势；不发布固定延迟/吞吐承诺，也不把 benchmark 当成功能正确性的证据。命令默认在仓库根目录执行。

## 1. 准备与单个基准

准备[构建依赖](build-and-test.md)和基准所需本地服务；网络基准尤其需要固定服务端、协议能力与负载。源码入口在 [benchmarks](../../benchmarks/README.md)，包括 HTTP/2、HTTP/3、connection pool、scheduler 和有 WebSocket 能力时的对应程序。

```bash
cmake -S . -B build-performance -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=ON \
  -DQCURL_BUILD_SHARED_LIBS=ON
cmake --build build-performance --parallel
./build-performance/benchmarks/benchmark_http2 -iterations 100
```

HTTP/2 target 仅在相应能力启用时生成；缺 target/依赖、超时、QtTest 失败或跳过都不是有效跑分。需要具体函数时先用该 QtTest 程序的 `-functions` 查看，再显式选择；不要用“没有结果”当作零退化。

## 2. 已有回归脚本与副作用

仓库入口为 `scripts/run_benchmark_regression.sh [baseline_branch] [iterations] [threshold_percent]`，默认 master、100 次、10%。只在独立、干净且可切换分支的工作副本中使用：

```bash
./scripts/run_benchmark_regression.sh master 100 10
```

它会使用该副本的 build/，采集 current/baseline 输出到 benchmark-results/，调用 parser 生成 summary.json，再生成 comparison 报告。**它会 stash 已跟踪改动并 checkout 基线分支，不是只读比较命令**；stash 不包含全部未跟踪文件，异常退出也没有完整恢复保证。不要在携带 WIP、发布证据或并行任务的工作区运行。

现有脚本对单个 benchmark 超时/失败只发警告，parser 也可能跳过无结果文件；最终比较退出 0 仅表示已解析的可比较项未超过阈值，不能证明全部基准执行。当前 docs-only 整合不修改脚本，也不将它提升为 fail-closed 发布门禁。

## 3. 结果解析与比较

需要保留可复验结论时，先确认两侧每个目标真实执行成功、结果集合非空且匹配，再使用现有 parser/comparator；可用分别准备好的 clean baseline/current 输出，不要求为比较切换当前工作区。

```bash
python3 scripts/parse_benchmark_results.py benchmark-results/baseline \
  --output benchmark-results/baseline/summary.json
python3 scripts/parse_benchmark_results.py benchmark-results/current \
  --output benchmark-results/current/summary.json
python3 scripts/compare_benchmarks.py \
  benchmark-results/baseline/summary.json benchmark-results/current/summary.json \
  --threshold 10 --output benchmark-results/comparison.md --fail-on-regression
```

上面的 baseline/current 目录须事先装入各自 Qt QBENCHMARK 原始 txt 输出，不是脚本自动生成目录名的别名。保留命令及退出码；comparison 的非零结果可能是退化，也可能是输入/执行失败，应看原始错误而不是统一解释为性能下降。

检查新增/移除项、单位及可比较项数量，不让空集合或缺失项目变成“未回归”。默认按耗时增长计算退化；不能把不同单位、硬件或不同协议的结果直接比较。

## 4. 阈值与结论

- 阈值越低越敏感，也越容易把噪声当退化；越高越可能漏掉真实退化。
- 网络基准的波动一般高于纯 CPU/内存基准。固定环境、减少后台负载并增加迭代，不用一次公网胜负证明协议优劣。
- 日常先筛明显退化，再在同环境复验；提升迭代次数或收紧阈值不能补救缺失原始结果。
- CI 与本地不一致时先对齐依赖、脚本、服务端、负载和阈值；不是简单选择更好看的数字。

每份持久结论至少写明比较对象/commit、机器与依赖版本、网络与服务端、命令/迭代次数、样本数和阈值、是否全部成功执行，以及结论是趋势还是交付门槛。未绑定这些上下文的百分比不进入当前发布声明。

## 5. 常见失败与非目标

超时或无结果先检查可执行文件、协议能力、端口/本地服务与原始 QtTest 输出；排除构建和环境失败后再谈性能。波动大时固定比较环境、多次采样并检查机器负载。

长期正文不维护某次 PR 截图、单个 commit 分数或某台机器的快照；这些留在该次原始结果/CI 工件。本页不代替协议、生命周期、安装消费或完整发布测试。
