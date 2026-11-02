"""Clang AST support for QCurl's direct-field authorization gate."""

from __future__ import annotations

from collections.abc import Iterable, Sequence
from dataclasses import dataclass
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
from typing import Protocol


PREFIX_PATTERN = re.compile(r"\bm_[A-Za-z][A-Za-z0-9_]*\b")
NON_CODE_PATTERN = re.compile(
    r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
    re.DOTALL,
)
RECORD_PATTERN = re.compile(r"\b(?:class|struct|union)\b(?P<header>[^;{}]*)\{")
FIELD_DUMP_PATTERN = re.compile(
    r"^FieldDecl\b[^\n]*?<(?P<path>.+?):(?P<line>\d+):(?P<column>\d+)"
    r"(?:,|>)[^\n]*?\b(?P<name>m_[A-Za-z][A-Za-z0-9_]*)\b",
    re.MULTILINE,
)
FIELD_NOTE_PATTERN = re.compile(
    r'^(?P<path>.+?):(?P<line>\d+):(?P<column>\d+): note: "field" binds here$',
    re.MULTILINE,
)
MATCH_COUNT_PATTERN = re.compile(r"(?P<count>\d+) match(?:es)?\.\s*$")
GCC_ONLY_FLAGS = {"-mno-direct-extern-access"}
CLANG_QUERY_TIMEOUT_SECONDS = 180
# clang-query 会同时保留本次构建的所有 AST。逐文件运行，并将同一文件的
# 编译命令限制为每进程最多 8 条；同一路径的不同编译配置不能只算一个 TU。
# 头文件仍由 Clang 原生推断一条命令。AST 大小随源码变化，不承诺固定字节上限。
CLANG_QUERY_BATCH_SIZE = 8


class Authorization(Protocol):
    @property
    def relative_path(self) -> str: ...

    @property
    def type_name(self) -> str: ...


class AstScanError(RuntimeError):
    """Reports an unusable compile database or failed Clang AST scan."""


@dataclass(frozen=True)
class PublicPrefixedMember:
    """Identifies one public non-static field whose name starts with m_."""

    relative_path: str
    record_name: str
    field_name: str
    line: int


@dataclass(frozen=True)
class _RecordSpan:
    name: str
    start: int
    end: int


def sanitize_compile_arguments(arguments: Sequence[str]) -> list[str]:
    """Remove compiler-specific flags rejected by Clang tooling."""

    return [argument for argument in arguments if argument not in GCC_ONLY_FLAGS]


def classify_public_members(
    members: Iterable[PublicPrefixedMember], authorizations: Iterable[Authorization]
) -> list[str]:
    """Reject every public m_ field whose owning type lacks explicit authorization."""

    authorized = {
        (entry.relative_path, entry.type_name) for entry in authorizations
    }
    return [
        f"{member.relative_path}:{member.line}:{member.record_name}: "
        f"public m_ 字段未分类: {member.field_name}; "
        "普通行为类必须封装，直接字段类型必须显式授权"
        for member in members
        if (member.relative_path, member.record_name) not in authorized
    ]


def parse_ast_public_members(
    source_root: Path, output: str
) -> list[PublicPrefixedMember]:
    """Map clang-query field bindings to their innermost source record."""

    members: dict[tuple[str, int, str], PublicPrefixedMember] = {}
    for member in _parse_ast_bindings(source_root, output):
        members.setdefault(
            (member.relative_path, member.line, member.field_name), member
        )
    return list(members.values())


def _parse_ast_bindings(source_root: Path, output: str) -> list[PublicPrefixedMember]:
    """Keep repeated bindings until every Clang match has been accounted for."""

    source_root = source_root.resolve()
    locations = [
        (match.group("path"), int(match.group("line")), match.group("name"))
        for match in FIELD_DUMP_PATTERN.finditer(output)
    ]
    if not locations:
        locations = _parse_note_locations(source_root, output)

    sources: dict[Path, str] = {}
    spans: dict[Path, list[_RecordSpan]] = {}
    members: list[PublicPrefixedMember] = []
    for raw_path, line, field_name in locations:
        path = Path(raw_path)
        path = (path if path.is_absolute() else source_root / path).resolve()
        try:
            relative_path = path.relative_to(source_root).as_posix()
        except ValueError:
            continue
        if not relative_path.startswith("src/"):
            continue

        if path not in sources:
            sources[path] = path.read_text(encoding="utf-8")
            spans[path] = _record_spans(sources[path])
        source = sources[path]
        record_spans = spans[path]
        record_name = _record_name_at_line(source, record_spans, line)
        members.append(
            PublicPrefixedMember(
                relative_path=relative_path,
                record_name=record_name,
                field_name=field_name,
                line=line,
            )
        )
    return members


