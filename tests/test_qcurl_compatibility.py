from __future__ import annotations

import json
import subprocess
from pathlib import Path

import pytest

from tests.compatibility.fixture import make_reference_fixture
from scripts import qcurl_compatibility_check as checks
from scripts import qcurl_compatibility_consumers as consumers
from scripts.qcurl_compatibility import (
    check_reference,
    reference_file_entries,
    validate_reference,
    verify_capture,
)
from scripts.qcurl_compatibility_common import (
    CompatibilityError,
    file_entry,
    load_json,
    write_json,
)
from scripts.qcurl_compatibility_reference import INPUT_DIRECTORIES
from scripts.release_identity import file_sha256


@pytest.fixture
def reference(tmp_path: Path) -> tuple[Path, Path, Path]:
    return make_reference_fixture(tmp_path / "synthetic")


@pytest.fixture
def abi_pass(monkeypatch: pytest.MonkeyPatch) -> None:
    # Real ABI parsing/negative ELF controls live in test_qcurl_abi_compare.py;
    # these tests isolate the independent old-binary and input-integrity contract.
    monkeypatch.setattr(
        checks,
        "_compare_abis",
        lambda *args: {
            name: {
                "compatible": True,
                "raw": {"returncode": 0},
                "residual": {"returncode": 0},
            }
            for name in ("Core", "OtherExtras")
        },
    )


def _candidate_identity(head: str = "b" * 40) -> dict[str, object]:
    return {
        "head": head,
        "submodules": {"entries": [], "digest": "synthetic-tool-fixture"},
    }


def _refresh_package(build: Path, stage: Path) -> None:
    path = build / "evidence/package/shared-install-consumer.json"
    report = load_json(path)
    report["profile"]["sha256"] = file_sha256(
        build / "generated/consumer-profile.cmake"
    )
    installed = []
    for candidate in sorted(stage.rglob("*")):
        if candidate.is_file() or candidate.is_symlink():
            entry = file_entry(stage, candidate)
            entry.pop("executable", None)
            installed.append(entry)
    report["installInventory"] = {"files": installed}
    write_json(path, report)


def test_real_reference_inputs_cover_both_libraries_headers_and_four_old_binaries(
    reference: tuple[Path, Path, Path],
) -> None:
    root, build, stage = reference
    payload = validate_reference(root)
    assert set(payload["consumers"]) == {
        "Core",
        "BlockingExtras",
        "TestSupport",
        "OtherExtras",
    }
    entries = {entry["relativePath"]: entry for entry in reference_file_entries(root)}
    assert entries["reference.json"]["sha256"] == file_sha256(root / "reference.json")
    assert entries["bin/TestSupport"]["executable"] is True
    assert entries["install/lib/libQCurl.so.2.1"]["target"] == "libQCurl.so.2.1.0"
    assert verify_capture(root, build, stage)["version"] == "2.1.0"


@pytest.mark.parametrize(
    "relative",
    [
        "bin/Core",
        "install/include/qcurl/QCurlConfig.h",
        "abi/other-extras.snapshot.xml",
        "install/lib/libQCurlTestSupport.a",
    ],
)
def test_changed_reference_bytes_are_rejected(
    reference: tuple[Path, Path, Path], relative: str
) -> None:
    root, _, _ = reference
    with (root / relative).open("ab") as stream:
        stream.write(b"replaced-input")
    with pytest.raises(CompatibilityError, match="content changed"):
        validate_reference(root)


def test_removing_an_input_from_index_does_not_hide_it(
    reference: tuple[Path, Path, Path],
) -> None:
    root, _, _ = reference
    payload = load_json(root / "reference.json")
    payload["files"] = [
        entry for entry in payload["files"] if entry["path"] != "bin/OtherExtras"
    ]
    write_json(root / "reference.json", payload)
    with pytest.raises(CompatibilityError, match="inventory does not cover"):
        validate_reference(root)


def test_escaping_input_path_is_rejected(reference: tuple[Path, Path, Path]) -> None:
    root, _, _ = reference
    payload = load_json(root / "reference.json")
    payload["files"][0]["path"] = "../outside"
    write_json(root / "reference.json", payload)
    with pytest.raises(CompatibilityError, match="invalid reference input path"):
        validate_reference(root)


