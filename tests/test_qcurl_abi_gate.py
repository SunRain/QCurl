from __future__ import annotations

import json
import shutil
import subprocess
from copy import deepcopy
from pathlib import Path

import pytest

from scripts import qcurl_abi_gate
from scripts import release_identity
from scripts import run_release_gate


def _six_tree_args(tmp_path: Path) -> list[str]:
    return [
        "--release-shared-build-dir",
        str(tmp_path / "release-shared"),
        "--release-static-build-dir",
        str(tmp_path / "release-static"),
        "--test-shared-gcc-build-dir",
        str(tmp_path / "test-shared-gcc"),
        "--test-shared-clang-build-dir",
        str(tmp_path / "test-shared-clang"),
        "--asan-ubsan-lsan-build-dir",
        str(tmp_path / "asan-ubsan-lsan"),
        "--tsan-build-dir",
        str(tmp_path / "tsan"),
    ]


def _write_six_tree_caches(tmp_path: Path) -> None:
    specs = {
        "release-shared": ("OFF", "ON", "/usr/bin/g++", ""),
        "release-static": ("OFF", "OFF", "/usr/bin/g++", ""),
        "test-shared-gcc": ("ON", "ON", "/usr/bin/g++", ""),
        "test-shared-clang": ("ON", "ON", "/usr/bin/clang++", ""),
        "asan-ubsan-lsan": (
            "ON",
            "ON",
            "/usr/bin/clang++",
            "asan-ubsan-lsan",
        ),
        "tsan": ("ON", "ON", "/usr/bin/clang++", "tsan"),
    }
    for tree_id, (testing, shared, compiler, sanitizer) in specs.items():
        build_dir = tmp_path / tree_id
        build_dir.mkdir(parents=True)
        flags = f"-fsanitize={sanitizer}" if sanitizer else ""
        (build_dir / "CMakeCache.txt").write_text(
            "\n".join(
                (
                    f"BUILD_TESTING:BOOL={testing}",
                    f"QCURL_BUILD_SHARED_LIBS:BOOL={shared}",
                    f"CMAKE_CXX_COMPILER:FILEPATH={compiler}",
                    f"QCURL_SANITIZER_PROFILE:STRING={sanitizer}",
                    f"CMAKE_CXX_FLAGS:STRING={flags}",
                )
            )
            + "\n",
            encoding="utf-8",
        )


def test_shared_targets_use_target_wide_hidden_visibility() -> None:
    source = (Path(__file__).resolve().parents[1] / "src" / "CMakeLists.txt").read_text(
        encoding="utf-8"
    )

    assert "foreach(_qcurl_shared_target IN ITEMS QCurl QCurlOtherExtras)" in source
    assert "CXX_VISIBILITY_PRESET hidden" in source
    assert "VISIBILITY_INLINES_HIDDEN YES" in source
    assert "set_source_files_properties(" not in source


def test_static_export_macros_do_not_depend_on_visibility() -> None:
    source = (Path(__file__).resolve().parents[1] / "src" / "QCGlobal.h").read_text(
        encoding="utf-8"
    )

    assert "#if defined(QCURL_STATIC_DEFINE)\n#define QCURL_EXPORT" in source
    assert (
        "#if defined(QCURL_OTHER_EXTRAS_STATIC_DEFINE)\n"
        "#define QCURL_OTHER_EXTRAS_EXPORT" in source
    )


def test_full_release_gate_runs_symbol_allowlists_before_abi_diff(
    tmp_path: Path,
    capsys,
) -> None:
    _write_six_tree_caches(tmp_path)
    assert run_release_gate.main(
        [
            "--tier",
            "full",
            *_six_tree_args(tmp_path),
            "--dry-run",
        ]
    ) == 0

    names = [item["name"] for item in json.loads(capsys.readouterr().out)["steps"]]
    assert names.index("dynamic_symbol_allowlist") < names.index("abi_current_baseline_diff")
    assert names.index("other_extras_dynamic_symbol_allowlist") < names.index(
        "abi_current_baseline_diff"
    )