def discover_public_members(
    source_root: Path,
    sources: dict[str, str],
    compile_commands: Path | None = None,
    clang_query: str | None = None,
) -> list[PublicPrefixedMember]:
    """Enumerate all public m_ fields in candidate source files through Clang AST.

    Run one file per process, splitting its compile configurations into batches
    of at most ``CLANG_QUERY_BATCH_SIZE`` ASTs. Missing header commands retain
    Clang's native inference rather than guessing compiler flags here.
    """

    candidates = [
        (source_root / relative_path).resolve()
        for relative_path, source in sources.items()
        if PREFIX_PATTERN.search(source)
    ]
    if not candidates:
        return []

    database_path = _resolve_compile_commands(source_root, compile_commands)
    executable = _resolve_clang_query(clang_query)
    entries = _read_sanitized_database(database_path)
    commands_by_file: dict[Path, list[dict[str, object]]] = {}
    for entry in entries:
        path = Path(str(entry["file"]))
        directory = Path(str(entry["directory"]))
        path = (path if path.is_absolute() else directory / path).resolve()
        commands_by_file.setdefault(path, []).append(entry)

    members: dict[tuple[str, int, str], PublicPrefixedMember] = {}
    with tempfile.TemporaryDirectory(prefix="qcurl-member-ast-") as directory:
        database_directory = Path(directory)
        inferred_directory = database_directory / "inferred"
        inferred_directory.mkdir()
        # 推断必须看到完整数据库，不能因当前文件的裁剪命令而换用另一组 flags。
        _write_compile_database(entries, inferred_directory)
        for path in candidates:
            commands = commands_by_file.get(path)
            batches: list[list[dict[str, object]] | None]
            if commands is None:
                batches = [None]
            else:
                batches = [
                    commands[start : start + CLANG_QUERY_BATCH_SIZE]
                    for start in range(0, len(commands), CLANG_QUERY_BATCH_SIZE)
                ]
            for batch in batches:
                scan_directory = inferred_directory
                if batch is not None:
                    _write_compile_database(batch, database_directory)
                    scan_directory = database_directory
                for member in _scan_file(source_root, executable, scan_directory, path):
                    members.setdefault(
                        (member.relative_path, member.line, member.field_name), member
                    )
    return list(members.values())


