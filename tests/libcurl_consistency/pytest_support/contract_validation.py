"""一致性 gate YAML 合同的共享静态校验。"""

from __future__ import annotations

import ast
from pathlib import Path


SUITES = frozenset({"p0", "p1", "p2", "ext"})


def _local_functions(path: Path) -> set[str]:
    try:
        tree = ast.parse(path.read_text(encoding="utf-8"))
    except (OSError, SyntaxError):
        return set()
    names: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            names.add(node.name)
    return names


def validate_nodeid(repo_root: Path, nodeid: str, *, label: str) -> list[str]:
    """验证 nodeid 的文件和本地测试函数均存在。"""

    if "::" not in nodeid:
        return [f"{label}: nodeid must contain ::"]
    path_text, selector = nodeid.split("::", 1)
    path = repo_root / path_text
    if not path.is_file():
        return [f"{label}: nodeid path does not exist: {path_text}"]
    function_name = selector.split("[", 1)[0]
    if path_text.startswith("tests/libcurl_consistency/"):
        if function_name not in _local_functions(path):
            return [f"{label}: nodeid function does not exist: {function_name}"]
    return []