def test_dynamic_symbol_allowlist_rejects_unlisted_first_party_owners() -> None:
    symbols = (
        "_ZN5QCurl18QCNetworkRequestC1Ev|QCurl::QCNetworkRequest::QCNetworkRequest()\n"
        "_ZN5QCurl14jitterFractionEv|QCurl::jitterFraction()\n"
        "_ZN5QCurl21QCNetworkReplyPrivate14loggerSnapshotEPKNS_14QCNetworkReplyE|"
        "QCurl::QCNetworkReplyPrivate::loggerSnapshot(QCurl::QCNetworkReply const*)\n"
        "_ZN5QCurl25QCCurlMultiTransferRecord6handleEv|"
        "QCurl::QCCurlMultiTransferRecord::handle() const\n"
        "_ZN5QCurl18QCCurlMultiManager11addTransferEONS_19QCCurlHandleManagerE|"
        "QCurl::QCCurlMultiManager::addTransfer(QCurl::QCCurlHandleManager&&)\n"
    )

    with pytest.raises(qcurl_abi_gate.AbiGateError) as exc_info:
        qcurl_abi_gate.validate_dynamic_symbol_contract(
            symbols,
            allowed_owners={"QCNetworkRequest"},
        )

    message = str(exc_info.value)
    assert "jitterFraction" in message
    assert "QCNetworkReplyPrivate" in message
    assert "QCCurlMultiTransferRecord" in message
    assert "QCCurlHandleManager" in message


def test_dynamic_symbol_allowlist_accepts_public_owner_and_non_first_party_symbols() -> None:
    symbols = (
        "_ZN5QCurl18QCNetworkRequestC1Ev|QCurl::QCNetworkRequest::QCNetworkRequest()\n"
        "_ZN10QByteArrayD1Ev|QByteArray::~QByteArray()\n"
    )

    qcurl_abi_gate.validate_dynamic_symbol_contract(
        symbols,
        allowed_owners={"QCNetworkRequest"},
    )


def test_baseline_default_is_build_snapshot_not_tracked_baseline() -> None:
    args = qcurl_abi_gate.build_parser().parse_args(["baseline"])

    assert args.output == Path("build/abi/qcurl-core-v2.candidate.abi.xml")
    assert not qcurl_abi_gate.path_is_controlled_baseline(args.output)


def test_baseline_command_rejects_controlled_baseline_output(tmp_path: Path) -> None:
    repo = tmp_path / "repo"
    controlled = repo / "abi" / "baseline" / "qcurl-core-v1.abi.xml"

    with pytest.raises(qcurl_abi_gate.AbiGateError, match="promote"):
        qcurl_abi_gate.validate_snapshot_output(controlled, repo_root=repo)


def test_promotion_manifest_requires_clean_recorded_candidate_and_required_gates(
    tmp_path: Path,
) -> None:
    candidate = "a" * 40
    manifest = {
        "schema": "qa-manifest@v1",
        "identity": {
            "head": candidate,
            "tracked_patch": {"entries": []},
            "untracked": {"entries": []},
            "submodules": {"entries": []},
            "toolchain": {
                "cmake": "cmake 4.0",
                "compilers": {"c++": "c++ 14.1"},
                "libabigail": {"abidiff": "abidiff 2.6", "abidw": "abidw 2.6"},
            },
            "platform": {"system": "Linux", "machine": "x86_64"},
        },
        "gates": {
            "required": [
                "shared_package_evidence",
                "static_package_evidence",
                "dynamic_symbol_allowlist",
                "other_extras_dynamic_symbol_allowlist",
                "abi_hardbreak_report",
            ],
            "results": {
                name: {"result": "pass", "returncode": 0}
                for name in (
                    "shared_package_evidence",
                    "static_package_evidence",
                    "dynamic_symbol_allowlist",
                    "other_extras_dynamic_symbol_allowlist",
                    "abi_hardbreak_report",
                )
            },
        },
    }
    path = tmp_path / "candidate.json"
    path.write_text(json.dumps(manifest), encoding="utf-8")

    loaded = qcurl_abi_gate.load_promotion_manifest(path, candidate_commit=candidate)

    assert loaded["identity"]["head"] == candidate


