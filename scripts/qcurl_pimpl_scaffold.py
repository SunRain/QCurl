#!/usr/bin/env python3
"""QCurl PIMPL scaffold helper (best-effort, non-blocking).

This script scans `src/CMakeLists.txt` for `QCURL_INSTALL_HEADERS` and reports
exported public types that likely need refactoring to Qt/KF-style ABI-friendly
patterns (d-pointer / implicit sharing).

It is intended as an engineering aid (not a CI gate).
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class ExportedType:
    name: str
    kind: str  # "class" | "struct"
    has_public_fields: bool
    heavy_includes: list[str]


def _read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _strip_line_comment(line: str) -> str:
    return line.split("#", 1)[0].strip()


def parse_cmake_install_headers(cmake_path: Path, var_name: str) -> list[str]:
    """Parse a simple `set(VAR ... )` list block and return items."""
    content = _read_text(cmake_path)
    lines = content.splitlines()

    start_re = re.compile(rf"^\s*set\(\s*{re.escape(var_name)}\b")
    headers: list[str] = []
    in_block = False

    for raw in lines:
        line = _strip_line_comment(raw)
        if not line:
            continue

        if not in_block:
            if start_re.search(line):
                in_block = True
                # Remove the leading "set(VAR" prefix so inline items work:
                line = start_re.sub("", line, count=1)
        if not in_block:
            continue

        # End of block?
        if ")" in line:
            before, _sep, _after = line.partition(")")
            tokens = before.split()
            headers.extend(tokens)
            break

        headers.extend(line.split())

    return [h for h in headers if h and not h.startswith("${")]


def detect_heavy_includes(source: str) -> list[str]:
    """Return a list of heavy include names detected in the header."""
    patterns = [
        (r"^\s*#\s*include\s*<Q(Mutex|ReadWriteLock|WaitCondition|Thread|ThreadPool|Semaphore)\b", "QtThreading"),
        (r"^\s*#\s*include\s*<Q(File|Dir|FileInfo|SaveFile|TemporaryFile|StandardPaths)\b", "QtFilesystem"),
        (r"^\s*#\s*include\s*<QProcess\b", "QtProcess"),
        (r"^\s*#\s*include\s*<QJson", "QtJson"),
        (r"^\s*#\s*include\s*<curl/", "libcurl"),
    ]

    heavy: list[str] = []
    for line in source.splitlines():
        if not line.lstrip().startswith("#"):
            continue
        for pattern, label in patterns:
            if re.search(pattern, line):
                heavy.append(label)
                break
    return sorted(set(heavy))


def detect_exported_types(header_path: Path) -> list[ExportedType]:
    source = _read_text(header_path)
    heavy_includes = detect_heavy_includes(source)

    # Match: class QCURL_EXPORT Foo / struct QCURL_EXPORT Foo
    exported = re.finditer(r"\b(class|struct)\s+QCURL_EXPORT\s+([A-Za-z_][A-Za-z_0-9]*)\b", source)
    results: list[ExportedType] = []

    for match in exported:
        kind = match.group(1)
        name = match.group(2)

        # Heuristic: if the exported type contains public data members in the header.
        # We keep this intentionally simple: scan the class body text until the first "};".
        body_start = source.find("{", match.end())
        body_end = source.find("};", body_start) if body_start != -1 else -1
        body = source[body_start:body_end] if body_start != -1 and body_end != -1 else ""

        # Public field pattern: a line ending with ';' that is not a function declaration.
        # This is heuristic; it may have false positives/negatives.
        has_public_fields = False
        if body:
            public_area = body
            # If there are explicit access labels, focus on 'public:' section.
            public_pos = body.find("public:")
            if public_pos != -1:
                public_area = body[public_pos:]
            for line in public_area.splitlines():
                stripped = line.strip()
                if not stripped or stripped.startswith("//") or stripped.startswith("/*"):
                    continue
                if "(" in stripped or ")" in stripped:
                    continue
                if stripped.endswith(";") and not stripped.startswith("typedef") and not stripped.startswith("using"):
                    # Likely a field or a forward declaration.
                    if "class " in stripped or "struct " in stripped or "enum " in stripped:
                        continue
                    if stripped.endswith(");"):
                        continue
                    has_public_fields = True
                    break

        results.append(
            ExportedType(
                name=name,
                kind=kind,
                has_public_fields=has_public_fields,
                heavy_includes=heavy_includes,
            )
        )

    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--cmake-file", type=Path, default=None)
    parser.add_argument("--var", default="QCURL_INSTALL_HEADERS")
    args = parser.parse_args()

    repo_root: Path = args.repo_root
    cmake_path = args.cmake_file or (repo_root / "src" / "CMakeLists.txt")
    if not cmake_path.is_file():
        print(f"[pimpl_scaffold] cmake file not found: {cmake_path}", file=sys.stderr)
        return 1

    headers = parse_cmake_install_headers(cmake_path, args.var)
    if not headers:
        print(f"[pimpl_scaffold] no headers found for {args.var} in {cmake_path}", file=sys.stderr)
        return 1

    print("# QCurl PIMPL scaffold report")
    print(f"- source: {cmake_path}")
    print(f"- var: {args.var}")
    print(f"- headers: {len(headers)}")
    print("")

    any_findings = False
    for header in headers:
        header_path = repo_root / "src" / header
        if not header_path.is_file():
            print(f"- {header}: [missing]", file=sys.stderr)
            continue

        exported_types = detect_exported_types(header_path)
        if not exported_types:
            continue

        any_findings = True
        print(f"## {header}")
        for t in exported_types:
            flags: list[str] = []
            if t.has_public_fields:
                flags.append("public-fields")
            flags.extend(t.heavy_includes)
            flag_text = (", ".join(flags)) if flags else "ok"
            print(f"- {t.kind} {t.name}: {flag_text}")
        print("")

    if not any_findings:
        print("[pimpl_scaffold] no exported types found in install headers (nothing to do).")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

