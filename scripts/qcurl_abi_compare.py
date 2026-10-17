"""QCurl 两个 shared 组件的 DWARF snapshot 与保守 ABI 比较。"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

if __package__:
    from .qcurl_abi_common import (
        AbiGateError,
        resolve_existing_dir,
        resolve_existing_file,
        run,
        tool_path,
        validate_snapshot_output,
    )
    from .qcurl_abi_symbols import (
        collect_dynamic_symbols,
        validate_library_dynamic_symbols,
    )
else:
    from qcurl_abi_common import (
        AbiGateError,
        resolve_existing_dir,
        resolve_existing_file,
        run,
        tool_path,
        validate_snapshot_output,
    )
    from qcurl_abi_symbols import (
        collect_dynamic_symbols,
        validate_library_dynamic_symbols,
    )


ABIDIFF_ABI_CHANGE = 4
ABIDIFF_ABI_INCOMPATIBLE_CHANGE = 8
ABIDIFF_CHANGE_MASK = ABIDIFF_ABI_CHANGE | ABIDIFF_ABI_INCOMPATIBLE_CHANGE
COMPONENT_LIBRARIES = {"core": "libQCurl", "other-extras": "libQCurlOtherExtras"}
REPO_ROOT = Path(__file__).resolve().parents[1]


def validate_component_snapshot(path: Path, component: str) -> ET.Element:
    """要求 snapshot 含所选组件的 ELF 符号及可分析的 DWARF 声明。"""

    if component not in COMPONENT_LIBRARIES:
        raise AbiGateError(f"unknown ABI component: {component}")
    path = resolve_existing_file(path, "ABI snapshot")
    try:
        corpus = ET.parse(path).getroot()
    except (ET.ParseError, OSError) as exc:
        raise AbiGateError(f"invalid ABI snapshot: {path}: {exc}") from exc
    soname = corpus.get("soname", "")
    expected = re.escape(COMPONENT_LIBRARIES[component]) + r"\.so\.\d+(?:\.\d+)*"
    if corpus.tag != "abi-corpus" or re.fullmatch(expected, soname) is None:
        raise AbiGateError(
            f"snapshot does not describe {component}: {path}: {soname!r}"
        )
    if not corpus.findall(".//elf-symbol"):
        raise AbiGateError(f"snapshot has no ELF symbols: {path}")
    declarations = corpus.findall(".//function-decl[@elf-symbol-id]")
    declarations += corpus.findall(".//var-decl[@elf-symbol-id]")
    if not declarations:
        raise AbiGateError(f"snapshot has no exported DWARF declarations: {path}")
    if not corpus.findall(".//*[@filepath]"):
        raise AbiGateError(
            f"snapshot has no source locations; header filtering would hide changes: {path}"
        )
    return corpus


def _snapshot_owners(corpus: ET.Element) -> set[str]:
    symbols = [node.get("name", "") for node in corpus.findall(".//elf-symbol")]
    proc = subprocess.run(
        [tool_path("c++filt")],
        input="\n".join(symbols) + "\n",
        text=True,
        capture_output=True,
    )
    if proc.returncode:
        raise AbiGateError(f"cannot demangle ABI snapshot symbols: {proc.stderr}")
    return {
        match.group(1)
        for match in re.finditer(r"\bQCurl::([A-Za-z_][A-Za-z0-9_]*)", proc.stdout)
    }


def _validate_headers(path: Path, corpus: ET.Element) -> Path:
    headers = resolve_existing_dir(path, "public headers directory")
    files = [item for item in headers.rglob("*") if item.suffix in {".h", ".hpp"}]
    if not files:
        raise AbiGateError(f"public headers directory contains no headers: {headers}")
    try:
        source = "\n".join(item.read_text(encoding="utf-8") for item in files)
    except (OSError, UnicodeError) as exc:
        raise AbiGateError(f"cannot read public headers: {headers}: {exc}") from exc
    names = {item.name for item in files}
    locations = {
        Path(item.get("filepath", "")).name
        for item in corpus.findall(".//*[@filepath]")
    }
    if not names.intersection(locations):
        raise AbiGateError(
            f"public headers do not describe snapshot source locations: {headers}"
        )
    owners = _snapshot_owners(corpus)
    if not owners or not any(
        re.search(rf"\b{re.escape(owner)}\b", source) for owner in owners
    ):
        raise AbiGateError(
            f"public headers do not describe snapshot component: {headers}"
        )
    return headers


def snapshot_library(
    library: Path,
    headers_dir: Path,
    output: Path,
    *,
    component: str,
    surface_manifest: Path,
    source_root: Path,
    symbol_report: Path,
) -> dict:
    """捕获真实交付库的 ABI；缺调试信息或错误组件均失败。"""

    if component not in COMPONENT_LIBRARIES:
        raise AbiGateError(f"unknown ABI component: {component}")
    library = resolve_existing_file(library, "QCurl shared library")
    headers_dir = resolve_existing_dir(headers_dir, "public headers directory")
    output = output.resolve()
    symbol_report = symbol_report.resolve()
    if output == symbol_report:
        raise AbiGateError("ABI snapshot and symbol reports must be distinct")
    for path in (output, symbol_report):
        validate_snapshot_output(path, repo_root=REPO_ROOT)
        if path.exists():
            raise AbiGateError(f"ABI evidence output already exists: {path}")
    validate_library_dynamic_symbols(
        argparse.Namespace(
            surface_manifest=surface_manifest,
            source_root=source_root,
            component=component,
            symbol_report=symbol_report,
        ),
        library,
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        tool_path("abidw"),
        "--exported-interfaces-only",
        "--fail-no-debug-info",
        "--show-locs",
        "--no-corpus-path",
        "--no-comp-dir-path",
        "--headers-dir",
        str(headers_dir),
        "--out-file",
        str(output),
        str(library),
    ]
    run(command)
    _validate_headers(headers_dir, validate_component_snapshot(output, component))
    return {
        "component": component,
        "library": str(library),
        "snapshot": str(output),
        "headers": str(headers_dir),
        "symbolReport": str(symbol_report),
        "command": command,
    }


def abidiff_command(
    baseline_library: Path,
    current_library: Path,
    old_headers: Path,
    new_headers: Path,
    *,
    no_added_symbols: bool = False,
) -> list[str]:
    """构造独立旧、新头路径的 abidiff 调用，不加载默认 suppression。"""

    command = [
        tool_path("abidiff"),
        "--exported-interfaces-only",
        "--fail-no-debug-info",
        "--no-default-suppression",
        "--headers-dir1",
        str(old_headers),
        "--headers-dir2",
        str(new_headers),
    ]
    if no_added_symbols:
        command.append("--no-added-syms")
    return [*command, str(baseline_library), str(current_library)]


def run_abidiff_report(command: list[str], report: Path) -> dict:
    """保留原始输出；只有工具成功且无输出时写明无变化。"""

    try:
        proc = subprocess.run(command, text=True, capture_output=True)
    except OSError as exc:
        report.write_text(f"ABI tool execution failed: {exc}\n", encoding="utf-8")
        raise AbiGateError(f"ABI tool execution failed: {exc}") from exc
    details = (proc.stdout or "") + (proc.stderr or "")
    empty_error = proc.returncode != 0 and not details.strip()
    if not details.strip():
        details = (
            "No ABI changes detected (abidiff returncode=0).\n"
            if proc.returncode == 0
            else f"abidiff failed without output (returncode={proc.returncode}).\n"
        )
    report.write_text(details, encoding="utf-8")
    return {
        "command": command,
        "report": str(report),
        "returncode": proc.returncode,
        "emptyErrorOutput": empty_error,
    }


def _comparison_error(raw: dict, residual: dict) -> str | None:
    raw_code, residual_code = raw["returncode"], residual["returncode"]
    if raw["emptyErrorOutput"] or residual["emptyErrorOutput"]:
        return "abidiff returned a nonzero status with empty error output"
    if raw_code & ~ABIDIFF_CHANGE_MASK or residual_code & ~ABIDIFF_CHANGE_MASK:
        return f"abidiff tool/usage error or unknown return bits: {raw_code}/{residual_code}"
    if raw_code & ABIDIFF_ABI_INCOMPATIBLE_CHANGE:
        return f"abidiff identified an incompatible ABI change: {raw_code}"
    if residual_code != 0:
        return f"ABI changes remain after excluding added symbols: {residual_code}"
    return None


def _prepare_reports(reports: tuple[Path, Path, Path]) -> list[Path]:
    paths = [path.resolve() for path in reports]
    if len(set(paths)) != 3:
        raise AbiGateError("ABI raw, residual and result reports must be distinct")
    for path in paths:
        validate_snapshot_output(path, repo_root=REPO_ROOT)
        if path.exists():
            raise AbiGateError(f"ABI evidence output already exists: {path}")
        path.parent.mkdir(parents=True, exist_ok=True)
    return paths


def _validate_library_snapshot(library: Path, corpus: ET.Element) -> Path:
    path = resolve_existing_file(library, "retained ABI shared library")
    dynamic = run([tool_path("readelf"), "-d", str(path)]).stdout
    if f"Library soname: [{corpus.get('soname')}]" not in dynamic:
        raise AbiGateError(
            f"ABI snapshot SONAME does not match its shared library: {path}"
        )
    symbols = {
        line.split("|", 1)[0] for line in collect_dynamic_symbols(path).splitlines()
    }
    expected = {item.get("name", "") for item in corpus.findall(".//elf-symbol")}
    if symbols != expected:
        raise AbiGateError(
            f"ABI snapshot symbols do not match its shared library: {path}"
        )
    return path


def _run_comparisons(
    old_library: Path,
    new_library: Path,
    old_headers: Path,
    new_headers: Path,
    paths: list[Path],
) -> dict:
    result = {}
    for name, report, residual in (
        ("raw", paths[0], False),
        ("residual", paths[1], True),
    ):
        result[name] = run_abidiff_report(
            abidiff_command(
                old_library,
                new_library,
                old_headers,
                new_headers,
                no_added_symbols=residual,
            ),
            report,
        )
    return result


def compare_snapshots(
    baseline: Path,
    current: Path,
    old_headers_dir: Path,
    new_headers_dir: Path,
    *,
    component: str,
    baseline_library: Path,
    current_library: Path,
    raw_report: Path,
    residual_report: Path,
    result_report: Path,
) -> dict:
    """记录完整/残余分析，仅在残余无变化且原始无错误或破坏时通过。"""

    paths = _prepare_reports((raw_report, residual_report, result_report))
    result = {
        "schema": "qcurl-abi-comparison@v1",
        "component": component,
        "baseline": str(baseline.resolve()),
        "current": str(current.resolve()),
        "baselineLibrary": str(baseline_library.resolve()),
        "currentLibrary": str(current_library.resolve()),
        "oldHeaders": str(old_headers_dir.resolve()),
        "newHeaders": str(new_headers_dir.resolve()),
        "compatible": False,
    }
    try:
        old = validate_component_snapshot(baseline, component)
        new = validate_component_snapshot(current, component)
        old_headers = _validate_headers(old_headers_dir, old)
        new_headers = _validate_headers(new_headers_dir, new)
        old_library = _validate_library_snapshot(baseline_library, old)
        new_library = _validate_library_snapshot(current_library, new)
        result.update(
            _run_comparisons(old_library, new_library, old_headers, new_headers, paths)
        )
        error = _comparison_error(result["raw"], result["residual"])
        if error:
            raise AbiGateError(error)
        result["compatible"] = True
    except (AbiGateError, OSError, UnicodeError) as exc:
        result["error"] = str(exc)
        paths[2].write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        raise AbiGateError(str(exc)) from exc
    paths[2].write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return result