@pytest.mark.parametrize(
    ("mutation", "expected"),
    [
        (lambda manifest: manifest["identity"]["tracked_patch"]["entries"].append({"path": "x"}), "clean"),
        (lambda manifest: manifest["identity"].update(head="short"), "full commit"),
        (
            lambda manifest: manifest["gates"]["results"].pop("dynamic_symbol_allowlist"),
            "dynamic_symbol_allowlist",
        ),
        (lambda manifest: manifest["identity"]["platform"].update(system="Darwin"), "Linux"),
    ],
)
def test_promotion_manifest_fails_closed(
    tmp_path: Path,
    mutation,
    expected: str,
) -> None:
    candidate = "b" * 40
    required = {
        name: {"result": "pass", "returncode": 0}
        for name in (
            "shared_package_evidence",
            "static_package_evidence",
            "dynamic_symbol_allowlist",
            "other_extras_dynamic_symbol_allowlist",
            "abi_hardbreak_report",
        )
    }
    manifest = {
        "schema": "qa-manifest@v1",
        "identity": {
            "head": candidate,
            "tracked_patch": {"entries": []},
            "untracked": {"entries": []},
            "submodules": {"entries": []},
            "toolchain": {
                "cmake": "cmake 4.0",
                "compilers": {"c++": "c++ 14.1"},
                "libabigail": {"abidiff": "abidiff 2.6", "abidw": "abidw 2.6"},
            },
            "platform": {"system": "Linux", "machine": "x86_64"},
        },
        "gates": {"required": list(required), "results": required},
    }
    mutation(manifest)
    path = tmp_path / "candidate.json"
    path.write_text(json.dumps(manifest), encoding="utf-8")

    with pytest.raises(qcurl_abi_gate.AbiGateError, match=expected):
        qcurl_abi_gate.load_promotion_manifest(path, candidate_commit=candidate)


