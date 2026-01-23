# 性能回归自动检测指南

> **版本**: 1.0.0
> **创建日期**: 2025-11-23

---

## 概述

QCurl 提供了完整的性能回归自动检测系统，包括：

1. **GitHub Actions 工作流** - 在 PR 和主分支推送时自动运行
2. **本地检测脚本** - 开发者在提交前运行
3. **结果解析工具** - 将 Qt Test 输出转换为结构化数据
4. **比较报告生成** - 生成详细的 Markdown 报告

---

## 快速开始

### 本地运行性能回归检测

```bash
# 与 master 分支比较（默认）
./scripts/run_benchmark_regression.sh

# 与指定分支比较
./scripts/run_benchmark_regression.sh develop

# 指定迭代次数（更精确但更慢）
./scripts/run_benchmark_regression.sh master 500

# 指定回归阈值（百分比）
./scripts/run_benchmark_regression.sh master 100 15
```

### 单独运行基准测试

```bash
# 编译基准测试
cmake -B build -DBUILD_BENCHMARKS=ON
cmake --build build --parallel

# 运行单个基准测试
./build/benchmarks/benchmark_http2 -iterations 100

# 运行所有基准测试
for bench in build/benchmarks/benchmark_*; do
    $bench -iterations 100
done
```

---

## GitHub Actions 集成

### 触发条件

工作流在以下情况下自动运行：

| 触发条件 | 分支 | 说明 |
|---------|------|------|
| `push` | master, main, develop | 主分支推送时更新基线 |
| `pull_request` | master, main, develop | PR 时检测回归并评论 |
| `workflow_dispatch` | 任意 | 手动触发，可指定迭代次数 |

### 文件变更过滤

只有以下路径变更时才会触发：

- `src/**` - 源代码
- `benchmarks/**` - 基准测试代码
- `.github/workflows/benchmark.yml` - 工作流配置

### PR 评论

当 PR 触发工作流时，会自动在 PR 中添加性能比较报告评论：

```markdown
## 📊 性能基准测试结果

### 📈 性能比较概览
| 项目 | 基线 | 当前 |
|------|------|------|
| 版本 | `abc1234` | `def5678` |

### 📊 统计摘要
- ✅ 改进: **2** 个测试
- ❌ 回归: **0** 个测试
- ➖ 无变化: **15** 个测试
```

### 回归检测

如果检测到超过阈值（默认 10%）的性能回归：

1. 工作流将**失败**
2. PR 将被标记为检查失败
3. 评论中会列出具体的回归测试

---

## 工具详解

### parse_benchmark_results.py

将 Qt Test QBENCHMARK 输出解析为 JSON：

```bash
python3 scripts/parse_benchmark_results.py \
    benchmark-results/ \
    --output summary.json \
    --verbose
```

**输入格式**（Qt Test 输出）:

```
RESULT : BenchmarkHttp2::benchmarkHttp1Request():
     123.45 msecs per iteration (total: 12345, iterations: 100)
```

**输出格式**（JSON）:

```json
{
  "metadata": {
    "timestamp": "2025-11-23T12:00:00Z",
    "version": "abc1234"
  },
  "benchmarks": {
    "benchmark_http2": {
      "benchmarkHttp1Request": {
        "value": 123.45,
        "unit": "msecs",
        "class": "BenchmarkHttp2"
      }
    }
  }
}
```

### compare_benchmarks.py

比较两次基准测试结果：

```bash
python3 scripts/compare_benchmarks.py \
    baseline.json \
    current.json \
    --threshold 10 \
    --output comparison.md
```

**参数说明**:

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--threshold` | 回归检测阈值（百分比） | 10 |
| `--output` | 输出报告路径 | stdout |
| `--fail-on-regression` | 检测到回归时返回非零退出码 | true |

**退出码**:

| 退出码 | 含义 |
|--------|------|
| 0 | 无回归 |
| 1 | 检测到回归 |

---

## 配置说明

### 阈值调整

可以通过以下方式调整回归检测阈值：

1. **GitHub Actions**: 修改 `.github/workflows/benchmark.yml` 中的 `--threshold` 参数
2. **本地脚本**: 传递第三个参数 `./scripts/run_benchmark_regression.sh master 100 15`

**建议阈值**:

| 场景 | 阈值 | 说明 |
|------|------|------|
| 日常开发 | 10% | 平衡灵敏度和误报 |
| 发布前 | 5% | 更严格的检测 |
| 实验性功能 | 20% | 允许更大波动 |

### 迭代次数

迭代次数影响结果的精确度：

| 迭代次数 | 精确度 | 运行时间 |
|---------|--------|---------|
| 100 | 中等 | ~5 分钟 |
| 500 | 较高 | ~15 分钟 |
| 1000 | 高 | ~30 分钟 |

**建议**:
- CI/CD: 100-200 次（快速反馈）
- 发布前: 500-1000 次（精确结果）

---

## 最佳实践

### 1. 提交前运行

在提交性能敏感的代码前，先运行本地检测：

```bash
./scripts/run_benchmark_regression.sh
```

### 2. 稳定的测试环境

为获得一致的结果：

```bash
# 关闭其他程序
killall chrome firefox

# 固定 CPU 频率（Linux）
sudo cpupower frequency-set -g performance

# 多次运行取平均
for i in {1..3}; do
    ./scripts/run_benchmark_regression.sh
done
```

### 3. 关注趋势

单次回归可能是噪声，关注多次测试的趋势：

- 查看 GitHub Actions 历史记录
- 比较多个提交的结果
- 使用较高的迭代次数确认

### 4. 合理设置阈值

根据测试的稳定性设置阈值：

- **网络测试**: 15-20%（网络延迟波动大）
- **CPU 密集型**: 5-10%（相对稳定）
- **内存测试**: 10-15%（受系统影响）

---

## 故障排查

### 问题：基准测试超时

**原因**: 测试服务器未启动或网络问题

**解决**: 启动本地 httpbin 与健康检查请参考：[`docs/dev/build-and-test.md`](../dev/build-and-test.md)。

### 问题：结果波动大

**原因**: 系统负载不稳定或迭代次数太少

**解决**:
```bash
# 增加迭代次数
./scripts/run_benchmark_regression.sh master 500

# 检查系统负载
htop
```

### 问题：GitHub Actions 失败

**原因**: 依赖安装失败或测试服务不可用

**解决**:
1. 检查 Actions 日志
2. 确认 `httpbin` 服务启动成功
3. 检查 Qt6 和 libcurl 安装

---

## 文件清单

```
.github/
└── workflows/
    └── benchmark.yml           # GitHub Actions 工作流

scripts/
├── parse_benchmark_results.py  # 解析 Qt Test 输出
├── compare_benchmarks.py       # 比较基准测试结果
└── run_benchmark_regression.sh # 本地运行脚本

docs/
└── PERFORMANCE_REGRESSION_GUIDE.md  # 本文档
```

---

## 参考

- [Qt Test QBENCHMARK](https://doc.qt.io/qt-6/qtest.html#QBENCHMARK)
- [GitHub Actions 文档](https://docs.github.com/en/actions)
- [性能测试最佳实践](https://www.brendangregg.com/methodology.html)

---

**文档维护者**: AI (Claude)
**最后更新**: 2025-11-23
