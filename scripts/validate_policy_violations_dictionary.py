#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ast
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Set


@dataclass(frozen=True)
class ScanTarget:
    path: Path
    mode: str  # "add_policy_violation" | "policy_violations_list"


def _load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _extract_codes_from_markdown(md_path: Path) -> Set[str]:
    # 仅提取表格中第一列的 `code`（避免误抓正文示例）。
    # 形如：| `gate_offline_failed` | HIGH | ...
    codes: Set[str] = set()
    line_re = re.compile(r"^\|\s*`(?P<code>[a-z0-9_]+)`\s*\|")
    for line in md_path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = line_re.match(line)
        if m:
            code = m.group("code")
            if code:
                codes.add(code)
    return codes


def _scan_codes_from_python_file(path: Path, mode: str) -> Set[str]:
    codes: Set[str] = set()
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))

    class Visitor(ast.NodeVisitor):
        def visit_Call(self, node: ast.Call) -> None:  # noqa: N802
            if mode == "add_policy_violation":
                # add_policy_violation("code")
                if isinstance(node.func, ast.Name) and node.func.id == "add_policy_violation":
                    if node.args and isinstance(node.args[0], ast.Constant) and isinstance(node.args[0].value, str):
                        codes.add(node.args[0].value)

            if mode == "policy_violations_list":
                # policy_violations.append("code") / policy_violations.extend(["a","b"])
                if isinstance(node.func, ast.Attribute) and isinstance(node.func.value, ast.Name):
                    if node.func.value.id == "policy_violations":
                        if node.func.attr == "append":
                            if node.args and isinstance(node.args[0], ast.Constant) and isinstance(node.args[0].value, str):
                                codes.add(node.args[0].value)
                        elif node.func.attr == "extend":
                            if node.args and isinstance(node.args[0], (ast.List, ast.Tuple)):
                                for elt in node.args[0].elts:
                                    if isinstance(elt, ast.Constant) and isinstance(elt.value, str):
                                        codes.add(elt.value)

            self.generic_visit(node)

    Visitor().visit(tree)
    return codes


def _validate_dictionary(doc: dict, *, dictionary_path: Path) -> Set[str]:
    errors = []
    if doc.get("schema_version") != 1:
        errors.append(f"schema_version must be 1: {dictionary_path}")
    entries = doc.get("entries")
    if not isinstance(entries, dict) or not entries:
        errors.append(f"entries must be a non-empty object: {dictionary_path}")
        entries = {}

    enums = doc.get("enums") or {}
    allowed_severity = set((enums.get("evidence_severity") or [])) or {"CRITICAL", "HIGH", "MEDIUM"}
    allowed_quality = set((enums.get("quality_signal") or [])) or {
        "IMPLEMENTATION",
        "INFRA",
        "SECURITY",
        "EVIDENCE",
        "MIXED",
    }

    codes: Set[str] = set()
    code_re = re.compile(r"^[a-z0-9_]+$")
    for code, meta in entries.items():
        if not isinstance(code, str) or not code_re.match(code):
            errors.append(f"invalid code key: {code!r}")
            continue
        if code in codes:
            errors.append(f"duplicate code: {code}")
            continue
        codes.add(code)
        if not isinstance(meta, dict):
            errors.append(f"entry must be object: {code}")
            continue
        sev = meta.get("evidence_severity")
        qs = meta.get("quality_signal")
        meaning = meta.get("meaning")
        if sev not in allowed_severity:
            errors.append(f"{code}: invalid evidence_severity={sev!r} (allowed: {sorted(allowed_severity)})")
        if qs not in allowed_quality:
            errors.append(f"{code}: invalid quality_signal={qs!r} (allowed: {sorted(allowed_quality)})")
        if not isinstance(meaning, str) or not meaning.strip():
            errors.append(f"{code}: missing/empty meaning")

    if errors:
        for e in errors:
            sys.stderr.write(f"[policy_violations_dictionary] error: {e}\n")
        raise SystemExit(2)

    return codes


def _fmt_codes(codes: Iterable[str]) -> str:
    safe = [c for c in codes if isinstance(c, str) and c]
    return ", ".join(sorted(safe))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Validate policy_violations dictionary and forbid unregistered codes.")
    parser.add_argument(
        "--dictionary",
        default=".helloagents/modules/policy_violations_dictionary.json",
        help="Path to machine-readable dictionary JSON (default: .helloagents/modules/policy_violations_dictionary.json).",
    )
    parser.add_argument(
        "--markdown",
        default=".helloagents/modules/policy_violations_dictionary.md",
        help="Path to human-readable dictionary MD (default: .helloagents/modules/policy_violations_dictionary.md).",
    )
    args = parser.parse_args(argv)

    repo_root = Path(__file__).resolve().parent.parent
    dictionary_path = (repo_root / args.dictionary).resolve()
    markdown_path = (repo_root / args.markdown).resolve()

    doc = _load_json(dictionary_path)
    dict_codes = _validate_dictionary(doc, dictionary_path=dictionary_path)

    md_codes = _extract_codes_from_markdown(markdown_path)
    if md_codes != dict_codes:
        missing_in_json = md_codes - dict_codes
        extra_in_json = dict_codes - md_codes
        sys.stderr.write("[policy_violations_dictionary] error: markdown/json codes mismatch\n")
        if missing_in_json:
            sys.stderr.write(f"  missing_in_json: {_fmt_codes(missing_in_json)}\n")
        if extra_in_json:
            sys.stderr.write(f"  extra_in_json: {_fmt_codes(extra_in_json)}\n")
        return 2

    targets = [
        ScanTarget(path=repo_root / "scripts" / "run_basic_no_problem_gate.py", mode="add_policy_violation"),
        ScanTarget(path=repo_root / "tests" / "libcurl_consistency" / "run_gate.py", mode="policy_violations_list"),
    ]
    used_codes: Set[str] = set()
    for t in targets:
        used_codes |= _scan_codes_from_python_file(t.path, t.mode)

    unknown = used_codes - dict_codes
    if unknown:
        sys.stderr.write("[policy_violations_dictionary] error: unregistered policy_violations codes detected\n")
        sys.stderr.write(f"  unknown: {_fmt_codes(unknown)}\n")
        sys.stderr.write(f"  register in: {dictionary_path}\n")
        return 2

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