def test_hardbreak_report_is_nonempty_when_abidiff_reports_no_change(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(
        qcurl_abi_gate.subprocess,
        "run",
        lambda *_args, **_kwargs: subprocess.CompletedProcess([], 0, "", ""),
    )
    report = tmp_path / "old-to-new.txt"

    qcurl_abi_gate._run_abidiff_hardbreak_report(["abidiff"], report)

    assert report.read_text(encoding="utf-8").strip() == (
        "No ABI changes detected (abidiff returncode=0)."
    )


def test_promotion_rejects_forged_manifest_that_cannot_replay_identity(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    repo = Path(__file__).resolve().parents[1]
    manifest_path = tmp_path / "candidate.json"
    manifest = {
        "schema": "qa-manifest@v1",
        "repo_root": str(repo),
        "identity": {
            "capabilities": {"build_dirs": [str(tmp_path / "build")]},
            "authority": {"entries": []},
            "commands": [["true"]],
        },
        "result": "pass",
        "validation": {"valid": True},
        "run_guard": {"identity_stable": True},
        "gates": {
            "required": list(qcurl_abi_gate.PROMOTION_REQUIRED_GATES),
            "results": {
                name: {"result": "pass", "returncode": 0, "command": ["true"]}
                for name in qcurl_abi_gate.PROMOTION_REQUIRED_GATES
            },
        },
        "artifacts": {
            "qa_manifest": {"path": str(manifest_path), "required": True},
        },
    }
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
    monkeypatch.setattr(
        release_identity,
        "verify_snapshot",
        lambda *_args, **_kwargs: release_identity.SnapshotCheck(
            False, "blocked", ("identity digest mismatch",), None
        ),
    )

    with pytest.raises(qcurl_abi_gate.AbiGateError, match="identity digest mismatch"):
        qcurl_abi_gate.verify_promotion_manifest(manifest, manifest_path=manifest_path)


def _promotion_input_manifest(
    tmp_path: Path,
) -> tuple[dict[str, object], Path, dict[str, Path]]:
    repo = Path(__file__).resolve().parents[1]
    input_paths = {
        "candidate_core_library": tmp_path / "candidate" / "libQCurl.so.2.0.0",
        "v1_baseline": tmp_path / "candidate" / "qcurl-core-v1.abi.xml",
        "abi_hardbreak_current_snapshot": (
            tmp_path / "candidate" / "qcurl-core-v2.candidate.abi.xml"
        ),
        "abi_hardbreak_report": tmp_path / "candidate" / "qcurl-core-v1-to-v2.txt",
        "core_dynamic_symbols": tmp_path / "candidate" / "core-symbols.json",
        "other_extras_dynamic_symbols": tmp_path / "candidate" / "extras-symbols.json",
    }
    for artifact_id, path in input_paths.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"{artifact_id}\n", encoding="utf-8")

    required_gates = list(qcurl_abi_gate.PROMOTION_REQUIRED_GATES)
    commands = [["gate", name] for name in required_gates]
    manifest_path = tmp_path / "candidate" / "promotion-candidate-manifest.json"
    manifest: dict[str, object] = {
        "schema": "qa-manifest@v1",
        "repo_root": str(repo),
        "identity": {
            "capabilities": {"build_dirs": [str(tmp_path / "build")]},
            "authority": {"entries": []},
            "commands": commands,
        },
        "result": "pass",
        "validation": {"valid": True},
        "run_guard": {"identity_stable": True},
        "gates": {
            "required": required_gates,
            "results": {
                name: {"result": "pass", "returncode": 0, "command": command}
                for name, command in zip(required_gates, commands)
            },
        },
        "artifacts": {
            artifact_id: {
                "path": str(path),
                "required": True,
                "kind": "promotion-input" if artifact_id in {
                    "candidate_core_library",
                    "v1_baseline",
                    "abi_hardbreak_current_snapshot",
                    "abi_hardbreak_report",
                } else "gate-output",
                "sha256": release_identity.file_sha256(path),
            }
            for artifact_id, path in input_paths.items()
        },
        "promotion": {"manifest_path": str(manifest_path)},
    }
    manifest[release_identity.MANIFEST_PAYLOAD_DIGEST_FIELD] = (
        release_identity.manifest_payload_digest(manifest)
    )
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return manifest, manifest_path, input_paths


def test_promotion_rejects_each_manifest_input_substitution(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    manifest, manifest_path, input_paths = _promotion_input_manifest(tmp_path)
    monkeypatch.setattr(
        release_identity,
        "verify_snapshot",
        lambda *_args, **_kwargs: release_identity.SnapshotCheck(
            True, "pass", (), {}
        ),
    )
    bound = {
        "library": input_paths["candidate_core_library"],
        "old_baseline": input_paths["v1_baseline"],
        "candidate_snapshot": input_paths["abi_hardbreak_current_snapshot"],
        "old_to_new_report": input_paths["abi_hardbreak_report"],
    }

    for name in bound:
        substituted = tmp_path / f"substituted-{name}"
        substituted.write_text("substitution\n", encoding="utf-8")
        inputs = dict(bound)
        inputs[name] = substituted
        with pytest.raises(
            qcurl_abi_gate.AbiGateError, match="promotion input|candidate manifest"
        ):
            qcurl_abi_gate.verify_promotion_manifest(
                manifest,
                manifest_path=manifest_path,
                **inputs,
            )

    substituted_manifest = tmp_path / "substituted-manifest.json"
    substituted_manifest.write_text("substitution\n", encoding="utf-8")
    with pytest.raises(
        qcurl_abi_gate.AbiGateError, match="promotion input|candidate manifest"
    ):
        qcurl_abi_gate.verify_promotion_manifest(
            manifest,
            manifest_path=substituted_manifest,
            **bound,
        )

    tampered = deepcopy(manifest)
    tampered[release_identity.MANIFEST_PAYLOAD_DIGEST_FIELD] = "0" * 64
    with pytest.raises(
        qcurl_abi_gate.AbiGateError, match="promotion input|payload digest"
    ):
        qcurl_abi_gate.verify_promotion_manifest(
            tampered,
            manifest_path=manifest_path,
            **bound,
        )


def test_promotion_accepts_manifest_bound_inputs(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    manifest, manifest_path, input_paths = _promotion_input_manifest(tmp_path)
    monkeypatch.setattr(
        release_identity,
        "verify_snapshot",
        lambda *_args, **_kwargs: release_identity.SnapshotCheck(
            True, "pass", (), {}
        ),
    )

    qcurl_abi_gate.verify_promotion_manifest(
        manifest,
        manifest_path=manifest_path,
        library=input_paths["candidate_core_library"],
        old_baseline=input_paths["v1_baseline"],
        candidate_snapshot=input_paths["abi_hardbreak_current_snapshot"],
        old_to_new_report=input_paths["abi_hardbreak_report"],
    )


def test_promotion_is_copy_only_after_manifest_verification(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    candidate = "c" * 40
    manifest, manifest_path, input_paths = _promotion_input_manifest(tmp_path)

    monkeypatch.setattr(
        qcurl_abi_gate,
        "load_promotion_manifest",
        lambda *_args, **_kwargs: manifest,
    )
    monkeypatch.setattr(
        qcurl_abi_gate,
        "verify_promotion_manifest",
        lambda *_args, **_kwargs: None,
    )
    monkeypatch.setattr(
        qcurl_abi_gate,
        "_abidw_command",
        lambda *_args, **_kwargs: pytest.fail("promotion must not invoke abidw"),
    )
    monkeypatch.setattr(
        qcurl_abi_gate,
        "_run_abidiff_hardbreak_report",
        lambda *_args, **_kwargs: pytest.fail("promotion must not invoke abidiff"),
    )

    def fake_run(command: list[str], **_kwargs) -> subprocess.CompletedProcess[str]:
        if command[-2:] == ["rev-parse", "HEAD"]:
            return subprocess.CompletedProcess(command, 0, candidate + "\n", "")
        if command[-2:] == ["--untracked-files=all"]:
            return subprocess.CompletedProcess(command, 0, "", "")
        return subprocess.CompletedProcess(command, 0, "", "")

    monkeypatch.setattr(qcurl_abi_gate, "_run", fake_run)
    copied: list[tuple[Path, Path, str]] = []
    monkeypatch.setattr(
        shutil,
        "copyfile",
        lambda *_args, **_kwargs: pytest.fail("promotion must use atomic copy helper"),
    )
    monkeypatch.setattr(
        qcurl_abi_gate,
        "atomic_copy_verified_snapshot",
        lambda source, target, digest, **_kwargs: copied.append(
            (Path(source), Path(target), digest)
        ),
    )
    args = qcurl_abi_gate.build_parser().parse_args(
        [
            "--library",
            str(input_paths["candidate_core_library"]),
            "promote",
            "--candidate-manifest",
            str(manifest_path),
            "--candidate-commit",
            candidate,
            "--old-baseline",
            str(input_paths["v1_baseline"]),
            "--candidate-snapshot",
            str(input_paths["abi_hardbreak_current_snapshot"]),
            "--old-to-new-report",
            str(input_paths["abi_hardbreak_report"]),
        ]
    )

    qcurl_abi_gate.command_promote(args)

    assert copied
    assert copied[0][0] == input_paths["abi_hardbreak_current_snapshot"].resolve()
    assert copied[0][2] == release_identity.file_sha256(copied[0][0])


@pytest.mark.parametrize("drift_target", ["snapshot", "manifest"])
def test_promotion_rejects_inputs_changed_after_verification(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    drift_target: str,
) -> None:
    candidate = "d" * 40
    manifest, manifest_path, input_paths = _promotion_input_manifest(tmp_path)
    monkeypatch.setattr(
        qcurl_abi_gate,
        "load_promotion_manifest",
        lambda *_args, **_kwargs: manifest,
    )

    def verify_then_drift(*_args, **_kwargs) -> None:
        path = (
            input_paths["abi_hardbreak_current_snapshot"]
            if drift_target == "snapshot"
            else manifest_path
        )
        path.write_text("changed after verification\n", encoding="utf-8")

    monkeypatch.setattr(qcurl_abi_gate, "verify_promotion_manifest", verify_then_drift)

    def fake_run(command: list[str], **_kwargs) -> subprocess.CompletedProcess[str]:
        if command[-2:] == ["rev-parse", "HEAD"]:
            return subprocess.CompletedProcess(command, 0, candidate + "\n", "")
        return subprocess.CompletedProcess(command, 0, "", "")

    monkeypatch.setattr(qcurl_abi_gate, "_run", fake_run)
    monkeypatch.setattr(
        qcurl_abi_gate,
        "atomic_copy_verified_snapshot",
        lambda *_args, **_kwargs: pytest.fail("drift must be rejected before copy"),
        raising=False,
    )
    args = qcurl_abi_gate.build_parser().parse_args(
        [
            "--library",
            str(input_paths["candidate_core_library"]),
            "promote",
            "--candidate-manifest",
            str(manifest_path),
            "--candidate-commit",
            candidate,
            "--old-baseline",
            str(input_paths["v1_baseline"]),
            "--candidate-snapshot",
            str(input_paths["abi_hardbreak_current_snapshot"]),
            "--old-to-new-report",
            str(input_paths["abi_hardbreak_report"]),
        ]
    )

    with pytest.raises(qcurl_abi_gate.AbiGateError, match="changed after verification"):
        qcurl_abi_gate.command_promote(args)


def test_atomic_promotion_copy_failure_preserves_existing_target(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    source = tmp_path / "candidate.abi.xml"
    target = tmp_path / "baseline.abi.xml"
    source.write_bytes(b"candidate\n")
    target.write_bytes(b"existing\n")

    def fail_copy(source_stream, target_stream, *_args, **_kwargs) -> None:
        target_stream.write(source_stream.read(3))
        raise OSError("injected copy failure")

    monkeypatch.setattr(shutil, "copyfileobj", fail_copy)

    with pytest.raises(qcurl_abi_gate.AbiGateError, match="copy"):
        qcurl_abi_gate.atomic_copy_verified_snapshot(
            source,
            target,
            release_identity.file_sha256(source),
        )

    assert target.read_bytes() == b"existing\n"


def test_atomic_promotion_rechecks_target_digest(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    source = tmp_path / "candidate.abi.xml"
    target = tmp_path / "baseline.abi.xml"
    source.write_bytes(b"candidate\n")
    expected_digest = release_identity.file_sha256(source)
    real_file_sha256 = release_identity.file_sha256

    def mismatched_target_digest(path: Path) -> str:
        if Path(path).resolve() == target.resolve():
            return "0" * 64
        return real_file_sha256(path)

    monkeypatch.setattr(release_identity, "file_sha256", mismatched_target_digest)

    with pytest.raises(qcurl_abi_gate.AbiGateError, match="target digest mismatch"):
        qcurl_abi_gate.atomic_copy_verified_snapshot(source, target, expected_digest)


def test_full_release_manifest_requires_symbol_and_abi_artifacts(tmp_path: Path) -> None:
    args = run_release_gate.build_parser().parse_args(
        [
            "--tier",
            "full",
            *_six_tree_args(tmp_path),
        ]
    )
    steps = run_release_gate._selected_steps(args)

    artifacts = dict(run_release_gate._required_artifacts(args, steps))

    assert artifacts["core_dynamic_symbols"].name == "qcurl-core-v2.dynamic-symbols.json"
    assert artifacts["other_extras_dynamic_symbols"].name == (
        "qcurl-other-extras-v2.dynamic-symbols.json"
    )
    assert artifacts["abi_current_report"].name == "qcurl-core-v2.abidiff.txt"
    assert artifacts["abi_current_snapshot"].name == "qcurl-core-v2.current.abi.xml"


def test_core_dynamic_symbols_exclude_multi_manager_owner() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    library = repo_root / "build-test-shared-gcc" / "src" / "libQCurl.so.2.0.0"

    symbols = qcurl_abi_gate._collect_dynamic_symbols(library)
    leaked_symbols = [line for line in symbols.splitlines() if "QCCurlMultiManager" in line]

    assert not leaked_symbols, "Core 动态 ABI 泄漏 QCCurlMultiManager：\n" + "\n".join(
        leaked_symbols
    )


def test_other_extras_opaque_bridge_consumer() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    library = (
        repo_root / "build-test-shared-gcc" / "src" / "libQCurlOtherExtras.so.2.0.0"
    )

    symbols = subprocess.run(
        ["nm", "-D", "--undefined-only", "--format=posix", str(library)],
        check=False,
        capture_output=True,
        text=True,
    )
    assert symbols.returncode == 0, symbols.stderr

    mangled_names = [line.split(None, 1)[0] for line in symbols.stdout.splitlines() if line.strip()]
    demangled = subprocess.run(
        ["c++filt"],
        input="\n".join(mangled_names) + "\n",
        check=False,
        capture_output=True,
        text=True,
    )
    assert demangled.returncode == 0, demangled.stderr

    bridge_symbols = demangled.stdout.splitlines()
    assert any("QCurl::registerPersistentTransfer(" in line for line in bridge_symbols)
    assert any("QCurl::removePersistentTransfer(" in line for line in bridge_symbols)
    assert not any("QCCurlMultiManager" in line for line in bridge_symbols), (
        "Other Extras 直接依赖 QCCurlMultiManager：\n"
        + "\n".join(line for line in bridge_symbols if "QCCurlMultiManager" in line)
    )


def test_production_libraries_exclude_test_hook_symbols_and_strings() -> None:
    """验证正式动态库不包含测试钩子符号或稳定字符串。"""

    repo_root = Path(__file__).resolve().parents[1]
    libraries = (
        repo_root / "build-test-shared-gcc" / "src" / "libQCurl.so.2.0.0",
        repo_root / "build-test-shared-gcc" / "src" / "libQCurlOtherExtras.so.2.0.0",
    )
    forbidden_tokens = (
        "QCURL_ENABLE_TEST_HOOKS",
        "QCURL_TEST_FORCE_",
        "ForTest",
        "ForTesting",
        "TestHook",
    )

    for library in libraries:
        assert library.is_file(), f"缺少正式动态库：{library}"

        symbols = subprocess.run(
            ["nm", "-D", "--defined-only", str(library)],
            check=False,
            capture_output=True,
            text=True,
        )
        assert symbols.returncode == 0, symbols.stderr

        strings = subprocess.run(
            ["strings", "-a", str(library)],
            check=False,
            capture_output=True,
            text=True,
        )
        assert strings.returncode == 0, strings.stderr

        exported_surface = symbols.stdout + strings.stdout
        leaked_tokens = [token for token in forbidden_tokens if token in exported_surface]
        assert not leaked_tokens, f"正式动态库泄漏测试钩子 {leaked_tokens}：{library}"