def test_check_runs_saved_elfs_without_any_build_command(
    reference: tuple[Path, Path, Path],
    abi_pass: None,
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
) -> None:
    root, build, stage = reference
    before = {path.name: file_sha256(path) for path in (root / "bin").iterdir()}
    original = subprocess.run
    commands = []

    def forbid_build(
        command: list[str], *args: object, **kwargs: object
    ) -> subprocess.CompletedProcess[str]:
        commands.append(command)
        assert Path(command[0]).name not in {
            "cmake",
            "g++",
            "c++",
            "clang++",
            "ninja",
            "make",
            "ld",
        }
        return original(command, *args, **kwargs)

    monkeypatch.setattr(subprocess, "run", forbid_build)
    report = check_reference(root, stage, tmp_path / "check", build_dir=build)
    assert report["result"] == "pass"
    assert report["classification"] == "REFERENCE_VALIDATED"
    assert report["patchCompatibility"] is False
    assert report["sourceRebuild"] == "not-run-separate-operation"
    assert {
        name: entry["binarySha256"] for name, entry in report["candidateRun"].items()
    } == before
    assert report["candidateRun"]["TestSupport"]["testSupportBridge"] is True
    for result in report["candidateRun"].values():
        assert (
            Path(result["loadedLibraries"]["libQCurl.so.2.1"])
            == (stage / "lib/libQCurl.so.2.1.0").resolve()
        )
        assert result["loaderSha256"] == file_sha256(Path(result["loaderLog"]))
    assert any(command[0] == str(root / "bin/TestSupport") for command in commands)


def test_patch_mode_rejects_self_reference(
    reference: tuple[Path, Path, Path], tmp_path: Path
) -> None:
    root, build, stage = reference
    with pytest.raises(CompatibilityError, match="own historical reference"):
        check_reference(
            root,
            stage,
            tmp_path / "check",
            build_dir=build,
            require_history=True,
            candidate_identity=_candidate_identity(),
        )
    assert load_json(tmp_path / "check/report.json")["result"] == "fail"


def test_existing_output_is_never_replaced(
    reference: tuple[Path, Path, Path], tmp_path: Path
) -> None:
    root, build, stage = reference
    output = tmp_path / "check"
    output.mkdir()
    marker = output / "kept.txt"
    marker.write_text("original", encoding="utf-8")
    with pytest.raises(CompatibilityError, match="already exists"):
        check_reference(root, stage, output, build_dir=build)
    assert marker.read_text(encoding="utf-8") == "original"


@pytest.mark.parametrize(
    "version,head,error",
    [
        ("2.2.0", "b" * 40, "different compatibility lines"),
        ("2.1.1", "a" * 40, "different source candidate"),
    ],
)
def test_wrong_line_or_same_candidate_is_rejected(
    reference: tuple[Path, Path, Path],
    tmp_path: Path,
    version: str,
    head: str,
    error: str,
) -> None:
    root, _, _ = reference
    _, build, stage = make_reference_fixture(
        tmp_path / "candidate", version=version, head=head
    )
    with pytest.raises(CompatibilityError, match=error):
        check_reference(
            root,
            stage,
            tmp_path / "check",
            build_dir=build,
            require_history=True,
            candidate_identity=_candidate_identity(head),
        )


def test_wrong_tuple_is_rejected_even_with_updated_package_digest(
    reference: tuple[Path, Path, Path], tmp_path: Path
) -> None:
    root, build, stage = reference
    profile = build / "generated/consumer-profile.cmake"
    profile.write_text(
        profile.read_text(encoding="utf-8").replace(
            "-O3 -DNDEBUG -g", "-O2 -DNDEBUG -g"
        ),
        encoding="utf-8",
    )
    _refresh_package(build, stage)
    with pytest.raises(CompatibilityError, match="different ABI tuples"):
        check_reference(
            root,
            stage,
            tmp_path / "check",
            build_dir=build,
            candidate_identity=_candidate_identity(),
        )


def test_loader_using_reference_libraries_cannot_pass_candidate_run(
    reference: tuple[Path, Path, Path],
    abi_pass: None,
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
) -> None:
    root, build, stage = reference
    original = consumers.library_environment
    monkeypatch.setattr(
        consumers,
        "library_environment",
        lambda selected, profile: original(root / "install", profile),
    )
    with pytest.raises(CompatibilityError, match="loaded wrong QCurl libraries"):
        check_reference(
            root,
            stage,
            tmp_path / "check",
            build_dir=build,
            candidate_identity=_candidate_identity(),
        )
    assert list((tmp_path / "check/logs/candidate").glob("*.loader.log"))


