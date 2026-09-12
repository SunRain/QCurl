#!/usr/bin/env python3
"""校验 UCE 必需上传工件，输出与 gate 结论分离的归档检查结果。"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from scripts.uce_gate.archive_validation import validate_archive


def main(argv: list[str] | None = None) -> int:
    """校验指定 run 的上传工件，失败时返回非零并保留结构化诊断。"""

    parser = argparse.ArgumentParser(description="校验 UCE 归档的完整性和身份。")
    parser.add_argument("--evidence-root", required=True, type=Path, help="UCE 证据根目录")
    parser.add_argument("--run-id", required=True, help="待核对的运行标识")
    parser.add_argument("--require-pass", action="store_true", help="同时要求原始 gate 结论通过")
    args = parser.parse_args(argv)
    report = validate_archive(args.evidence_root, args.run_id, require_pass=args.require_pass)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report["accepted"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
