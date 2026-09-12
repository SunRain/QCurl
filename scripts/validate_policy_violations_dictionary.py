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
    note: str  # 该目标承载 policy code 的原因，供维护者判断增删


# 承载 policy code 字面量的调用形态：函数名 -> code 所在的位置参数索引。
#
# `add_policy_violation` 的签名是 `(manifest, code)`；包装它的辅助函数也必须登记在此，
# 否则包装函数内部的 `add_policy_violation(manifest, code)` 会让 code 字面量对本门禁不可见，
# 重新形成"新增未注册 code 却全绿"的假绿。未登记的包装函数会出现在动态调用报告中。
_CODE_CARRYING_CALLS = {
    "add_policy_violation": 1,  # scripts/uce/manifest.py
    "_record_envelope_failure": 1,  # scripts/uce_gate/evidence.py
    "GateSpec": 3,  # scripts/uce_gate/planner.py 的稳定 policy_code 字段
    "_CtestSelection": 5,  # scripts/uce_gate/ctest_gates.py 的严格标签门禁
}

# 通过常量传递 policy code 的名称。常量本身仍是稳定字典的一部分，不能因为没有
# 直接调用 `add_policy_violation` 就从静态门禁中消失。
_CODE_CONSTANT_NAMES = frozenset({"TIMELINE_PARSE_POLICY"})

# 承载 policy code 字面量的累积器变量名。
#
# 这些变量上的 `.append("code")`、`.add("code")`、`.extend([...])` 以及
# `= {"code"}` / `= ["code"]` 初始化都会被采集。UCE 各模块先把 code 累积到本地列表/集合，
# 再由上层统一交给 `add_policy_violation`，因此只扫调用形态会漏掉这些定义点。
_CODE_ACCUMULATOR_NAMES = frozenset(
    {
        "policy_violations",  # report["policy_violations"] 与本地累积器
        "violations",  # scripts/uce_gate/{dci_contract,httpbin}.py
        "policy_codes",  # tests/uce/{ctbp,hes}/validate.py
    }
)


def _load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _policy_violation_code_node(node: ast.Call) -> ast.expr | None:
    """返回承载 policy code 的 AST 节点；调用形态不在登记表中时返回 None。

    形态与参数位置由 `_CODE_CARRYING_CALLS` 定义；同时接受 `code=` 关键字形式。
    """

    if not isinstance(node.func, ast.Name):
        return None
    index = _CODE_CARRYING_CALLS.get(node.func.id)
    if index is None:
        return None

    if len(node.args) > index:
        return node.args[index]
    for keyword in node.keywords:
        if keyword.arg == "code":
            return keyword.value
    return None


def count_dynamic_policy_violation_calls(path: Path) -> list[int]:
    """统计 code 实参不是字面量的 code-carrying 调用行号。

    只报告真正无法静态解析的调用（如 `add_policy_violation(manifest, gate_spec.policy_code)`，
    code 由运行期聚合而来）。包装函数自身定义体内的转发调用会被排除：`_record_envelope_failure`
    内部的 `add_policy_violation(manifest, code)` 中 code 是形参，其字面量在调用点已被采集，
    报告它只会制造"存在未覆盖 code"的错觉。
    """

    lines: list[int] = []
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))

    class Visitor(ast.NodeVisitor):
        def __init__(self) -> None:
            self._wrapper_depth = 0

        def _visit_function(self, node: ast.FunctionDef | ast.AsyncFunctionDef) -> None:
            is_wrapper = node.name in _CODE_CARRYING_CALLS
            self._wrapper_depth += int(is_wrapper)
            self.generic_visit(node)
            self._wrapper_depth -= int(is_wrapper)

        def visit_FunctionDef(self, node: ast.FunctionDef) -> None:  # noqa: N802
            self._visit_function(node)

        def visit_AsyncFunctionDef(self, node: ast.AsyncFunctionDef) -> None:  # noqa: N802
            self._visit_function(node)

        def visit_Call(self, node: ast.Call) -> None:  # noqa: N802
            if self._wrapper_depth == 0 and isinstance(node.func, ast.Name):
                if node.func.id in _CODE_CARRYING_CALLS:
                    code_node = _policy_violation_code_node(node)
                    is_literal = isinstance(code_node, ast.Constant) and isinstance(
                        code_node.value, str
                    )
                    if not is_literal:
                        lines.append(node.lineno)
            self.generic_visit(node)

    Visitor().visit(tree)
    return sorted(lines)


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