def _scan_file(
    source_root: Path,
    executable: str,
    database_directory: Path,
    path: Path,
) -> list[PublicPrefixedMember]:
    """Run clang-query using the bounded configurations prepared for one file."""

    command = [
        executable,
        "-p",
        str(database_directory),
        str(path),
        "-c",
        "set output dump",
        "-c",
        "set bind-root false",
        "-c",
        (
            "match fieldDecl(isPublic(), "
            'matchesName("(^|::)m_[A-Za-z][A-Za-z0-9_]*$"), '
            'isExpansionInMainFile()).bind("field")'
        ),
    ]
    try:
        result = subprocess.run(
            command,
            cwd=source_root,
            text=True,
            capture_output=True,
            check=False,
            timeout=CLANG_QUERY_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as error:
        raise AstScanError(
            "clang-query 执行超时 "
            f"({CLANG_QUERY_TIMEOUT_SECONDS} 秒)"
        ) from error
    except OSError as error:
        raise AstScanError(f"无法启动 clang-query: {error}") from error

    output = result.stdout + result.stderr
    if result.returncode != 0 or re.search(r"(^|\n)[^\n]*\berror:", output):
        detail = _first_error_line(output) or f"clang-query exit code {result.returncode}"
        raise AstScanError(detail)

    count_match = MATCH_COUNT_PATTERN.search(result.stdout)
    if count_match is None:
        raise AstScanError("clang-query 未返回匹配数量摘要")
    try:
        members = _parse_ast_bindings(source_root, result.stdout)
    except (OSError, UnicodeError, ValueError) as error:
        raise AstScanError(f"无法分类 clang-query 字段绑定: {error}") from error
    if int(count_match.group("count")) != len(members):
        raise AstScanError(
            "clang-query 匹配数量与可分类字段数量不一致: "
            f"{count_match.group('count')} != {len(members)}"
        )
    return members


def _parse_note_locations(source_root: Path, output: str) -> list[tuple[str, int, str]]:
    locations: list[tuple[str, int, str]] = []
    for match in FIELD_NOTE_PATTERN.finditer(output):
        path = Path(match.group("path"))
        if not path.is_absolute():
            path = source_root / path
        lines = path.read_text(encoding="utf-8").splitlines()
        line = int(match.group("line"))
        names = (
            PREFIX_PATTERN.findall(lines[line - 1]) if 0 < line <= len(lines) else []
        )
        if len(names) == 1:
            locations.append((str(path), line, names[0]))
    return locations


def _record_spans(source: str) -> list[_RecordSpan]:
    code = NON_CODE_PATTERN.sub(lambda match: " " * len(match.group(0)), source)
    spans: list[_RecordSpan] = []
    for match in RECORD_PATTERN.finditer(code):
        name = _record_name(match.group("header"))
        closing_brace = _closing_brace(code, match.end() - 1)
        if closing_brace >= 0:
            spans.append(_RecordSpan(name=name, start=match.start(), end=closing_brace))
    return spans


def _record_name(header: str) -> str:
    declaration = re.split(r"(?<!:):(?!:)", header, maxsplit=1)[0]
    identifiers = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", declaration)
    for identifier in identifiers:
        if identifier in {"alignas", "final"}:
            continue
        if identifier.endswith("_EXPORT") or identifier.startswith("Q_DECL_"):
            continue
        return identifier
    return "<anonymous>"


def _closing_brace(code: str, opening_brace: int) -> int:
    depth = 0
    for index in range(opening_brace, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return -1


def _record_name_at_line(source: str, spans: list[_RecordSpan], line: int) -> str:
    lines = source.splitlines(keepends=True)
    offset = sum(len(value) for value in lines[: max(0, line - 1)])
    containing = [span for span in spans if span.start <= offset <= span.end]
    return max(containing, key=lambda span: span.start).name if containing else "<anonymous>"


def _resolve_compile_commands(source_root: Path, requested: Path | None) -> Path:
    candidates = []
    if requested is not None:
        candidates.append(requested)
    candidates.extend((source_root / "build-clang", source_root / "build"))
    for candidate in candidates:
        path = candidate / "compile_commands.json" if candidate.is_dir() else candidate
        if path.is_file():
            return path.resolve()
    raise AstScanError("未找到 compile_commands.json，无法执行全局 Clang AST 分类")


def _resolve_clang_query(requested: str | None) -> str:
    executable = shutil.which(requested or "clang-query")
    if not executable:
        raise AstScanError("未找到 clang-query，无法执行全局 Clang AST 分类")
    return executable


def _read_sanitized_database(source: Path) -> list[dict[str, object]]:
    try:
        entries = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise AstScanError(f"无法解析 compile_commands.json: {error}") from error
    if not isinstance(entries, list):
        raise AstScanError("无效的 compile_commands.json: 顶层必须是数组")

    sanitized: list[dict[str, object]] = []
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            raise AstScanError(
                f"无效的 compile_commands.json: 第 {index + 1} 项不是对象"
            )
        directory = entry.get("directory")
        file_path = entry.get("file")
        if not isinstance(directory, str) or not isinstance(file_path, str):
            raise AstScanError(
                "无效的 compile_commands.json: "
                f"第 {index + 1} 项缺少字符串 directory/file"
            )
        arguments = entry.get("arguments")
        if arguments is None:
            command = entry.get("command")
            if not isinstance(command, str):
                raise AstScanError(
                    "无效的 compile_commands.json: "
                    f"第 {index + 1} 项缺少 arguments/command"
                )
            try:
                arguments = shlex.split(command)
            except ValueError as error:
                raise AstScanError(
                    "无效的 compile_commands.json: "
                    f"第 {index + 1} 项 command 无法解析: {error}"
                ) from error
        if not isinstance(arguments, list) or not all(
            isinstance(argument, str) for argument in arguments
        ):
            raise AstScanError(
                "无效的 compile_commands.json: "
                f"第 {index + 1} 项 arguments 必须是字符串数组"
            )
        value: dict[str, object] = {
            "directory": directory,
            "file": file_path,
            "arguments": sanitize_compile_arguments(arguments),
        }
        if "output" in entry:
            value["output"] = entry["output"]
        sanitized.append(value)
    return sanitized


def _write_compile_database(
    entries: Sequence[dict[str, object]], destination: Path
) -> None:
    (destination / "compile_commands.json").write_text(
        json.dumps(entries, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def _first_error_line(output: str) -> str:
    for line in output.splitlines():
        if "error:" in line:
            return line.strip()
    return ""
