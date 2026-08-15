#!/usr/bin/env python3
"""Validate QCurl's explicitly authorized direct-field models."""

from __future__ import annotations

import argparse
from collections.abc import Iterable, Sequence
from pathlib import Path
import re
import sys

try:
    from scripts.qt_member_ast import (
        AstScanError,
        PublicPrefixedMember,
        classify_public_members,
        discover_public_members,
        parse_ast_public_members,
        sanitize_compile_arguments,
    )
    from scripts.qt_member_authorizations import (
        DIRECT_FIELD_AUTHORIZATIONS,
        DirectFieldAuthorization,
    )
except ModuleNotFoundError:  # Direct invocation keeps the repository script usable.
    from qt_member_ast import (  # type: ignore[no-redef]
        AstScanError,
        PublicPrefixedMember,
        classify_public_members,
        discover_public_members,
        parse_ast_public_members,
        sanitize_compile_arguments,
    )
    from qt_member_authorizations import (  # type: ignore[no-redef]
        DIRECT_FIELD_AUTHORIZATIONS,
        DirectFieldAuthorization,
    )


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
NOISE_PATTERN = re.compile(
    r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
    re.DOTALL,
)
PIMPL_DEFINITION_PATTERN = re.compile(
    r"\bclass\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*Private)\b[^;{]*\{"
)
SHARED_DATA_DEFINITION_PATTERN = re.compile(
    r"\bclass\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*:\s*public\s+QSharedData\b[^;{]*\{"
)
ACCESS_LABEL_PATTERN = re.compile(r"[ \t]*(public|protected|private)\s*:")
PREFIX_PATTERN = re.compile(r"\bm_[A-Za-z][A-Za-z0-9_]*\b")
CHINESE_PATTERN = re.compile(r"[\u3400-\u9fff]")


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path.cwd())
    parser.add_argument("--compile-commands", type=Path)
    parser.add_argument("--clang-query")
    return parser.parse_args(argv)


def strip_non_code(text: str) -> str:
    def replacement(match: re.Match[str]) -> str:
        value = match.group(0)
        return "\n" * value.count("\n") + " " * (len(value) - value.count("\n"))

    return NOISE_PATTERN.sub(replacement, text)


def read_sources(source_root: Path) -> dict[str, str]:
    src_root = source_root / "src"
    if not src_root.is_dir():
        return {}

    return {
        path.relative_to(source_root).as_posix(): path.read_text(encoding="utf-8")
        for path in sorted(src_root.rglob("*"))
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    }


def discover_candidates(
    sources: dict[str, str], pattern: re.Pattern[str]
) -> set[tuple[str, str]]:
    return {
        (relative_path, name)
        for relative_path, source in sources.items()
        for name in pattern.findall(strip_non_code(source))
    }


def record_body(source: str, record_name: str) -> str:
    pattern = re.compile(rf"\b(?:class|struct)\s+{re.escape(record_name)}\b[^;{{]*\{{")
    match = pattern.search(strip_non_code(source))
    if not match:
        raise ValueError(f"未找到类型定义: {record_name}")

    source_code = strip_non_code(source)
    body_start = match.end()
    depth = 1
    for index in range(body_start, len(source_code)):
        if source_code[index] == "{":
            depth += 1
        elif source_code[index] == "}":
            depth -= 1
            if depth == 0:
                return source_code[body_start:index]
    raise ValueError(f"类型定义未闭合: {record_name}")


def _is_method_parameter(statement: str, name: str) -> bool:
    position = statement.find(name)
    open_parenthesis = statement.rfind("(", 0, position)
    close_parenthesis = statement.rfind(")")
    return open_parenthesis >= 0 and position < close_parenthesis


