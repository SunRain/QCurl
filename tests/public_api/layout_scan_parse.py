"""Parsing helpers for public API layout scanning."""

from __future__ import annotations

from dataclasses import dataclass
import re


def strip_comments_and_strings(source: str) -> str:
    """Strip comments and string literals while preserving line structure."""
    result: list[str] = []
    i = 0
    length = len(source)
    state = "normal"
    quote = ""

    while i < length:
        ch = source[i]
        nxt = source[i + 1] if i + 1 < length else ""

        if state == "normal":
            if ch == "/" and nxt == "/":
                result.extend("  ")
                i += 2
                state = "line_comment"
                continue
            if ch == "/" and nxt == "*":
                result.extend("  ")
                i += 2
                state = "block_comment"
                continue
            if ch in {'"', "'"}:
                result.append(" ")
                quote = ch
                i += 1
                state = "string"
                continue
            result.append(ch)
            i += 1
            continue

        if state == "line_comment":
            if ch == "\n":
                result.append("\n")
                state = "normal"
            else:
                result.append(" ")
            i += 1
            continue

        if state == "block_comment":
            if ch == "*" and nxt == "/":
                result.extend("  ")
                i += 2
                state = "normal"
            else:
                result.append("\n" if ch == "\n" else " ")
                i += 1
            continue

        if state == "string":
            if ch == "\\" and i + 1 < length:
                result.extend("  ")
                i += 2
                continue
            if ch == quote:
                result.append(" ")
                i += 1
                state = "normal"
                continue
            result.append("\n" if ch == "\n" else " ")
            i += 1

    return "".join(result)


@dataclass(frozen=True)
class ExportedType:
    """Structured metadata for an exported type declaration."""

    kind: str
    name: str
    start: int
    end: int
    body_start: int
    body_end: int
    line: int
    inherits_qobject: bool


def line_number_for_offset(text: str, offset: int) -> int:
    """Map a byte offset in `text` to a 1-based line number."""
    return text.count("\n", 0, offset) + 1


def find_matching_brace(text: str, opening_brace: int) -> int:
    """Find the matching `}` for `text[opening_brace] == '{'`."""
    depth = 0
    for index in range(opening_brace, len(text)):
        ch = text[index]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return index
    return -1


def collect_exported_types(stripped_header: str) -> list[ExportedType]:
    """Collect `class|struct QCURL_EXPORT ... { ... }` declarations."""
    export_pattern = re.compile(r"\b(class|struct)\s+QCURL_EXPORT\s+([A-Za-z_][A-Za-z_0-9]*)\b")
    exported: list[ExportedType] = []
    for match in export_pattern.finditer(stripped_header):
        body_start = stripped_header.find("{", match.end())
        if body_start == -1:
            continue
        body_end = find_matching_brace(stripped_header, body_start)
        if body_end == -1:
            continue
        end = stripped_header.find(";", body_end)
        if end == -1:
            end = body_end

        exported.append(
            ExportedType(
                kind=match.group(1),
                name=match.group(2),
                start=match.start(),
                end=end,
                body_start=body_start,
                body_end=body_end,
                line=line_number_for_offset(stripped_header, match.start()),
                inherits_qobject="QObject" in stripped_header[match.start() : body_start],
            )
        )
    return exported


def split_public_statements(body: str, *, default_public: bool) -> list[str]:
    """Split type body into top-level statements that are in public section."""
    return split_access_statements(body, default_public=default_public, target_access="public")


def split_private_statements(body: str, *, default_public: bool) -> list[str]:
    """Split type body into top-level statements that are in private section."""
    return split_access_statements(body, default_public=default_public, target_access="private")


def split_access_statements(body: str, *, default_public: bool, target_access: str) -> list[str]:
    """Split type body into top-level statements for one C++ access section."""
    statements: list[str] = []
    current_access = "public" if default_public else "private"
    depth = 0
    statement_chars: list[str] = []

    for line in body.splitlines(keepends=True):
        access = re.match(r"^\s*(public|private|protected)\s*:\s*$", line)
        if depth == 0 and access:
            current_access = access.group(1)
            continue

        for ch in line:
            statement_chars.append(ch)
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth = max(depth - 1, 0)

            if ch == ";" and depth == 0:
                statement = "".join(statement_chars).strip()
                if current_access == target_access and statement:
                    statements.append(statement)
                statement_chars = []

    trailing = "".join(statement_chars).strip()
    if trailing and current_access == target_access:
        statements.append(trailing)
    return statements