def test_missing_candidate_runtime_does_not_fall_back_to_host(
    reference: tuple[Path, Path, Path], tmp_path: Path
) -> None:
    root, build, stage = reference
    # Rename preserves the input for inspection and does not delete evidence.
    library = stage / "lib/libQCurlOtherExtras.so.2.1.0"
    library.rename(library.with_suffix(".missing"))
    with pytest.raises(CompatibilityError, match="runtime library missing"):
        check_reference(
            root,
            stage,
            tmp_path / "check",
            build_dir=build,
            candidate_identity=_candidate_identity(),
        )


def test_real_old_binary_rejects_wrong_candidate_behavior(
    reference: tuple[Path, Path, Path], abi_pass: None, tmp_path: Path
) -> None:
    root, _, _ = reference
    _, build, stage = make_reference_fixture(
        tmp_path / "candidate", version="2.1.1", head="b" * 40
    )
    source = tmp_path / "wrong-core.cpp"
    source.write_text(
        'extern "C" int core_contract() { return 99; }\n'
        "namespace QCurl { bool installNetworkMockProvider(const void *) noexcept { return true; } }\n",
        encoding="utf-8",
    )
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-O3",
            "-g",
            "-fPIC",
            "-shared",
            str(source),
            "-Wl,-soname,libQCurl.so.2.1",
            "-o",
            str(stage / "lib/libQCurl.so.2.1.1"),
        ],
        check=True,
    )
    _refresh_package(build, stage)
    with pytest.raises(CompatibilityError, match=r"command failed \(19\)"):
        check_reference(
            root,
            stage,
            tmp_path / "check",
            build_dir=build,
            require_history=True,
            candidate_identity=_candidate_identity(),
        )
    assert load_json(tmp_path / "check/report.json")["result"] == "fail"


def test_synthetic_patch_tool_control_does_not_rebuild_old_binary(
    reference: tuple[Path, Path, Path], abi_pass: None, tmp_path: Path
) -> None:
    root, _, _ = reference
    _, build, stage = make_reference_fixture(
        tmp_path / "candidate", version="2.1.1", head="b" * 40
    )
    report = check_reference(
        root,
        stage,
        tmp_path / "check",
        build_dir=build,
        require_history=True,
        candidate_identity=_candidate_identity(),
    )
    assert report["classification"] == "PATCH_COMPATIBLE"
    assert report["patchCompatibility"] is True
    assert set(report["candidateRun"]) == {
        "Core",
        "BlockingExtras",
        "TestSupport",
        "OtherExtras",
    }
    # This is explicitly a synthetic tool control, not an actual QCurl 2.1.1 release.
    assert validate_reference(root)["syntheticToolFixture"] is True


def test_dirty_capture_cannot_become_a_formal_patch_reference(
    reference: tuple[Path, Path, Path], tmp_path: Path
) -> None:
    root, _, _ = reference
    payload = load_json(root / "reference.json")
    payload["identity"]["tracked_patch"]["entries"] = [{"path": "dirty.cpp"}]
    write_json(root / "reference.json", payload)
    _, build, stage = make_reference_fixture(
        tmp_path / "candidate", version="2.1.1", head="b" * 40
    )
    with pytest.raises(
        CompatibilityError, match="historical reference source is not clean"
    ):
        check_reference(
            root,
            stage,
            tmp_path / "check",
            build_dir=build,
            require_history=True,
            candidate_identity=_candidate_identity(),
        )


def test_ambient_loader_injection_is_rejected(
    reference: tuple[Path, Path, Path], tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    root, build, stage = reference
    monkeypatch.setenv("LD_PRELOAD", "/tmp/not-a-compatibility-input.so")
    with pytest.raises(CompatibilityError, match="ambient LD_PRELOAD/LD_AUDIT"):
        check_reference(
            root,
            stage,
            tmp_path / "check",
            build_dir=build,
            candidate_identity=_candidate_identity(),
        )


def test_reference_validation_schema_cannot_be_forged_with_a_new_digest(
    reference: tuple[Path, Path, Path],
) -> None:
    from scripts.qcurl_compatibility_common import inventory

    root, _, _ = reference
    validation = load_json(root / "validation.json")
    validation["schema"] = "unrelated-result@v1"
    write_json(root / "validation.json", validation)
    payload = load_json(root / "reference.json")
    payload["files"] = inventory(root, INPUT_DIRECTORIES, ("validation.json",))
    write_json(root / "reference.json", payload)
    with pytest.raises(CompatibilityError, match="self-test evidence is incomplete"):
        validate_reference(root)