def public_member_prefixes(body: str, default_access: str = "private") -> list[str]:
    """Return m_-prefixed top-level fields from public sections only."""

    code = strip_non_code(body)
    access = default_access
    depth = 0
    statement_start = 0
    names: set[str] = set()
    discard_block = False
    index = 0

    while index < len(code):
        if depth == 0 and (index == 0 or code[index - 1] == "\n"):
            access_match = ACCESS_LABEL_PATTERN.match(code, index)
            if access_match:
                access = access_match.group(1)
                index = access_match.end()
                statement_start = index
                continue

        character = code[index]
        if character == "{":
            if depth == 0:
                statement_prefix = code[statement_start:index]
                discard_block = bool(
                    re.search(r"\b(?:class|struct|enum)\b", statement_prefix)
                    or (")" in statement_prefix and "=" not in statement_prefix)
                )
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0 and discard_block:
                statement_start = index + 1
                discard_block = False
        elif character == ";" and depth == 0:
            statement = code[statement_start : index + 1]
            if access == "public":
                names.update(
                    name
                    for name in PREFIX_PATTERN.findall(statement)
                    if not _is_method_parameter(statement, name)
                )
            statement_start = index + 1
        index += 1

    return sorted(names)


def has_chinese_doxygen_comment(source: str, record_name: str) -> bool:
    match = re.search(
        rf"\b(?:class|struct)\s+{re.escape(record_name)}\b[^;{{]*\{{",
        source,
    )
    if not match:
        return False

    prefix = source[: match.start()]
    block_comment = re.search(r"/\*\*(?P<body>.*?)\*/\s*$", prefix, re.DOTALL)
    if block_comment:
        return bool(CHINESE_PATTERN.search(block_comment.group("body")))

    line_comment = re.search(
        r"(?P<body>(?:^[ \t]*///[^\n]*(?:\n|$))+)[ \t]*$",
        prefix,
        re.MULTILINE,
    )
    return bool(line_comment and CHINESE_PATTERN.search(line_comment.group("body")))


def record_default_access(source: str, record_name: str) -> str:
    match = re.search(
        rf"\b(class|struct)\s+{re.escape(record_name)}\b", strip_non_code(source)
    )
    if not match:
        raise ValueError(f"未找到类型定义: {record_name}")
    return "public" if match.group(1) == "struct" else "private"


def _all_source_text(sources: dict[str, str]) -> str:
    return "\n".join(strip_non_code(source) for source in sources.values())


def _has_pimpl_relation(
    source_text: str, authorization: DirectFieldAuthorization
) -> bool:
    assert authorization.public_type
    public_type = re.escape(authorization.public_type)
    private_type = re.escape(authorization.type_name)
    pattern = re.compile(
        rf"Q_DECLARE_(?:PRIVATE|PUBLIC)\s*\(\s*{public_type}\s*\)"
        rf"|(?:QScopedPointer|std::unique_ptr)\s*<\s*(?:QCurl::)?{private_type}\s*>"
    )
    return bool(pattern.search(source_text))


def _has_shared_data_holder(source_text: str, type_name: str) -> bool:
    pattern = re.compile(
        rf"QSharedDataPointer\s*<\s*(?:QCurl::)?{re.escape(type_name)}\s*>"
    )
    return bool(pattern.search(source_text))


def _is_internal_pimpl_path(relative_path: str) -> bool:
    return (
        relative_path.startswith("src/private/")
        or relative_path.endswith("_p.h")
        or relative_path.endswith(".cpp")
    )


def _missing_authorizations(
    candidates: set[tuple[str, str]], authorized: set[tuple[str, str]]
) -> list[str]:
    return [
        f"{path}:{type_name}: 缺少显式授权 (missing explicit authorization)"
        for path, type_name in sorted(candidates - authorized)
    ]


def _validate_authorization_entry(
    entry: DirectFieldAuthorization,
    sources: dict[str, str],
    source_text: str,
    shared_candidates: set[tuple[str, str]],
) -> list[str]:
    source = sources.get(entry.relative_path)
    if source is None:
        return [f"{entry.relative_path}:{entry.type_name}: 授权文件不存在"]
    try:
        body = record_body(source, entry.type_name)
    except ValueError as error:
        return [f"{entry.relative_path}:{entry.type_name}: {error}"]

    prefix = f"{entry.relative_path}:{entry.type_name}"
    if entry.kind == "pimpl":
        expected_name = f"{entry.public_type}Private" if entry.public_type else ""
        if entry.type_name != expected_name or not _is_internal_pimpl_path(
            entry.relative_path
        ):
            return [f"{prefix}: PIMPL 授权信息无效"]
        if not _has_pimpl_relation(source_text, entry):
            return [f"{prefix}: 未找到公共实现关系"]
    elif entry.kind == "shared_data":
        if (entry.relative_path, entry.type_name) not in shared_candidates:
            return [f"{prefix}: 未继承 QSharedData"]
        if not _has_shared_data_holder(source_text, entry.type_name):
            return [f"{prefix}: 未找到真实的 shared-data holder"]
        if re.search(r"\bq_ptr\b|Q_DECLARE_PUBLIC\s*\(", body):
            return [
                f"{prefix}: shared-data 不得保存 owner 专属 q_ptr 或声明 Q_DECLARE_PUBLIC"
            ]
    elif entry.kind != "record":
        return [f"{prefix}: 未知直接字段模型"]
    return []


