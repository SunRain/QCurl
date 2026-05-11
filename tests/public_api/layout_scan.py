"""Layout and header scanning helpers for public API guardrails."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
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


@dataclass(frozen=True)
class GuardrailFinding:
    """A single public API guardrail finding."""

    key: str
    message: str


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
                if current_access == "public" and statement:
                    statements.append(statement)
                statement_chars = []

    trailing = "".join(statement_chars).strip()
    if trailing and current_access == "public":
        statements.append(trailing)
    return statements


def type_public_field_exists(statements: list[str]) -> bool:
    """Heuristically detect if public statements contain data fields."""
    skip_prefixes = (
        "using ",
        "typedef ",
        "static_assert",
        "Q_OBJECT",
        "Q_GADGET",
        "Q_PROPERTY",
        "Q_DECLARE_",
        "friend ",
        "enum ",
        "class ",
        "struct ",
    )
    for statement in statements:
        normalized = " ".join(statement.split())
        if not normalized:
            continue
        if normalized.startswith(skip_prefixes):
            continue
        if "(" in normalized or ")" in normalized:
            continue
        if normalized.startswith("operator "):
            continue
        if normalized.endswith(";"):
            return True
    return False


def collect_nested_public_structs(body: str, *, default_public: bool) -> list[str]:
    """Collect names of nested structs declared in public section."""
    nested: list[str] = []
    statements = split_public_statements(body, default_public=default_public)
    nested_pattern = re.compile(
        r"^(?:[A-Z_][A-Z_0-9]*\s+)*struct\s+(?:QCURL_EXPORT\s+)?([A-Za-z_][A-Za-z_0-9]*)\b"
    )
    for statement in statements:
        normalized = " ".join(statement.split())
        match = nested_pattern.match(normalized)
        if match and "{" in normalized:
            nested.append(match.group(1))
    return nested


def has_top_level_exported_signature_with_type(stripped_header: str, type_name: str) -> bool:
    """Return True if a free exported signature references `type_name`."""
    signature_pattern = re.compile(r"\bQCURL_EXPORT\b[^;{}]*\([^;{}]*\)\s*[^;{}]*;")
    type_pattern = re.compile(rf"\b{re.escape(type_name)}\b")
    for match in signature_pattern.finditer(stripped_header):
        statement = match.group(0)
        if statement.lstrip().startswith(("class ", "struct ")):
            continue
        if type_pattern.search(statement):
            return True
    return False


def has_public_method_signature_with_type(body: str, type_name: str, *, default_public: bool) -> bool:
    """Return True if a public method declaration references `type_name`."""
    type_pattern = re.compile(rf"\b{re.escape(type_name)}\b")
    for statement in split_public_statements(body, default_public=default_public):
        normalized = " ".join(statement.split())
        if "(" not in normalized or ")" not in normalized:
            continue
        if type_pattern.search(normalized):
            return True
    return False


def collect_cpp_sources(source_root: Path) -> str:
    """Concatenate C++ sources for out-of-line definition checks."""
    parts: list[str] = []
    for suffix in ("*.cpp", "*.cc", "*.cxx"):
        for path in source_root.rglob(suffix):
            parts.append(path.read_text(encoding="utf-8"))
    return "\n".join(parts)


def has_out_of_line_definition(source_text: str, type_name: str, member_kind: str) -> bool:
    """Check whether a special member has an out-of-line definition."""
    patterns = {
        "destructor": rf"\b{re.escape(type_name)}::\s*~{re.escape(type_name)}\s*\(",
        "copy_ctor": (
            rf"\b{re.escape(type_name)}::\s*{re.escape(type_name)}\s*"
            rf"\(\s*const\s+{re.escape(type_name)}\s*&"
        ),
        "move_ctor": (
            rf"\b{re.escape(type_name)}::\s*{re.escape(type_name)}\s*"
            rf"\(\s*{re.escape(type_name)}\s*&&"
        ),
        "copy_assign": (
            rf"\b{re.escape(type_name)}::\s*operator=\s*"
            rf"\(\s*const\s+{re.escape(type_name)}\s*&"
        ),
        "move_assign": (
            rf"\b{re.escape(type_name)}::\s*operator=\s*"
            rf"\(\s*{re.escape(type_name)}\s*&&"
        ),
    }
    pattern = patterns.get(member_kind)
    if not pattern:
        return False
    return re.search(pattern, source_text) is not None


def find_member_fragment(body: str, type_name: str, member_kind: str) -> str | None:
    """Return the first declaration/definition fragment for a special member."""
    compact = " ".join(body.split())
    pattern_map = {
        "destructor": rf"~{re.escape(type_name)}\s*\([^)]*\)\s*[^;{{}}]*(?:;|\{{)",
        "copy_ctor": (
            rf"{re.escape(type_name)}\s*\(\s*const\s+{re.escape(type_name)}\s*&[^)]*\)"
            rf"\s*[^;{{}}]*(?:;|\{{)"
        ),
        "move_ctor": (
            rf"{re.escape(type_name)}\s*\(\s*{re.escape(type_name)}\s*&&[^)]*\)"
            rf"\s*[^;{{}}]*(?:;|\{{)"
        ),
        "copy_assign": (
            rf"{re.escape(type_name)}\s*&\s*operator=\s*"
            rf"\(\s*const\s+{re.escape(type_name)}\s*&[^)]*\)\s*[^;{{}}]*(?:;|\{{)"
        ),
        "move_assign": (
            rf"{re.escape(type_name)}\s*&\s*operator=\s*"
            rf"\(\s*{re.escape(type_name)}\s*&&[^)]*\)\s*[^;{{}}]*(?:;|\{{)"
        ),
    }
    pattern = pattern_map.get(member_kind)
    if not pattern:
        return None
    match = re.search(pattern, compact)
    return match.group(0) if match else None


def load_allowlist(path: Path) -> set[str]:
    """Load allowlisted finding keys from file."""
    if not path.is_file():
        return set()
    keys: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        normalized = line.strip()
        if not normalized or normalized.startswith("#"):
            continue
        keys.add(normalized)
    return keys


def collect_layout_findings(
    header: str,
    stripped_header: str,
    source_text: str,
) -> list[GuardrailFinding]:
    """Collect ABI/layout findings for one public header."""
    findings: list[GuardrailFinding] = []
    exported_types = collect_exported_types(stripped_header)

    exported_struct_names = [t.name for t in exported_types if t.kind == "struct"]
    for exported in exported_types:
        body = stripped_header[exported.body_start + 1 : exported.body_end]
        default_public = exported.kind == "struct"
        public_statements = split_public_statements(body, default_public=default_public)

        if type_public_field_exists(public_statements):
            findings.append(
                GuardrailFinding(
                    key=f"public-fields:{header}:{exported.name}",
                    message=f"{header}:{exported.line}: exported type `{exported.name}` has public data fields",
                )
            )

        nested_structs = collect_nested_public_structs(body, default_public=default_public)
        for nested in nested_structs:
            findings.append(
                GuardrailFinding(
                    key=f"struct-layout:nested:{header}:{exported.name}::{nested}",
                    message=(
                        f"{header}:{exported.line}: exported type `{exported.name}` exposes nested struct "
                        f"`{nested}` in public API"
                    ),
                )
            )

        signature_struct_names = exported_struct_names + nested_structs
        for struct_name in sorted(set(signature_struct_names)):
            if has_public_method_signature_with_type(
                body,
                struct_name,
                default_public=default_public,
            ):
                findings.append(
                    GuardrailFinding(
                        key=f"struct-layout:signature:{header}:{exported.name}:{struct_name}",
                        message=(
                            f"{header}:{exported.line}: exported signature in `{exported.name}` references "
                            f"struct type `{struct_name}`"
                        ),
                    )
                )

        has_incomplete_holder = "QScopedPointer<" in body or "QSharedDataPointer<" in body
        if not has_incomplete_holder:
            continue

        non_copyable = "Q_DISABLE_COPY" in body or exported.inherits_qobject

        required_members = ["destructor"]
        optional_members: list[str] = ["move_ctor", "move_assign"]
        if not non_copyable:
            required_members.extend(["copy_ctor", "copy_assign"])
        else:
            optional_members.extend(["copy_ctor", "copy_assign"])

        for member in required_members:
            fragment = find_member_fragment(body, exported.name, member)
            if fragment is None:
                findings.append(
                    GuardrailFinding(
                        key=f"out-of-line:implicit:{header}:{exported.name}:{member}",
                        message=(
                            f"{header}:{exported.line}: `{exported.name}` is missing explicit `{member}` "
                            "declaration for incomplete-type holder"
                        ),
                    )
                )
                continue

            if "= delete" in fragment:
                continue
            if "= default" in fragment or "{" in fragment:
                findings.append(
                    GuardrailFinding(
                        key=f"out-of-line:inline-or-default:{header}:{exported.name}:{member}",
                        message=(
                            f"{header}:{exported.line}: `{exported.name}` has `{member}` inline/default "
                            "in header; must be out-of-line"
                        ),
                    )
                )
                continue

            if not has_out_of_line_definition(source_text, exported.name, member):
                findings.append(
                    GuardrailFinding(
                        key=f"out-of-line:missing-def:{header}:{exported.name}:{member}",
                        message=(
                            f"{header}:{exported.line}: `{exported.name}` declares `{member}` but no "
                            "out-of-line definition was found in source files"
                        ),
                    )
                )

        for member in optional_members:
            fragment = find_member_fragment(body, exported.name, member)
            if fragment is None or "= delete" in fragment:
                continue
            if "= default" in fragment or "{" in fragment:
                findings.append(
                    GuardrailFinding(
                        key=f"out-of-line:inline-or-default:{header}:{exported.name}:{member}",
                        message=(
                            f"{header}:{exported.line}: `{exported.name}` has `{member}` inline/default "
                            "in header; must be out-of-line"
                        ),
                    )
                )
                continue
            if not has_out_of_line_definition(source_text, exported.name, member):
                findings.append(
                    GuardrailFinding(
                        key=f"out-of-line:missing-def:{header}:{exported.name}:{member}",
                        message=(
                            f"{header}:{exported.line}: `{exported.name}` declares `{member}` but no "
                            "out-of-line definition was found in source files"
                        ),
                    )
                )

    for struct_name in sorted(set(exported_struct_names)):
        if has_top_level_exported_signature_with_type(stripped_header, struct_name):
            findings.append(
                GuardrailFinding(
                    key=f"struct-layout:free-signature:{header}:{struct_name}",
                    message=(
                        f"{header}: top-level exported signature references exported struct `{struct_name}`"
                    ),
                )
            )

    return findings