def _string_literals(node: ast.expr | None) -> Set[str]:
    """从表达式中取出字符串字面量；支持标量与 list/set/tuple 字面量。"""

    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        return {node.value}
    if isinstance(node, (ast.List, ast.Set, ast.Tuple)):
        return {
            elt.value
            for elt in node.elts
            if isinstance(elt, ast.Constant) and isinstance(elt.value, str)
        }
    return set()


def _is_code_accumulator(node: ast.expr) -> bool:
    """判断表达式是否指向已登记的 policy code 累积器。"""

    if isinstance(node, ast.Name):
        return node.id in _CODE_ACCUMULATOR_NAMES
    if not isinstance(node, ast.Subscript) or not isinstance(node.value, ast.Name):
        return False

    # `report["policy_violations"]` 是 netproof gate 的持久化报告形态。
    index = node.slice
    return isinstance(index, ast.Constant) and index.value == "policy_violations"


def _assignment_names(node: ast.Assign | ast.AnnAssign) -> set[str]:
    """返回赋值节点中可用于 policy 常量登记的名称。"""

    targets = node.targets if isinstance(node, ast.Assign) else (node.target,)
    return {target.id for target in targets if isinstance(target, ast.Name)}


def _scan_codes_from_python_file(path: Path) -> Set[str]:
    """采集文件中出现的 policy_violations code 字面量。

    同时覆盖两类定义点，无需调用方指定模式（模式与目标的组合本身曾是出错来源）：

    1. 调用形态：`_CODE_CARRYING_CALLS` 登记的函数，取其 code 位置参数或 `code=` 关键字。
    2. 累积器形态：`_CODE_ACCUMULATOR_NAMES` 登记的变量，取其 `.append/.add/.extend`
       实参与 `= {...}` / `= [...]` 初始化字面量。

    只采集字面量。运行期聚合的 code 由 `count_dynamic_policy_violation_calls()` 单独报告。
    """

    codes: Set[str] = set()
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))

    class Visitor(ast.NodeVisitor):
        def visit_Call(self, node: ast.Call) -> None:  # noqa: N802
            # 形态 1：add_policy_violation(manifest, "code") 及已登记的包装函数
            code_node = _policy_violation_code_node(node)
            codes.update(_string_literals(code_node))

            # 形态 2：violations.append("code") / violations.add("code") / .extend([...])
            if isinstance(node.func, ast.Attribute):
                if (
                    _is_code_accumulator(node.func.value)
                    and node.func.attr in {"append", "add", "extend", "update"}
                    and node.args
                ):
                    codes.update(_string_literals(node.args[0]))

            self.generic_visit(node)

        def visit_Assign(self, node: ast.Assign) -> None:  # noqa: N802
            # 形态 2 的初始化：violations = {"code"} / violations = ["code"]
            if _assignment_names(node) & _CODE_ACCUMULATOR_NAMES:
                codes.update(_string_literals(node.value))
            if _assignment_names(node) & _CODE_CONSTANT_NAMES:
                codes.update(_string_literals(node.value))
            self.generic_visit(node)

        def visit_AnnAssign(self, node: ast.AnnAssign) -> None:  # noqa: N802
            # 带注解的初始化：violations: set[str] = {"code"}
            if _assignment_names(node) & (_CODE_ACCUMULATOR_NAMES | _CODE_CONSTANT_NAMES):
                codes.update(_string_literals(node.value))
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