def validate_authorizations(
    source_root: Path, authorizations: Iterable[DirectFieldAuthorization]
) -> list[str]:
    sources = read_sources(source_root)
    source_text = _all_source_text(sources)
    authorization_list = tuple(authorizations)
    shared_candidates = discover_candidates(sources, SHARED_DATA_DEFINITION_PATTERN)
    pimpl_candidates = (
        discover_candidates(sources, PIMPL_DEFINITION_PATTERN) - shared_candidates
    )
    authorized_pimpl = {
        (entry.relative_path, entry.type_name)
        for entry in authorization_list
        if entry.kind == "pimpl"
    }
    authorized_shared = {
        (entry.relative_path, entry.type_name)
        for entry in authorization_list
        if entry.kind == "shared_data"
    }

    violations = _missing_authorizations(pimpl_candidates, authorized_pimpl)
    violations.extend(_missing_authorizations(shared_candidates, authorized_shared))

    seen: set[tuple[str, str]] = set()
    for entry in authorization_list:
        key = (entry.relative_path, entry.type_name)
        if key in seen:
            violations.append(f"{entry.relative_path}:{entry.type_name}: 授权重复")
            continue
        seen.add(key)
        violations.extend(
            _validate_authorization_entry(
                entry, sources, source_text, shared_candidates
            )
        )

    return violations


def inspect_authorized_types(
    source_root: Path, authorizations: Iterable[DirectFieldAuthorization]
) -> list[str]:
    sources = read_sources(source_root)
    violations: list[str] = []
    for entry in authorizations:
        source = sources.get(entry.relative_path)
        if source is None:
            continue
        try:
            body = record_body(source, entry.type_name)
        except ValueError:
            continue
        prefixes = public_member_prefixes(
            body, default_access=record_default_access(source, entry.type_name)
        )
        if prefixes:
            violations.append(
                f"{entry.relative_path}:{entry.type_name}: public 直接字段不得使用 m_: "
                f"{', '.join(prefixes)}"
            )
        if not has_chinese_doxygen_comment(source, entry.type_name):
            violations.append(
                f"{entry.relative_path}:{entry.type_name}: 缺少中文 Doxygen 类型注释"
            )
    return violations


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    source_root = args.source_root.resolve()
    violations = validate_authorizations(source_root, DIRECT_FIELD_AUTHORIZATIONS)
    ast_member_count = 0
    try:
        ast_members = discover_public_members(
            source_root,
            read_sources(source_root),
            compile_commands=args.compile_commands,
            clang_query=args.clang_query,
        )
        ast_member_count = len(ast_members)
        violations.extend(classify_public_members(ast_members, DIRECT_FIELD_AUTHORIZATIONS))
    except AstScanError as error:
        violations.append(f"全局 Clang AST 分类失败: {error}")
    violations.extend(
        inspect_authorized_types(source_root, DIRECT_FIELD_AUTHORIZATIONS)
    )
    if violations:
        print("Qt 直接字段授权门禁失败:", file=sys.stderr)
        for violation in violations:
            print(f"  {violation}", file=sys.stderr)
        return 1

    categories = {
        kind: sum(entry.kind == kind for entry in DIRECT_FIELD_AUTHORIZATIONS)
        for kind in ("pimpl", "record", "shared_data")
    }
    print(
        "Qt 直接字段授权门禁通过: "
        f"{categories['pimpl']} 个 PIMPL、{categories['record']} 个 record-like、"
        f"{categories['shared_data']} 个 shared-data 类型；"
        f"全局 AST public m_ 字段 {ast_member_count} 个且均已分类"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
