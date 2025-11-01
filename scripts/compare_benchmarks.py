#!/usr/bin/env python3
"""
compare_benchmarks.py - 性能回归检测

比较两次基准测试结果，检测性能回归并生成报告。

使用方法:
    python3 compare_benchmarks.py <baseline.json> <current.json> --threshold 10 --output report.md

输出:
    - Markdown 格式的比较报告
    - 超过阈值的回归将被标记
    - 如果检测到回归，退出码为 1
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Dict, Any, Tuple, List


class BenchmarkComparison:
    """基准测试比较器"""

    def __init__(self, threshold_percent: float = 10.0):
        """
        Args:
            threshold_percent: 回归检测阈值（百分比）
        """
        self.threshold_percent = threshold_percent
        self.improvements = []
        self.regressions = []
        self.unchanged = []
        self.new_tests = []
        self.removed_tests = []

    def compare(
        self,
        baseline: Dict[str, Any],
        current: Dict[str, Any]
    ) -> Tuple[bool, str]:
        """
        比较基准测试结果

        Args:
            baseline: 基线结果 JSON
            current: 当前结果 JSON

        Returns:
            (has_regression, report_markdown)
        """
        baseline_benchmarks = baseline.get("benchmarks", {})
        current_benchmarks = current.get("benchmarks", {})

        # 获取所有测试名称
        all_tests = set()
        for bench_name, tests in baseline_benchmarks.items():
            for test_name in tests:
                all_tests.add((bench_name, test_name))

        for bench_name, tests in current_benchmarks.items():
            for test_name in tests:
                all_tests.add((bench_name, test_name))

        # 逐个比较
        for bench_name, test_name in sorted(all_tests):
            baseline_val = self._get_value(baseline_benchmarks, bench_name, test_name)
            current_val = self._get_value(current_benchmarks, bench_name, test_name)

            if baseline_val is None and current_val is not None:
                self.new_tests.append((bench_name, test_name, current_val))
            elif baseline_val is not None and current_val is None:
                self.removed_tests.append((bench_name, test_name, baseline_val))
            elif baseline_val is not None and current_val is not None:
                change_percent = self._calc_change(baseline_val["value"], current_val["value"])

                comparison = {
                    "bench": bench_name,
                    "test": test_name,
                    "baseline": baseline_val["value"],
                    "current": current_val["value"],
                    "unit": current_val.get("unit", "msecs"),
                    "change": change_percent
                }

                # 注意：对于时间指标，增加是回归，减少是改进
                if change_percent > self.threshold_percent:
                    self.regressions.append(comparison)
                elif change_percent < -self.threshold_percent:
                    self.improvements.append(comparison)
                else:
                    self.unchanged.append(comparison)

        # 生成报告
        report = self._generate_report(baseline, current)
        has_regression = len(self.regressions) > 0

        return has_regression, report

    def _get_value(
        self,
        benchmarks: Dict,
        bench_name: str,
        test_name: str
    ) -> Dict[str, Any] | None:
        """获取指定测试的值"""
        if bench_name not in benchmarks:
            return None
        if test_name not in benchmarks[bench_name]:
            return None
        return benchmarks[bench_name][test_name]

    def _calc_change(self, baseline: float, current: float) -> float:
        """计算变化百分比"""
        if baseline == 0:
            return 0.0
        return ((current - baseline) / baseline) * 100

    def _generate_report(
        self,
        baseline: Dict[str, Any],
        current: Dict[str, Any]
    ) -> str:
        """生成 Markdown 报告"""
        lines = []

        # 元数据
        baseline_meta = baseline.get("metadata", {})
        current_meta = current.get("metadata", {})

        lines.append("### 📈 性能比较概览\n")
        lines.append(f"| 项目 | 基线 | 当前 |")
        lines.append(f"|------|------|------|")
        lines.append(f"| 版本 | `{baseline_meta.get('version', 'N/A')}` | `{current_meta.get('version', 'N/A')}` |")
        lines.append(f"| 时间 | {baseline_meta.get('timestamp', 'N/A')[:19]} | {current_meta.get('timestamp', 'N/A')[:19]} |")
        lines.append("")

        # 摘要统计
        total = len(self.improvements) + len(self.regressions) + len(self.unchanged)
        lines.append("### 📊 统计摘要\n")
        lines.append(f"- ✅ 改进: **{len(self.improvements)}** 个测试")
        lines.append(f"- ❌ 回归: **{len(self.regressions)}** 个测试")
        lines.append(f"- ➖ 无变化: **{len(self.unchanged)}** 个测试")
        lines.append(f"- 🆕 新增: **{len(self.new_tests)}** 个测试")
        lines.append(f"- 🗑️ 移除: **{len(self.removed_tests)}** 个测试")
        lines.append(f"- 📏 阈值: ±{self.threshold_percent}%")
        lines.append("")

        # 回归警告
        if self.regressions:
            lines.append("### ⚠️ REGRESSION DETECTED - 性能回归\n")
            lines.append("以下测试的性能显著下降：\n")
            lines.append("| 基准测试 | 测试用例 | 基线 | 当前 | 变化 |")
            lines.append("|---------|---------|------|------|------|")
            for r in sorted(self.regressions, key=lambda x: x["change"], reverse=True):
                change_str = f"+{r['change']:.1f}%" if r['change'] > 0 else f"{r['change']:.1f}%"
                lines.append(
                    f"| {r['bench']} | {r['test']} | "
                    f"{r['baseline']:.2f} {r['unit']} | "
                    f"{r['current']:.2f} {r['unit']} | "
                    f"🔴 {change_str} |"
                )
            lines.append("")

        # 改进
        if self.improvements:
            lines.append("### ✅ 性能改进\n")
            lines.append("以下测试的性能有显著提升：\n")
            lines.append("| 基准测试 | 测试用例 | 基线 | 当前 | 变化 |")
            lines.append("|---------|---------|------|------|------|")
            for r in sorted(self.improvements, key=lambda x: x["change"]):
                change_str = f"{r['change']:.1f}%"
                lines.append(
                    f"| {r['bench']} | {r['test']} | "
                    f"{r['baseline']:.2f} {r['unit']} | "
                    f"{r['current']:.2f} {r['unit']} | "
                    f"🟢 {change_str} |"
                )
            lines.append("")

        # 无变化（折叠）
        if self.unchanged:
            lines.append("<details>")
            lines.append("<summary>➖ 无显著变化的测试（点击展开）</summary>\n")
            lines.append("| 基准测试 | 测试用例 | 基线 | 当前 | 变化 |")
            lines.append("|---------|---------|------|------|------|")
            for r in self.unchanged:
                change_str = f"+{r['change']:.1f}%" if r['change'] > 0 else f"{r['change']:.1f}%"
                lines.append(
                    f"| {r['bench']} | {r['test']} | "
                    f"{r['baseline']:.2f} {r['unit']} | "
                    f"{r['current']:.2f} {r['unit']} | "
                    f"{change_str} |"
                )
            lines.append("\n</details>\n")

        # 新增测试
        if self.new_tests:
            lines.append("### 🆕 新增测试\n")
            lines.append("| 基准测试 | 测试用例 | 值 |")
            lines.append("|---------|---------|-----|")
            for bench, test, val in self.new_tests:
                lines.append(f"| {bench} | {test} | {val['value']:.2f} {val.get('unit', 'msecs')} |")
            lines.append("")

        # 移除测试
        if self.removed_tests:
            lines.append("### 🗑️ 移除测试\n")
            lines.append("| 基准测试 | 测试用例 | 之前的值 |")
            lines.append("|---------|---------|---------|")
            for bench, test, val in self.removed_tests:
                lines.append(f"| {bench} | {test} | {val['value']:.2f} {val.get('unit', 'msecs')} |")
            lines.append("")

        return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="比较基准测试结果，检测性能回归"
    )
    parser.add_argument(
        "baseline",
        type=Path,
        help="基线结果 JSON 文件"
    )
    parser.add_argument(
        "current",
        type=Path,
        help="当前结果 JSON 文件"
    )
    parser.add_argument(
        "--threshold", "-t",
        type=float,
        default=10.0,
        help="回归检测阈值（百分比，默认 10）"
    )
    parser.add_argument(
        "--output", "-o",
        type=Path,
        default=None,
        help="输出 Markdown 报告路径"
    )
    parser.add_argument(
        "--fail-on-regression",
        action="store_true",
        default=True,
        help="检测到回归时返回非零退出码"
    )

    args = parser.parse_args()

    # 读取文件
    if not args.baseline.exists():
        print(f"错误: 基线文件不存在: {args.baseline}")
        sys.exit(1)

    if not args.current.exists():
        print(f"错误: 当前结果文件不存在: {args.current}")
        sys.exit(1)

    with open(args.baseline, 'r', encoding='utf-8') as f:
        baseline = json.load(f)

    with open(args.current, 'r', encoding='utf-8') as f:
        current = json.load(f)

    # 比较
    comparator = BenchmarkComparison(threshold_percent=args.threshold)
    has_regression, report = comparator.compare(baseline, current)

    # 输出报告
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with open(args.output, 'w', encoding='utf-8') as f:
            f.write(report)
        print(f"✓ 报告已保存到: {args.output}")
    else:
        print(report)

    # 退出码
    if has_regression and args.fail_on_regression:
        print(f"\n❌ 检测到 {len(comparator.regressions)} 个性能回归！")
        sys.exit(1)
    else:
        print(f"\n✓ 未检测到显著性能回归")
        sys.exit(0)


if __name__ == "__main__":
    main()