def _scan_targets(repo_root: Path) -> tuple[ScanTarget, ...]:
    """返回当前 UCE policy code 的全部静态 producer。"""

    return (
        ScanTarget(repo_root / "scripts" / "uce_gate" / "planner.py", "UCE gate 计划中的稳定 policy_code"),
        ScanTarget(repo_root / "scripts" / "uce_gate" / "orchestrator.py", "UCE 编排：汇总各 gate 的 violation"),
        ScanTarget(repo_root / "scripts" / "uce_gate" / "evidence.py", "证据打包与 archive envelope"),
        ScanTarget(repo_root / "scripts" / "uce_gate" / "execute.py", "consistency/netproof/redaction gate 执行"),
        ScanTarget(repo_root / "scripts" / "uce_gate" / "ctest_gates.py", "CTest acceptance 标签与严格跳过合同"),
        ScanTarget(repo_root / "scripts" / "uce_gate" / "finalize.py", "异常收口与归档终态"),
        ScanTarget(repo_root / "scripts" / "uce_gate" / "dci_contract.py", "DCI fixed-seed 合同"),
        ScanTarget(repo_root / "scripts" / "uce_gate" / "httpbin.py", "httpbin env gate 生命周期"),
        ScanTarget(repo_root / "scripts" / "uce_gate" / "qt_contracts.py", "BP QtTest 合同执行"),
        ScanTarget(repo_root / "scripts" / "netproof_strace_gate.py", "offline netproof 报告"),
        ScanTarget(repo_root / "scripts" / "run_uce_sanitizers.py", "sanitizer 构建与执行 gate"),
        ScanTarget(repo_root / "tests" / "libcurl_consistency" / "pytest_support" / "gate_report.py", "一致性 gate 的 policy checks 实现"),
        ScanTarget(repo_root / "tests" / "uce" / "bp" / "validate.py", "BP validator 产出的 code"),
        ScanTarget(repo_root / "tests" / "uce" / "ctbp" / "validate.py", "CTBP validator 产出的 code"),
        ScanTarget(repo_root / "tests" / "uce" / "hes" / "validate.py", "HES validator 产出的 code"),
        ScanTarget(repo_root / "tests" / "uce" / "timeline" / "parser.py", "TLC JSONL 解析错误 code"),
        ScanTarget(repo_root / "tests" / "uce" / "timeline" / "validate.py", "TLC validator 产出的 code"),
    )


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate policy_violations dictionary and forbid unregistered codes.")
    parser.add_argument(
        "--dictionary",
        default="docs/uce/policy_violations_dictionary.json",
        help="Path to machine-readable dictionary JSON (default: docs/uce/policy_violations_dictionary.json).",
    )
    parser.add_argument(
        "--markdown",
        default="docs/uce/policy_violations_dictionary.md",
        help="Path to human-readable dictionary MD (default: docs/uce/policy_violations_dictionary.md).",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = _parse_args(argv)

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

    # 扫描目标必须覆盖所有产出 policy code 的模块。
    # 漏扫的直接后果已有先例：`env_preflight_httpbin_stop_failed` 长期未注册，
    # 正是因为 httpbin.py 从未进入本列表。
    # 旧 basic-no-problem runner 已进入删除候选；当前扫描面以 UCE 产出模块为准。
    # tests/libcurl_consistency/run_gate.py 只是 CLI 委托入口，不直接生成 policy code，
    # 因此不应列为扫描目标；实际产出点是 pytest_support/gate_report.py。
    targets = _scan_targets(repo_root)
    used_codes: Set[str] = set()
    dynamic_calls: list[tuple[Path, int]] = []
    for t in targets:
        if not t.path.exists():
            sys.stderr.write(f"[policy_violations_dictionary] warning: scan target not found: {t.path}\n")
            continue
        used_codes |= _scan_codes_from_python_file(t.path)
        dynamic_calls.extend((t.path, line) for line in count_dynamic_policy_violation_calls(t.path))

    unknown = used_codes - dict_codes
    if unknown:
        sys.stderr.write("[policy_violations_dictionary] error: unregistered policy_violations codes detected\n")
        sys.stderr.write(f"  unknown: {_fmt_codes(unknown)}\n")
        sys.stderr.write(f"  register in: {dictionary_path}\n")
        return 2

    # 覆盖边界必须可见：变量实参的 code 无法静态解析，只能靠调用方自身保证已注册。
    if dynamic_calls:
        sys.stderr.write(
            f"[policy_violations_dictionary] note: {len(dynamic_calls)} 处 code 为变量实参，"
            "静态扫描不覆盖\n"
        )
        for path, line in dynamic_calls:
            sys.stderr.write(f"  {path.relative_to(repo_root)}:{line}\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
