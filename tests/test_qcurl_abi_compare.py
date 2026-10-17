from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path

import pytest

from scripts import qcurl_abi_compare as abi
from scripts import qcurl_abi_gate


def _run(command: list[str]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, text=True, capture_output=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return result


def _source(component: str, change: str) -> tuple[str, str]:
    macro = "QCURL_EXPORT" if component == "core" else "QCURL_OTHER_EXTRAS_EXPORT"
    fields = {"layout": "long first; int second;", "offset": "int second; int first;"}
    fields = fields.get(change, "int first; int second;")
    return_type = "double" if change == "return" else "Value"
    functions = "virtual int first() const; virtual int second() const;"
    if change == "virtual":
        functions = "virtual int second() const; virtual int first() const;"
    declaration = "" if change == "removed" else f"{macro} {return_type} value();"
    definition = (
        "" if change == "removed" else f"{return_type} value() {{ return {{}}; }}"
    )
    added_decl = f"{macro} int added();" if change == "added" else ""
    added_def = "int added() { return 7; }" if change == "added" else ""
    header = f"""#define {macro} __attribute__((visibility("default")))
namespace QCurl {{
struct {macro} Value {{ {fields} }};
{declaration}
class {macro} Interface {{ public: virtual ~Interface(); {functions} }};
{macro} Interface* create();
{added_decl}
}}
"""
    source = f"""#include "api.h"
namespace QCurl {{
{definition}
Interface::~Interface() {{}}
int Interface::first() const {{ return 1; }}
int Interface::second() const {{ return 2; }}
Interface* create() {{ return new Interface; }}
{added_def}
}}
"""
    if change.startswith("class-only"):
        methods = "virtual int first() const; virtual int second() const;"
        if change == "class-only-virtual":
            methods = "virtual int second() const; virtual int first() const;"
        header = f"""#define {macro} __attribute__((visibility("default")))
namespace QCurl {{ class {macro} Interface {{ public:
virtual ~Interface(); {methods}
}}; }}
"""
        source = """#include "api.h"
namespace QCurl {
Interface::~Interface() {}
int Interface::first() const { return 1; }
int Interface::second() const { return 2; }
}
"""
    return header, source


def _build(
    root: Path, component: str, change: str = "same", *, debug: bool = True
) -> dict:
    root.mkdir()
    header, source = _source(component, change)
    headers = root / "include" / "qcurl"
    headers.mkdir(parents=True)
    (headers / "api.h").write_text(header, encoding="utf-8")
    (root / "api.cpp").write_text(source, encoding="utf-8")
    library_name = abi.COMPONENT_LIBRARIES[component]
    library = root / f"{library_name}.so.2.1.0"
    command = [abi.tool_path("g++"), "-fPIC", "-shared", "-fvisibility=hidden"]
    if debug:
        command.append("-g")
    _run(
        [
            *command,
            "-I",
            str(headers),
            f"-Wl,-soname,{library_name}.so.2.1",
            "-o",
            str(library),
            str(root / "api.cpp"),
        ]
    )
    manifest = root / "surface.json"
    manifest.write_text(
        json.dumps(
            {
                "headers": [
                    {
                        "path": "api.h",
                        "visibility": "Public",
                        "component": "Core" if component == "core" else "OtherExtras",
                    }
                ]
            }
        ),
        encoding="utf-8",
    )
    return {
        "library": library,
        "headers": headers,
        "manifest": manifest,
        "snapshot": root / f"{component}.snapshot.xml",
        "component": component,
    }


def _snapshot(build: dict) -> Path:
    abi.snapshot_library(
        build["library"],
        build["headers"],
        build["snapshot"],
        component=build["component"],
        surface_manifest=build["manifest"],
        source_root=build["headers"],
        symbol_report=build["headers"] / "symbols.json",
    )
    return build["snapshot"]


def _compare(root: Path, old: dict, new: dict, **changes) -> dict:
    arguments = {
        "component": old["component"],
        "baseline_library": old["library"],
        "current_library": new["library"],
        "raw_report": root / "raw.txt",
        "residual_report": root / "residual.txt",
        "result_report": root / "comparison.json",
    }
    arguments.update(changes)
    return abi.compare_snapshots(
        old["snapshot"], new["snapshot"], old["headers"], new["headers"], **arguments
    )


@pytest.mark.parametrize("component", ["core", "other-extras"])
@pytest.mark.parametrize(
    "change", ["same", "added", "removed", "layout", "offset", "return", "virtual"]
)
def test_real_elf_conservative_abi_controls(
    tmp_path: Path, component: str, change: str
) -> None:
    old = _build(tmp_path / "old", component)
    new = _build(tmp_path / "new", component, change)
    _snapshot(old)
    _snapshot(new)

    if change in {"same", "added"}:
        result = _compare(tmp_path, old, new)
        assert result["compatible"] is True
        assert result["residual"]["returncode"] == 0
        assert result["raw"]["returncode"] == (4 if change == "added" else 0)
        if change == "added":
            assert "added" in (tmp_path / "raw.txt").read_text()
    else:
        with pytest.raises(abi.AbiGateError, match="incompatible|remain"):
            _compare(tmp_path, old, new)
        result = json.loads((tmp_path / "comparison.json").read_text())
        assert result["compatible"] is False
        assert result["residual"]["returncode"] in {4, 12}
        if change in {"layout", "offset", "return"}:
            assert result["raw"]["returncode"] in {4, 12}
    assert (tmp_path / "raw.txt").read_text().strip()
    assert (tmp_path / "residual.txt").read_text().strip()
    for side, build in (("--headers-dir1", old), ("--headers-dir2", new)):
        command = result["raw"]["command"]
        assert command[command.index(side) + 1] == str(build["headers"])
    assert "--no-added-syms" not in result["raw"]["command"]
    assert "--no-added-syms" in result["residual"]["command"]


@pytest.mark.parametrize("returncode", [1, 2, 3, 4, 5, 8, 12, 16, 20, -1])
def test_abidiff_errors_and_unknown_bits_fail_closed(
    tmp_path: Path, monkeypatch, returncode: int
) -> None:
    old = _build(tmp_path / "old", "core")
    new = _build(tmp_path / "new", "core")
    _snapshot(old)
    _snapshot(new)
    original = abi.subprocess.run

    def run(command, **kwargs):
        if Path(command[0]).name == "abidiff":
            return subprocess.CompletedProcess(
                command, returncode, "controlled abidiff failure\n", ""
            )
        return original(command, **kwargs)

    monkeypatch.setattr(abi.subprocess, "run", run)
    with pytest.raises(abi.AbiGateError):
        _compare(tmp_path, old, new)
    result = json.loads((tmp_path / "comparison.json").read_text())
    assert result["compatible"] is False
    assert result["raw"]["returncode"] == returncode
    assert "controlled abidiff failure" in (tmp_path / "raw.txt").read_text()


def test_real_elf_without_dwarf_is_rejected(tmp_path: Path) -> None:
    build = _build(tmp_path / "stripped", "core", debug=False)
    with pytest.raises(abi.AbiGateError, match="debug info|debug information"):
        _snapshot(build)


def test_component_mismatch_and_wrong_old_headers_are_rejected(tmp_path: Path) -> None:
    old = _build(tmp_path / "old", "core")
    new = _build(tmp_path / "new", "core")
    _snapshot(old)
    _snapshot(new)
    with pytest.raises(abi.AbiGateError, match="does not describe other-extras"):
        _compare(tmp_path, old, new, component="other-extras")
    wrong_headers = tmp_path / "wrong"
    wrong_headers.mkdir()
    (wrong_headers / "unrelated.h").write_text(
        "struct Unrelated {};\n", encoding="utf-8"
    )
    with pytest.raises(abi.AbiGateError, match="headers do not describe"):
        abi.compare_snapshots(
            old["snapshot"],
            new["snapshot"],
            wrong_headers,
            new["headers"],
            component="core",
            baseline_library=old["library"],
            current_library=new["library"],
            raw_report=tmp_path / "wrong-raw.txt",
            residual_report=tmp_path / "wrong-residual.txt",
            result_report=tmp_path / "wrong-result.json",
        )


def test_missing_tool_and_nonzero_empty_output_remain_errors(
    tmp_path: Path, monkeypatch
) -> None:
    old = _build(tmp_path / "old", "core")
    new = _build(tmp_path / "new", "core")
    _snapshot(old)
    _snapshot(new)
    original = shutil.which
    monkeypatch.setattr(
        shutil, "which", lambda name: None if name == "abidiff" else original(name)
    )
    with pytest.raises(abi.AbiGateError, match="required ABI tool not found: abidiff"):
        _compare(tmp_path, old, new)
    assert json.loads((tmp_path / "comparison.json").read_text())["compatible"] is False
    monkeypatch.setattr(shutil, "which", original)
    monkeypatch.setattr(
        abi.subprocess,
        "run",
        lambda *a, **k: subprocess.CompletedProcess([], 4, "", ""),
    )
    report = abi.run_abidiff_report(["abidiff"], tmp_path / "empty.txt")
    assert report["emptyErrorOutput"] is True
    assert "failed without output" in (tmp_path / "empty.txt").read_text()


def test_invalid_snapshot_and_existing_evidence_are_rejected(tmp_path: Path) -> None:
    invalid = tmp_path / "invalid.xml"
    invalid.write_text("not XML", encoding="utf-8")
    with pytest.raises(abi.AbiGateError, match="invalid ABI snapshot"):
        abi.validate_component_snapshot(invalid, "core")
    build = _build(tmp_path / "old", "core")
    _snapshot(build)
    before = build["snapshot"].read_bytes()
    with pytest.raises(abi.AbiGateError, match="already exists"):
        _snapshot(build)
    assert build["snapshot"].read_bytes() == before


def test_snapshot_without_source_locations_cannot_hide_header_filtered_changes(
    tmp_path: Path,
) -> None:
    import xml.etree.ElementTree as ET

    build = _build(tmp_path / "old", "core")
    _snapshot(build)
    tree = ET.parse(build["snapshot"])
    for node in tree.iter():
        node.attrib.pop("filepath", None)
    invalid = tmp_path / "no-locations.xml"
    tree.write(invalid, encoding="utf-8")

    with pytest.raises(abi.AbiGateError, match="no source locations"):
        abi.validate_component_snapshot(invalid, "core")


@pytest.mark.parametrize("change", ["class-only", "class-only-virtual"])
def test_class_only_dwarf_is_compared_without_translation_unit_container(
    tmp_path: Path,
    change: str,
) -> None:
    import xml.etree.ElementTree as ET

    old = _build(tmp_path / "old", "other-extras", "class-only")
    new = _build(tmp_path / "new", "other-extras", change)
    _snapshot(old)
    _snapshot(new)
    corpus = ET.parse(old["snapshot"]).getroot()
    assert corpus.findall(".//function-decl[@elf-symbol-id]")
    # libabigail 3 may place all member declarations in abi-types, without abi-instr.
    if change == "class-only":
        assert _compare(tmp_path, old, new)["compatible"] is True
    else:
        with pytest.raises(abi.AbiGateError, match="incompatible"):
            _compare(tmp_path, old, new)


def test_obsolete_cli_entries_and_implicit_reference_are_rejected() -> None:
    for command in (
        ["baseline"],
        ["promote"],
        ["diff"],
        ["diff", "--baseline", "reference.xml"],
    ):
        with pytest.raises(SystemExit) as caught:
            qcurl_abi_gate.build_parser().parse_args(command)
        assert caught.value.code == 2


def test_real_elf_cli_diff_preserves_addition_reports(tmp_path: Path) -> None:
    old = _build(tmp_path / "old", "other-extras")
    new = _build(tmp_path / "new", "other-extras", "added")
    _snapshot(old)
    args = [
        "--component",
        "other-extras",
        "--library",
        str(new["library"]),
        "--headers-dir",
        str(new["headers"]),
        "--source-root",
        str(new["headers"]),
        "--surface-manifest",
        str(new["manifest"]),
        "--symbol-report",
        str(tmp_path / "symbols.json"),
        "diff",
        "--baseline",
        str(old["snapshot"]),
        "--baseline-library",
        str(old["library"]),
        "--old-headers-dir",
        str(old["headers"]),
        "--current-snapshot",
        str(new["snapshot"]),
        "--report",
        str(tmp_path / "raw.txt"),
        "--residual-report",
        str(tmp_path / "residual.txt"),
        "--result-report",
        str(tmp_path / "comparison.json"),
    ]

    assert qcurl_abi_gate.main(args) == 0
    assert json.loads((tmp_path / "comparison.json").read_text())["compatible"] is True
    before = (tmp_path / "comparison.json").read_bytes()
    assert qcurl_abi_gate.main(args) == 2
    assert (tmp_path / "comparison.json").read_bytes() == before
