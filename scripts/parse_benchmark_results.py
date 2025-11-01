#!/usr/bin/env python3
"""
parse_benchmark_results.py - 解析 Qt Test QBENCHMARK 输出

将 Qt Test 基准测试输出解析为 JSON 格式，用于性能比较。

使用方法:
    python3 parse_benchmark_results.py <results_dir> --output <output.json>

输出格式:
    {
        "benchmark_http2": {
            "benchmarkHttp1Request": {"value": 123.45, "unit": "msecs"},
            "benchmarkHttp2Request": {"value": 98.76, "unit": "msecs"}
        },
        ...
    }
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Dict, Any


def parse_qt_benchmark_output(content: str) -> Dict[str, Dict[str, Any]]:
    """
    解析 Qt Test QBENCHMARK 输出

    示例输入:
    RESULT : BenchmarkHttp2::benchmarkHttp1Request():
         123.45 msecs per iteration (total: 12345, iterations: 100)

    Args:
        content: Qt Test 输出内容

    Returns:
        解析后的字典 {test_name: {value: float, unit: str}}
    """
    results = {}

    # 匹配 RESULT 行
    # 格式: RESULT : ClassName::methodName():
    #       value unit per iteration (total: xxx, iterations: xxx)
    pattern = re.compile(
        r'RESULT\s*:\s*(\w+)::(\w+)\(\):\s*\n\s*'
        r'([\d.]+)\s+(\w+)\s+per iteration\s+\(total:\s*[\d.]+,\s*iterations:\s*\d+\)',
        re.MULTILINE
    )

    for match in pattern.finditer(content):
        class_name = match.group(1)
        method_name = match.group(2)
        value = float(match.group(3))
        unit = match.group(4)

        results[method_name] = {
            "value": value,
            "unit": unit,
            "class": class_name
        }

    return results


def parse_benchmark_results_dir(results_dir: Path) -> Dict[str, Dict[str, Any]]:
    """
    解析目录中所有基准测试结果

    Args:
        results_dir: 结果目录路径

    Returns:
        {benchmark_name: {test_name: {value, unit}}}
    """
    all_results = {}

    for result_file in results_dir.glob("*.txt"):
        benchmark_name = result_file.stem

        try:
            content = result_file.read_text(encoding='utf-8', errors='ignore')
            parsed = parse_qt_benchmark_output(content)

            if parsed:
                all_results[benchmark_name] = parsed
                print(f"✓ 解析 {benchmark_name}: {len(parsed)} 个结果")
            else:
                print(f"⚠ {benchmark_name}: 未找到 QBENCHMARK 结果")

        except Exception as e:
            print(f"✗ 解析 {benchmark_name} 失败: {e}")

    return all_results


def add_metadata(results: Dict[str, Any]) -> Dict[str, Any]:
    """添加元数据"""
    import datetime

    return {
        "metadata": {
            "timestamp": datetime.datetime.now(datetime.UTC).isoformat(),
            "version": os.environ.get("GITHUB_SHA", "unknown")[:7],
            "ref": os.environ.get("GITHUB_REF", "unknown"),
            "runner": os.environ.get("RUNNER_OS", "unknown")
        },
        "benchmarks": results
    }


def main():
    parser = argparse.ArgumentParser(
        description="解析 Qt Test QBENCHMARK 输出"
    )
    parser.add_argument(
        "results_dir",
        type=Path,
        help="包含基准测试输出文件的目录"
    )
    parser.add_argument(
        "--output", "-o",
        type=Path,
        default=Path("summary.json"),
        help="输出 JSON 文件路径"
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="详细输出"
    )

    args = parser.parse_args()

    if not args.results_dir.exists():
        print(f"错误: 目录不存在: {args.results_dir}")
        sys.exit(1)

    print(f"📂 解析目录: {args.results_dir}")

    results = parse_benchmark_results_dir(args.results_dir)

    if not results:
        print("⚠ 未找到任何基准测试结果")
        # 创建空结果文件
        results = {}

    # 添加元数据
    output_data = add_metadata(results)

    # 写入 JSON
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, 'w', encoding='utf-8') as f:
        json.dump(output_data, f, indent=2, ensure_ascii=False)

    print(f"✓ 结果已保存到: {args.output}")
    print(f"  共 {len(results)} 个基准测试, "
          f"{sum(len(v) for v in results.values())} 个测试用例")

    if args.verbose:
        print("\n详细结果:")
        print(json.dumps(output_data, indent=2, ensure_ascii=False))

    return 0


if __name__ == "__main__":
    sys.exit(main())
