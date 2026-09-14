from __future__ import annotations

import inspect
import json
from copy import deepcopy
from pathlib import Path

import pytest

from scripts import qcurl_abi_gate
from scripts import release_evidence_model
from scripts import release_identity
from scripts import release_tree_model
from scripts import run_release_gate


def test_release_gate_path_resolution_respects_function_size_limit() -> None:
    source_lines, _ = inspect.getsourcelines(run_release_gate._resolve_paths)

    assert len(source_lines) <= 60


def test_hardbreak_abidiff_returncode_accepts_only_abi_change_bits() -> None:
    assert qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(0)
    assert qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(4)
    assert qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(8)
    assert qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(12)

    assert not qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(1)
    assert not qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(2)
    assert not qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(5)
    assert not qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(13)


def test_abi_gate_rejects_private_transfer_record_dynamic_symbols() -> None:
    symbols = (
        "0000000000001000 T QCurl::QCCurlMultiManager::addPersistentTransfer(unsigned long)\n"
        "0000000000002000 T QCurl::QCCurlMultiTransferRecord::handle() const\n"
    )

    with pytest.raises(qcurl_abi_gate.AbiGateError, match="QCCurlMultiTransferRecord"):
        qcurl_abi_gate.validate_dynamic_symbol_contract(symbols)


def test_abi_gate_accepts_opaque_transfer_bridge_symbols() -> None:
    symbols = (
        "0000000000001000 T QCurl::QCCurlMultiManager::addPersistentTransfer(unsigned long)\n"
        "0000000000002000 T QCurl::QCCurlMultiManager::removeTransfer(unsigned long)\n"
    )

    qcurl_abi_gate.validate_dynamic_symbol_contract(symbols)


def test_abi_gate_rejects_removed_reply_delete_later_wrapper() -> None:
    symbols = "0000000000001000 T QCurl::QCNetworkReply::deleteLater()\n"

    with pytest.raises(
        qcurl_abi_gate.AbiGateError, match="QCNetworkReply::deleteLater"
    ):
        qcurl_abi_gate.validate_dynamic_symbol_contract(symbols)


def test_abi_gate_accepts_inherited_qobject_delete_later_symbol() -> None:
    symbols = "                 U QObject::deleteLater()\n"

    qcurl_abi_gate.validate_dynamic_symbol_contract(symbols)


def test_release_gate_promotion_candidate_uses_only_archived_abi_comparison(
    tmp_path: Path,
    capsys,
) -> None:
    _prepare_full_tree_caches(tmp_path)
    result = run_release_gate.main(
        [
            "--tier",
            "full",
            "--stage",
            "promotion",
            "--release-shared-build-dir",
            str(tmp_path / "build"),
            "--release-static-build-dir",
            str(tmp_path / "build-static"),
            "--test-shared-gcc-build-dir",
            str(tmp_path / "build-tests"),
            "--test-shared-clang-build-dir",
            str(tmp_path / "build-tests-clang"),
            "--asan-ubsan-lsan-build-dir",
            str(tmp_path / "build-asan-ubsan-lsan"),
            "--tsan-build-dir",
            str(tmp_path / "build-tsan"),
            "--abi-mode",
            "promotion-candidate",
            "--abi-hardbreak-baseline",
            str(tmp_path / "previous.abi.xml"),
            "--abi-hardbreak-report",
            str(tmp_path / "build" / "abi" / "qcurl-core-v1-to-v2.abidiff.txt"),
            "--abi-hardbreak-current-snapshot",
            str(
                tmp_path
                / "build"
                / "abi"
                / "qcurl-core-v2.promotion-candidate.abi.xml"
            ),
            "--dry-run",
        ]
    )

    assert result == 0

    plan = json.loads(capsys.readouterr().out)
    steps = plan["steps"]
    names = [step["name"] for step in steps]

    assert "abi_hardbreak_report" in names
    assert "abi_current_baseline_diff" not in names

    hardbreak_step = steps[names.index("abi_hardbreak_report")]
    assert "hardbreak-report" in hardbreak_step["command"]


def test_release_gate_promotion_candidate_records_bound_input_artifacts(
    tmp_path: Path,
) -> None:
    args = run_release_gate.build_parser().parse_args(
        [
            "--tier",
            "full",
            "--stage",
            "promotion",
            "--release-shared-build-dir",
            str(tmp_path / "build"),
            "--release-static-build-dir",
            str(tmp_path / "build-static"),
            "--test-shared-gcc-build-dir",
            str(tmp_path / "build-tests"),
            "--test-shared-clang-build-dir",
            str(tmp_path / "build-tests-clang"),
            "--asan-ubsan-lsan-build-dir",
            str(tmp_path / "build-asan-ubsan-lsan"),
            "--tsan-build-dir",
            str(tmp_path / "build-tsan"),
            "--abi-mode",
            "promotion-candidate",
            "--abi-hardbreak-baseline",
            str(tmp_path / "v1.abi.xml"),
            "--abi-hardbreak-report",
            str(tmp_path / "build" / "abi" / "qcurl-core-v1-to-v2.abidiff.txt"),
            "--abi-hardbreak-current-snapshot",
            str(
                tmp_path
                / "build"
                / "abi"
                / "qcurl-core-v2.promotion-candidate.abi.xml"
            ),
        ]
    )
    steps = run_release_gate._selected_steps(args)
    artifacts = dict(run_release_gate._required_artifacts(args, steps))

    assert artifacts["candidate_core_library"] == (
        tmp_path / "build" / "src" / "libQCurl.so.2.0.0"
    )
    assert artifacts["v1_baseline"] == tmp_path / "v1.abi.xml"
    assert artifacts["abi_hardbreak_current_snapshot"] == (
        tmp_path / "build" / "abi" / "qcurl-core-v2.promotion-candidate.abi.xml"
    )
    assert artifacts["abi_hardbreak_report"] == (
        tmp_path / "build" / "abi" / "qcurl-core-v1-to-v2.abidiff.txt"
    )


def test_release_gate_writes_promotion_manifest_binding(tmp_path: Path) -> None:
    repo = _init_identity_repo(tmp_path)
    build_dir = repo / "build"
    static_build_dir = repo / "build-static"
    test_build_dir = repo / "build-tests"
    manifest_path = repo / "artifacts" / "promotion.json"
    input_paths = [
        build_dir / "src" / "libQCurl.so.2.0.0",
        repo / "v1.abi.xml",
        build_dir / "abi" / "qcurl-core-v1-to-v2.abidiff.txt",
        build_dir / "abi" / "qcurl-core-v2.promotion-candidate.abi.xml",
    ]
    for path in input_paths:
        path.parent.mkdir(parents=True, exist_ok=True)
        content = "<abi-corpus/>\n" if path.suffix == ".xml" else path.name + "\n"
        path.write_text(content, encoding="utf-8")

    args = run_release_gate.build_parser().parse_args(
        [
            "--tier",
            "full",
            "--stage",
            "promotion",
            "--release-shared-build-dir",
            str(build_dir),
            "--release-static-build-dir",
            str(static_build_dir),
            "--test-shared-gcc-build-dir",
            str(test_build_dir),
            "--test-shared-clang-build-dir",
            str(repo / "test-shared-clang"),
            "--asan-ubsan-lsan-build-dir",
            str(repo / "asan-ubsan-lsan"),
            "--tsan-build-dir",
            str(repo / "tsan"),
            "--abi-mode",
            "promotion-candidate",
            "--abi-hardbreak-baseline",
            str(repo / "v1.abi.xml"),
            "--abi-hardbreak-report",
            str(input_paths[2]),
            "--abi-hardbreak-current-snapshot",
            str(input_paths[3]),
            "--manifest",
            str(manifest_path),
        ]
    )
    run_release_gate._resolve_paths(args, repo)
    step = run_release_gate.GateStep(
        "abi_hardbreak_report",
        run_release_gate.GateTier.FULL,
        ["true"],
        "promotion report",
        "release-shared",
        ("abi_hardbreak_report", "abi_hardbreak_current_snapshot"),
    )
    commands = [step.command]
    start_identity = release_identity.build_identity(
        repo,
        build_dirs=run_release_gate._identity_build_dirs(args),
        authority_paths=[],
        commands=commands,
    )

    check = run_release_gate._write_gate_manifest(
        args,
        repo,
        [step],
        [],
        {
            step.name: {
                "result": "pass",
                "returncode": 0,
                "command": step.command,
                "producerTreeId": "release-shared",
            }
        },
        start_identity,
    )

    assert check.valid
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    assert "qa_manifest" not in manifest["artifacts"]
    assert manifest["promotion"]["manifest_path"] == "artifacts/promotion.json"
    assert manifest["artifacts"]["candidate_core_library"]["sha256"] == (
        release_identity.file_sha256(input_paths[0])
    )


def test_release_gate_default_skips_abi_compatibility_gate(
    tmp_path: Path,
    capsys,
) -> None:
    _prepare_full_tree_caches(tmp_path)
    result = run_release_gate.main(
        [
            "--tier",
            "full",
            "--release-shared-build-dir",
            str(tmp_path / "build"),
            "--release-static-build-dir",
            str(tmp_path / "build-static"),
            "--test-shared-gcc-build-dir",
            str(tmp_path / "build-tests"),
            "--test-shared-clang-build-dir",
            str(tmp_path / "build-tests-clang"),
            "--asan-ubsan-lsan-build-dir",
            str(tmp_path / "build-asan-ubsan-lsan"),
            "--tsan-build-dir",
            str(tmp_path / "build-tsan"),
            "--dry-run",
        ]
    )

    assert result == 0

    plan = json.loads(capsys.readouterr().out)
    names = [step["name"] for step in plan["steps"]]

    assert plan["abiMode"] == "none"
    assert "dynamic_symbol_allowlist" in names
    assert "blocking_extras_dynamic_symbol_allowlist" not in names
    assert "other_extras_dynamic_symbol_allowlist" in names
    assert "abi_current_baseline_diff" not in names
    assert "abi_hardbreak_report" not in names


def test_release_gate_abi_mode_supports_unstable_default_and_explicit_diagnostics(
    tmp_path: Path,
) -> None:
    parser = run_release_gate.build_parser()
    base_args = [
        "--tier",
        "full",
        "--release-shared-build-dir",
        str(tmp_path / "build"),
        "--release-static-build-dir",
        str(tmp_path / "build-static"),
        "--test-shared-gcc-build-dir",
        str(tmp_path / "build-tests"),
        "--test-shared-clang-build-dir",
        str(tmp_path / "build-tests-clang"),
        "--asan-ubsan-lsan-build-dir",
        str(tmp_path / "build-asan-ubsan-lsan"),
        "--tsan-build-dir",
        str(tmp_path / "build-tsan"),
    ]

    assert parser.parse_args(base_args).abi_mode == "none"
    assert parser.parse_args(base_args + ["--abi-mode", "none"]).abi_mode == "none"
    assert parser.parse_args(base_args + ["--abi-mode", "current"]).abi_mode == "current"
    assert (
        parser.parse_args(base_args + ["--abi-mode", "promotion-candidate"]).abi_mode
        == "promotion-candidate"
    )
    with pytest.raises(SystemExit):
        parser.parse_args(base_args + ["--abi-mode", "hybrid"])


def test_release_gate_abi_modes_have_mutually_exclusive_step_lists(
    tmp_path: Path,
    capsys,
) -> None:
    _prepare_full_tree_caches(tmp_path)
    base_args = [
        "--tier",
        "full",
        "--release-shared-build-dir",
        str(tmp_path / "build"),
        "--release-static-build-dir",
        str(tmp_path / "build-static"),
        "--test-shared-gcc-build-dir",
        str(tmp_path / "build-tests"),
        "--test-shared-clang-build-dir",
        str(tmp_path / "build-tests-clang"),
        "--asan-ubsan-lsan-build-dir",
        str(tmp_path / "build-asan-ubsan-lsan"),
        "--tsan-build-dir",
        str(tmp_path / "build-tsan"),
    ]
    none_result = run_release_gate.main(base_args + ["--dry-run"])
    assert none_result == 0
    none_names = [
        item["name"] for item in json.loads(capsys.readouterr().out)["steps"]
    ]
    assert "abi_current_baseline_diff" not in none_names
    assert "abi_hardbreak_report" not in none_names

    current_result = run_release_gate.main(
        base_args + ["--abi-mode", "current", "--dry-run"]
    )
    assert current_result == 0
    current_names = [
        item["name"] for item in json.loads(capsys.readouterr().out)["steps"]
    ]
    assert "abi_current_baseline_diff" in current_names
    assert "abi_hardbreak_report" not in current_names

    candidate_result = run_release_gate.main(
        base_args
        + [
            "--stage",
            "promotion",
            "--abi-mode",
            "promotion-candidate",
            "--abi-hardbreak-baseline",
            str(tmp_path / "v1.abi.xml"),
            "--abi-hardbreak-report",
            str(tmp_path / "build" / "abi" / "qcurl-core-v1-to-v2.abidiff.txt"),
            "--abi-hardbreak-current-snapshot",
            str(
                tmp_path
                / "build"
                / "abi"
                / "qcurl-core-v2.promotion-candidate.abi.xml"
            ),
            "--dry-run",
        ]
    )
    assert candidate_result == 0
    candidate_names = [
        item["name"] for item in json.loads(capsys.readouterr().out)["steps"]
    ]
    assert "abi_hardbreak_report" in candidate_names
    assert "abi_current_baseline_diff" not in candidate_names


def test_release_gate_abi_mode_parameters_fail_closed(tmp_path: Path) -> None:
    parser = run_release_gate.build_parser()
    candidate_args = parser.parse_args(
        ["--abi-mode", "promotion-candidate"]
    )
    with pytest.raises(ValueError, match="promotion-candidate"):
        run_release_gate._resolve_paths(candidate_args, tmp_path)

    current_args = parser.parse_args(
        [
            "--abi-mode",
            "current",
            "--abi-hardbreak-baseline",
            str(tmp_path / "v1.abi.xml"),
        ]
    )
    with pytest.raises(ValueError, match="current"):
        run_release_gate._resolve_paths(current_args, tmp_path)

    none_args = parser.parse_args(
        [
            "--abi-mode",
            "none",
            "--abi-hardbreak-baseline",
            str(tmp_path / "v1.abi.xml"),
        ]
    )
    with pytest.raises(ValueError, match="none"):
        run_release_gate._resolve_paths(none_args, tmp_path)


def test_release_gate_keeps_abi_tools_opt_in_and_preserves_promotion_gate(
    tmp_path: Path,
) -> None:
    args = run_release_gate.build_parser().parse_args(
        [
            "--tier",
            "full",
            "--release-shared-build-dir",
            str(tmp_path / "build"),
            "--release-static-build-dir",
            str(tmp_path / "build-static"),
            "--test-shared-gcc-build-dir",
            str(tmp_path / "build-tests"),
            "--test-shared-clang-build-dir",
            str(tmp_path / "build-tests-clang"),
            "--asan-ubsan-lsan-build-dir",
            str(tmp_path / "build-asan-ubsan-lsan"),
            "--tsan-build-dir",
            str(tmp_path / "build-tsan"),
        ]
    )
    steps = run_release_gate._selected_steps(args)
    artifacts = dict(run_release_gate._required_artifacts(args, steps))

    assert artifacts["core_dynamic_symbols"].name == "qcurl-core-v2.dynamic-symbols.json"
    assert "blocking_extras_dynamic_symbols" not in artifacts
    assert artifacts["other_extras_dynamic_symbols"].name == (
        "qcurl-other-extras-v2.dynamic-symbols.json"
    )
    assert "abi_current_report" not in artifacts
    assert "abi_current_snapshot" not in artifacts

    args.abi_mode = "current"
    current_artifacts = dict(
        run_release_gate._required_artifacts(
            args,
            run_release_gate._selected_steps(args),
        )
    )
    assert current_artifacts["abi_current_report"].name == "qcurl-core-v2.abidiff.txt"
    assert current_artifacts["abi_current_snapshot"].name == "qcurl-core-v2.current.abi.xml"

    abi_args = qcurl_abi_gate.build_parser().parse_args(["diff"])
    assert abi_args.library == Path("build/src/libQCurl.so.2.0.0")
    assert abi_args.baseline == Path("abi/baseline/qcurl-core-v2.abi.xml")
    assert qcurl_abi_gate.PROMOTION_REQUIRED_GATES[-1] == "abi_hardbreak_report"

    promote_args = qcurl_abi_gate.build_parser().parse_args(
        [
            "promote",
            "--candidate-manifest",
            str(tmp_path / "manifest.json"),
            "--candidate-commit",
            "a" * 40,
            "--old-baseline",
            str(tmp_path / "v1.abi.xml"),
        ]
    )
    assert promote_args.output == Path("abi/baseline/qcurl-core-v2.abi.xml")


def _init_identity_repo(tmp_path: Path) -> Path:
    repo = tmp_path / "repo"
    repo.mkdir()
    run_release = lambda *args: __import__("subprocess").run(
        ["git", *args], cwd=repo, check=True, capture_output=True, text=True
    )
    run_release("init", "-q")
    (repo / "tracked.txt").write_text("one\n", encoding="utf-8")
    (repo / ".gitignore").write_text("build/\nartifacts/\n", encoding="utf-8")
    run_release("add", "tracked.txt", ".gitignore")
    run_release(
        "-c",
        "user.name=qa",
        "-c",
        "user.email=qa@example.invalid",
        "commit",
        "-qm",
        "base",
    )
    return repo


def _prepare_manifest_authority_repo(tmp_path: Path) -> tuple[Path, list[Path]]:
    repo = _init_identity_repo(tmp_path)
    package = (
        repo
        / ".helloagents/plans/202609021948_qcurl_overdesign_compat_cleanup_remediation"
    )
    authority = [repo / "docs/arch/2.0.0-hard-break-release-contract.md"]
    local_files = [
        package / name for name in ("requirements.md", "plan.md", "contract.json", "tasks.md")
    ]
    for path in authority + local_files:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(path.name + "\n", encoding="utf-8")
    session = repo / ".helloagents/sessions/master/session-1"
    (repo / ".helloagents/sessions/active.json").parent.mkdir(
        parents=True, exist_ok=True
    )
    (repo / ".helloagents/sessions/active.json").write_text(
        '{"workspace":"master","session":"session-1"}\n',
        encoding="utf-8",
    )
    (session / "artifacts").mkdir(parents=True, exist_ok=True)
    for path in [
        session / "STATE.md",
        session / "artifacts/finding-map.json",
        session / "artifacts/qa-review.json",
        session / "artifacts/closeout.json",
    ]:
        path.write_text(path.name + "\n", encoding="utf-8")
    (repo / "docs/reviews/2026-07-31-release-blockers.md").parent.mkdir(
        parents=True, exist_ok=True
    )
    (repo / "docs/reviews/2026-07-31-release-blockers.md").write_text(
        "historical\n", encoding="utf-8"
    )
    exclude = repo / ".git/info/exclude"
    exclude.write_text(
        exclude.read_text(encoding="utf-8")
        + "\n.helloagents/\ndocs/reviews/\ndocs/arch/2.0.0-hard-break-release-contract.md\n",
        encoding="utf-8",
    )
    return repo, authority


def test_default_authority_paths_bind_versioned_release_contract(
    tmp_path: Path,
) -> None:
    repo = tmp_path / "repo"
    repo.mkdir()

    relative_paths = [
        path.relative_to(repo).as_posix()
        for path in release_identity.default_authority_paths(repo)
    ]

    assert relative_paths == [
        "docs/arch/2.0.0-hard-break-release-contract.md",
    ]


def _manifest_authority_paths(repo: Path, authority: list[Path]) -> list[Path]:
    argv = ["--release-shared-build-dir", str(repo / "release-shared")]
    for path in authority:
        argv.extend(("--authority", str(path)))
    args = run_release_gate.build_parser().parse_args(argv)
    run_release_gate._resolve_paths(args, repo)
    return run_release_gate._authority_paths(args, repo)


def _full_gate_authority_paths(repo: Path, paths: list[Path]) -> list[Path]:
    argv = [
        "--tier",
        "full",
        "--release-shared-build-dir",
        str(repo / "release-shared"),
        "--release-static-build-dir",
        str(repo / "release-static"),
        "--test-shared-gcc-build-dir",
        str(repo / "test-shared-gcc"),
        "--test-shared-clang-build-dir",
        str(repo / "test-shared-clang"),
        "--asan-ubsan-lsan-build-dir",
        str(repo / "asan-ubsan-lsan"),
        "--tsan-build-dir",
        str(repo / "tsan"),
    ]
    for path in paths:
        argv.extend(("--authority", str(path)))
    args = run_release_gate.build_parser().parse_args(argv)
    run_release_gate._resolve_paths(args, repo)
    return run_release_gate._authority_paths(args, repo)


def test_full_gate_requires_exact_authority_set(tmp_path: Path) -> None:
    repo, authority = _prepare_manifest_authority_repo(tmp_path)

    actual = _full_gate_authority_paths(repo, authority)
    assert sorted(actual) == sorted(path.resolve() for path in authority)

    invalid_sets = (
        [],
        authority + [repo / "extra-authority.md"],
        authority + [authority[0]],
    )
    for invalid in invalid_sets:
        with pytest.raises(ValueError, match="authority"):
            _full_gate_authority_paths(repo, invalid)


def test_snapshot_only_cannot_claim_pass(tmp_path: Path) -> None:
    repo, authority = _prepare_manifest_authority_repo(tmp_path)
    manifest = release_identity.create_snapshot(
        repo,
        build_dirs=[],
        authority_paths=authority,
        commands=[],
        required_gates=["full_gate"],
    )
    manifest["snapshot_kind"] = "t0"
    manifest["gates"]["results"] = {
        "full_gate": {"result": "pass", "returncode": 0}
    }
    manifest["result"] = "pass"

    check = release_identity.verify_snapshot(
        manifest, repo, build_dirs=[], authority_paths=authority
    )

    assert not check.valid
    assert check.result == "blocked"
    assert any("snapshot-only" in reason for reason in check.reasons)


def test_runtime_progress_is_not_authority(tmp_path: Path) -> None:
    repo, authority = _prepare_manifest_authority_repo(tmp_path)
    runtime_progress = (
        repo
        / ".helloagents/plans/202609021948_qcurl_overdesign_compat_cleanup_remediation/tasks.md"
    )

    with pytest.raises(ValueError, match="authority"):
        _full_gate_authority_paths(repo, authority[:-1] + [runtime_progress])


def test_release_manifest_authority_is_exactly_immutable_contract(tmp_path: Path) -> None:
    repo, authority = _prepare_manifest_authority_repo(tmp_path)

    actual = _manifest_authority_paths(repo, authority)

    assert sorted(actual) == sorted(path.resolve() for path in authority)


def test_release_manifest_ignores_runtime_progress_mutations(tmp_path: Path) -> None:
    repo, authority = _prepare_manifest_authority_repo(tmp_path)
    paths = _manifest_authority_paths(repo, authority)
    manifest = release_identity.create_snapshot(
        repo, build_dirs=[], authority_paths=paths, commands=[]
    )

    runtime_files = [
        repo
        / ".helloagents/plans/202609021948_qcurl_overdesign_compat_cleanup_remediation/requirements.md",
        repo
        / ".helloagents/plans/202609021948_qcurl_overdesign_compat_cleanup_remediation/plan.md",
        repo
        / ".helloagents/plans/202609021948_qcurl_overdesign_compat_cleanup_remediation/contract.json",
        repo
        / ".helloagents/plans/202609021948_qcurl_overdesign_compat_cleanup_remediation/tasks.md",
        repo / ".helloagents/sessions/master/session-1/STATE.md",
        repo / ".helloagents/sessions/master/session-1/artifacts/finding-map.json",
        repo / ".helloagents/sessions/master/session-1/artifacts/qa-review.json",
        repo / ".helloagents/sessions/master/session-1/artifacts/closeout.json",
    ]
    for path in runtime_files:
        path.write_text(path.read_text(encoding="utf-8") + "progress\n", encoding="utf-8")

    check = release_identity.verify_snapshot(
        manifest, repo, build_dirs=[], authority_paths=paths
    )

    assert check.valid


def test_release_manifest_rejects_immutable_contract_mutations(tmp_path: Path) -> None:
    repo, authority = _prepare_manifest_authority_repo(tmp_path)
    paths = _manifest_authority_paths(repo, authority)
    manifest = release_identity.create_snapshot(
        repo, build_dirs=[], authority_paths=paths, commands=[]
    )

    for path in authority:
        original = path.read_text(encoding="utf-8")
        path.write_text(original + "mutation\n", encoding="utf-8")
        check = release_identity.verify_snapshot(
            manifest, repo, build_dirs=[], authority_paths=paths
        )
        path.write_text(original, encoding="utf-8")
        assert not check.valid
        assert any("authority" in reason for reason in check.reasons)


def test_manifest_identity_mutation_matrix_rejects_tracked_and_untracked_changes(
    tmp_path: Path,
) -> None:
    repo = _init_identity_repo(tmp_path)
    authority = repo / "authority.md"
    authority.write_text("route\n", encoding="utf-8")
    manifest = release_identity.create_snapshot(
        repo,
        build_dirs=[],
        authority_paths=[authority],
        commands=[["pytest", "tests"]],
    )

    assert release_identity.verify_snapshot(
        manifest, repo, build_dirs=[], authority_paths=[authority]
    ).valid

    (repo / "tracked.txt").write_text("changed\n", encoding="utf-8")
    tracked_check = release_identity.verify_snapshot(
        manifest, repo, build_dirs=[], authority_paths=[authority]
    )
    assert not tracked_check.valid
    assert any("tracked_patch" in reason for reason in tracked_check.reasons)

    (repo / "tracked.txt").write_text("one\n", encoding="utf-8")
    (repo / "untracked.txt").write_text("new\n", encoding="utf-8")
    untracked_check = release_identity.verify_snapshot(
        manifest, repo, build_dirs=[], authority_paths=[authority]
    )
    assert not untracked_check.valid
    assert any("untracked" in reason for reason in untracked_check.reasons)


def test_manifest_machine_result_cannot_be_overridden_by_manual_pass(
    tmp_path: Path,
) -> None:
    repo = _init_identity_repo(tmp_path)
    manifest = release_identity.create_snapshot(
        repo, build_dirs=[], authority_paths=[], commands=[]
    )
    manifest["result"] = "pass"
    manifest["gates"] = {"required": ["full_ctest"], "results": {"full_ctest": "pass"}}
    (repo / "tracked.txt").write_text("drift\n", encoding="utf-8")

    check = release_identity.verify_snapshot(
        manifest, repo, build_dirs=[], authority_paths=[]
    )
    assert not check.valid
    assert check.result == "blocked"
    assert any("tracked_patch" in reason for reason in check.reasons)


@pytest.mark.parametrize("field", ["head", "submodules", "toolchain", "platform"])
def test_manifest_rejects_each_mutated_identity_section(
    tmp_path: Path, field: str
) -> None:
    repo = _init_identity_repo(tmp_path)
    manifest = release_identity.create_snapshot(
        repo, build_dirs=[], authority_paths=[], commands=[]
    )
    mutated = deepcopy(manifest)
    mutated["identity"][field] = {"forged": "value"}

    check = release_identity.verify_snapshot(
        mutated, repo, build_dirs=[], authority_paths=[]
    )

    assert not check.valid
    assert any(field in reason for reason in check.reasons)


def test_manifest_rejects_authority_capability_and_command_drift(
    tmp_path: Path,
) -> None:
    repo = _init_identity_repo(tmp_path)
    authority = repo / "authority.md"
    authority.write_text("route one\n", encoding="utf-8")
    build_dir = repo / "build"
    build_dir.mkdir()
    (build_dir / "CMakeCache.txt").write_text(
        "QCURL_BUILD_SHARED_LIBS:BOOL=ON\n", encoding="utf-8"
    )
    commands = [["pytest", "tests"]]
    manifest = release_identity.create_snapshot(
        repo, build_dirs=[build_dir], authority_paths=[authority], commands=commands
    )

    authority.write_text("route two\n", encoding="utf-8")
    authority_check = release_identity.verify_snapshot(
        manifest,
        repo,
        build_dirs=[build_dir],
        authority_paths=[authority],
        commands=commands,
    )
    assert any("authority" in reason for reason in authority_check.reasons)

    authority.write_text("route one\n", encoding="utf-8")
    (build_dir / "CMakeCache.txt").write_text(
        "QCURL_BUILD_SHARED_LIBS:BOOL=OFF\n", encoding="utf-8"
    )
    capability_check = release_identity.verify_snapshot(
        manifest,
        repo,
        build_dirs=[build_dir],
        authority_paths=[authority],
        commands=commands,
    )
    assert any("capabilities" in reason for reason in capability_check.reasons)

    (build_dir / "CMakeCache.txt").write_text(
        "QCURL_BUILD_SHARED_LIBS:BOOL=ON\n", encoding="utf-8"
    )
    command_check = release_identity.verify_snapshot(
        manifest,
        repo,
        build_dirs=[build_dir],
        authority_paths=[authority],
        commands=[["pytest", "different"]],
    )
    assert any("commands" in reason for reason in command_check.reasons)


@pytest.mark.parametrize(
    ("mutation", "expected"),
    [
        (lambda value: value.update(schema="unknown"), "schema"),
        (lambda value: value.update(gates={}), "gate schema"),
        (
            lambda value: value.update(
                artifacts={"report": {"path": "missing.json", "required": True}}
            ),
            "required artifact",
        ),
    ],
)
def test_manifest_fails_closed_for_schema_gate_and_artifact_errors(
    tmp_path: Path, mutation, expected: str
) -> None:
    repo = _init_identity_repo(tmp_path)
    manifest = release_identity.create_snapshot(
        repo, build_dirs=[], authority_paths=[], commands=[]
    )
    mutation(manifest)

    check = release_identity.verify_snapshot(
        manifest, repo, build_dirs=[], authority_paths=[]
    )

    assert not check.valid
    assert any(expected in reason for reason in check.reasons)


def test_manifest_rejects_required_artifact_content_drift(tmp_path: Path) -> None:
    repo = _init_identity_repo(tmp_path)
    artifact = repo / "artifacts" / "report.json"
    artifact.parent.mkdir()
    artifact.write_text('{"result":"pass"}\n', encoding="utf-8")
    manifest = release_identity.create_snapshot(
        repo, build_dirs=[], authority_paths=[], commands=[]
    )
    manifest["artifacts"]["report"] = {
        "path": "artifacts/report.json",
        "required": True,
        "sha256": release_identity.file_sha256(artifact),
    }
    artifact.write_text('{"result":"forged"}\n', encoding="utf-8")

    check = release_identity.verify_snapshot(
        manifest, repo, build_dirs=[], authority_paths=[]
    )

    assert not check.valid
    assert "required artifact digest mismatch: report" in check.reasons


def _set_manifest_payload_digest(manifest: dict) -> None:
    payload = deepcopy(manifest)
    payload.pop("manifestPayloadSha256", None)
    manifest["manifestPayloadSha256"] = release_identity._digest(payload)


def test_required_artifact_requires_regular_file_digest(tmp_path: Path) -> None:
    repo = _init_identity_repo(tmp_path)
    artifact = repo / "artifacts" / "report.json"
    artifact.parent.mkdir()
    artifact.write_text('{"result":"pass"}\n', encoding="utf-8")
    manifest = release_identity.create_snapshot(
        repo, build_dirs=[], authority_paths=[], commands=[]
    )
    entries = (
        {"path": "artifacts/report.json", "required": True, "kind": "report"},
        {
            "path": "artifacts/report.json",
            "required": True,
            "kind": "report",
            "sha256": "",
        },
        {
            "path": "artifacts/report.json",
            "required": True,
            "kind": "report",
            "sha256": None,
        },
        {
            "path": "artifacts/missing.json",
            "required": True,
            "kind": "report",
            "sha256": "0" * 64,
        },
        {
            "path": "artifacts",
            "required": True,
            "kind": "report",
            "sha256": "0" * 64,
        },
    )

    for entry in entries:
        candidate = deepcopy(manifest)
        candidate["artifacts"] = {"report": entry}
        check = release_identity.verify_snapshot(
            candidate, repo, build_dirs=[], authority_paths=[]
        )

        assert not check.valid
        assert any("digest" in reason for reason in check.reasons)


def test_manifest_payload_digest_is_canonical_and_not_self_artifact(
    tmp_path: Path,
) -> None:
    repo = _init_identity_repo(tmp_path)
    artifact = repo / "artifacts" / "report.json"
    artifact.parent.mkdir()
    artifact.write_text('{"result":"pass"}\n', encoding="utf-8")
    manifest = release_identity.create_snapshot(
        repo,
        build_dirs=[],
        authority_paths=[],
        commands=[],
        required_gates=["machine_gate"],
    )
    manifest["gates"]["results"] = {
        "machine_gate": {"result": "pass", "returncode": 0}
    }
    manifest["artifacts"]["report"] = {
        "path": "artifacts/report.json",
        "required": True,
        "kind": "report",
        "sha256": release_identity.file_sha256(artifact),
    }
    _set_manifest_payload_digest(manifest)

    check = release_identity.verify_snapshot(
        manifest, repo, build_dirs=[], authority_paths=[]
    )
    assert check.valid
    assert check.result == "pass"

    reordered = json.loads(json.dumps(manifest, ensure_ascii=False))
    reordered_check = release_identity.verify_snapshot(
        reordered, repo, build_dirs=[], authority_paths=[]
    )
    assert reordered_check.valid

    for mutation in (
        lambda value: value.pop("manifestPayloadSha256"),
        lambda value: value["artifacts"]["report"].update(kind="tampered"),
        lambda value: value["gates"]["results"]["machine_gate"].update(
            returncode=1
        ),
    ):
        mutated = deepcopy(manifest)
        mutation(mutated)
        mutated_check = release_identity.verify_snapshot(
            mutated, repo, build_dirs=[], authority_paths=[]
        )
        assert not mutated_check.valid
        assert any("payload" in reason for reason in mutated_check.reasons)

    manifest_path = repo / "artifacts" / "qa-manifest.json"
    manifest_path.write_text("candidate manifest\n", encoding="utf-8")
    self_referencing = deepcopy(manifest)
    self_referencing["artifacts"]["qa_manifest"] = {
        "path": "artifacts/qa-manifest.json",
        "required": True,
        "kind": "machine-qa-manifest",
        "sha256": release_identity.file_sha256(manifest_path),
    }
    _set_manifest_payload_digest(self_referencing)
    self_check = release_identity.verify_snapshot(
        self_referencing, repo, build_dirs=[], authority_paths=[]
    )

    assert not self_check.valid
    assert any("manifest" in reason for reason in self_check.reasons)


def test_release_gate_writes_and_revalidates_machine_pass_manifest(
    tmp_path: Path, monkeypatch
) -> None:
    repo = _init_identity_repo(tmp_path)
    manifest_path = repo / "artifacts" / "qa-manifest.json"
    step = run_release_gate.GateStep("machine_gate", run_release_gate.GateTier.FAST, ["true"], "test gate")
    monkeypatch.setattr(run_release_gate, "_repo_root", lambda: repo)
    monkeypatch.setattr(run_release_gate, "_selected_steps", lambda args: [step])
    monkeypatch.setattr(run_release_gate, "_run_step", lambda candidate, root: 0)
    monkeypatch.setattr(release_identity, "default_authority_paths", lambda root: [])

    result = run_release_gate.main(
        [
            "--manifest",
            str(manifest_path),
            "--release-shared-build-dir",
            str(repo / "build"),
            "--release-static-build-dir",
            str(repo / "build-static"),
        ]
    )

    assert result == 0
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    assert manifest["result"] == "pass"
    assert manifest["gates"]["results"]["machine_gate"]["returncode"] == 0
    assert (
        run_release_gate.main(["--manifest", str(manifest_path), "--verify-manifest"])
        == 0
    )
    assert (
        run_release_gate.main(
            [
                "--manifest",
                str(manifest_path),
                "--verify-manifest",
                "--abi-mode",
                "current",
            ]
        )
        == 1
    )


def _three_tree_gate_args(tmp_path: Path) -> tuple[Path, Path, Path, object]:
    release_shared = tmp_path / "release-shared"
    release_static = tmp_path / "release-static"
    test_shared = tmp_path / "test-shared-gcc"
    test_clang = tmp_path / "test-shared-clang"
    asan = tmp_path / "asan-ubsan-lsan"
    tsan = tmp_path / "tsan"
    parser = run_release_gate.build_parser()
    args = parser.parse_args(
        [
            "--tier",
            "full",
            "--release-shared-build-dir",
            str(release_shared),
            "--release-static-build-dir",
            str(release_static),
            "--test-shared-gcc-build-dir",
            str(test_shared),
            "--test-shared-clang-build-dir",
            str(test_clang),
            "--asan-ubsan-lsan-build-dir",
            str(asan),
            "--tsan-build-dir",
            str(tsan),
        ]
    )
    return release_shared, release_static, test_shared, args


def _write_build_cache(path: Path, *, build_testing: str, shared_libs: str) -> None:
    path.mkdir(parents=True)
    (path / "CMakeCache.txt").write_text(
        "\n".join(
            [
                f"BUILD_TESTING:BOOL={build_testing}",
                f"QCURL_BUILD_SHARED_LIBS:BOOL={shared_libs}",
                "",
            ]
        ),
        encoding="utf-8",
    )


def test_full_gate_requires_off_off_on_build_capabilities(tmp_path: Path) -> None:
    release_shared, release_static, test_shared, args = _three_tree_gate_args(tmp_path)
    trees = {
        "release-shared": release_shared,
        "release-static": release_static,
        "test-shared-gcc": test_shared,
        "test-shared-clang": tmp_path / "test-shared-clang",
        "asan-ubsan-lsan": tmp_path / "asan-ubsan-lsan",
        "tsan": tmp_path / "tsan",
    }
    for tree_id, path in trees.items():
        _write_tree_capability_cache(path, tree_id)

    run_release_gate._resolve_paths(args, tmp_path)
    run_release_gate._validate_build_capabilities(args)

    for path, expected in (
        (release_shared, "OFF"),
        (release_static, "OFF"),
        (test_shared, "ON"),
        (trees["test-shared-clang"], "ON"),
        (trees["asan-ubsan-lsan"], "ON"),
        (trees["tsan"], "ON"),
    ):
        cache_path = path / "CMakeCache.txt"
        cache_path.write_text(
            cache_path.read_text(encoding="utf-8").replace(
                "BUILD_TESTING:BOOL=" + expected,
                "BUILD_TESTING:BOOL=" + ("ON" if expected == "OFF" else "OFF"),
            ),
            encoding="utf-8",
        )
        with pytest.raises(ValueError, match="BUILD_TESTING"):
            run_release_gate._validate_build_capabilities(args)
        cache_path.write_text(
            cache_path.read_text(encoding="utf-8").replace(
                "BUILD_TESTING:BOOL=" + ("ON" if expected == "OFF" else "OFF"),
                "BUILD_TESTING:BOOL=" + expected,
            ),
            encoding="utf-8",
        )


def test_full_gate_routes_release_and_test_steps_to_distinct_trees(
    tmp_path: Path,
) -> None:
    release_shared, release_static, test_shared, args = _three_tree_gate_args(tmp_path)
    run_release_gate._resolve_paths(args, tmp_path)

    steps = {step.name: step for step in run_release_gate._selected_steps(args)}

    assert steps["shared_package_candidate"].producer_tree_id == "release-shared"
    assert steps["shared_package_evidence"].producer_tree_id == "release-shared"
    assert steps["static_build"].producer_tree_id == "release-static"
    assert steps["static_package_evidence"].producer_tree_id == "release-static"
    assert steps["full_ctest"].producer_tree_id == "test-shared-gcc"
    assert steps["clang_ctest"].producer_tree_id == "test-shared-clang"
    assert steps["package_asan_ubsan_lsan"].producer_tree_id == "asan-ubsan-lsan"
    assert steps["package_tsan"].producer_tree_id == "tsan"
    assert str(release_shared) in " ".join(steps["shared_package_evidence"].command)
    assert str(release_static) in " ".join(steps["static_package_evidence"].command)
    assert str(test_shared) in " ".join(steps["full_ctest"].command)
    assert steps["libcurl_consistency_full"].command[-2:] == [
        "--summary-report",
        str(test_shared / "libcurl_consistency" / "reports" / "summary.json"),
    ]

    static_build_command = steps["static_build"].command
    assert static_build_command[static_build_command.index("--target") + 1 :] == [
        "QCurl",
        "QCurlOtherExtras",
        "QCurlTestSupport",
        "-j",
        str(args.jobs),
    ]


def test_examples_benchmarks_gate_uses_direct_commands(tmp_path: Path) -> None:
    """示例与基准门禁不得通过 shell 拼接可配置路径或命令。"""

    _, _, test_shared, args = _three_tree_gate_args(tmp_path)
    run_release_gate._resolve_paths(args, tmp_path)
    steps = {step.name: step for step in run_release_gate._selected_steps(args)}
    gate_build = test_shared / "examples_benchmarks_gate"

    assert steps["examples_benchmarks_configure"].command == [
        args.cmake,
        "-S",
        ".",
        "-B",
        str(gate_build),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_TESTING=OFF",
        "-DBUILD_EXAMPLES=ON",
        "-DBUILD_BENCHMARKS=ON",
    ]
    assert steps["examples_benchmarks_build"].command == [
        args.cmake,
        "--build",
        str(gate_build),
        "--parallel",
        str(args.jobs),
    ]


def _six_tree_gate_args(tmp_path: Path) -> tuple[object, dict[str, Path]]:
    trees = {
        "release-shared": tmp_path / "release-shared",
        "release-static": tmp_path / "release-static",
        "test-shared-gcc": tmp_path / "test-shared-gcc",
        "test-shared-clang": tmp_path / "test-shared-clang",
        "asan-ubsan-lsan": tmp_path / "asan-ubsan-lsan",
        "tsan": tmp_path / "tsan",
    }
    parser = run_release_gate.build_parser()
    args = parser.parse_args(
        [
            "--tier",
            "full",
            "--release-shared-build-dir",
            str(trees["release-shared"]),
            "--release-static-build-dir",
            str(trees["release-static"]),
            "--test-shared-gcc-build-dir",
            str(trees["test-shared-gcc"]),
            "--test-shared-clang-build-dir",
            str(trees["test-shared-clang"]),
            "--asan-ubsan-lsan-build-dir",
            str(trees["asan-ubsan-lsan"]),
            "--tsan-build-dir",
            str(trees["tsan"]),
        ]
    )
    return args, trees


def test_full_gate_requires_all_six_explicit_tree_arguments(tmp_path: Path) -> None:
    parser = run_release_gate.build_parser()
    flags = (
        "--release-shared-build-dir",
        "--release-static-build-dir",
        "--test-shared-gcc-build-dir",
        "--test-shared-clang-build-dir",
        "--asan-ubsan-lsan-build-dir",
        "--tsan-build-dir",
    )
    for missing in flags:
        argv = ["--tier", "full"]
        for flag in flags:
            if flag != missing:
                argv.extend((flag, str(tmp_path / flag.removeprefix("--"))))
        args = parser.parse_args(argv)
        with pytest.raises(ValueError, match="six|tree"):
            run_release_gate._resolve_paths(args, tmp_path)


def test_release_gate_rejects_legacy_build_dir_fallback_flags() -> None:
    parser = run_release_gate.build_parser()
    with pytest.raises(SystemExit):
        parser.parse_args(["--build-dir", "build"])


def test_full_gate_exposes_fixed_six_tree_registry(tmp_path: Path) -> None:
    args, trees = _six_tree_gate_args(tmp_path)
    run_release_gate._resolve_paths(args, tmp_path)

    registry = run_release_gate._tree_registry(args)

    assert set(registry) == set(trees)
    for tree_id, path in trees.items():
        assert registry[tree_id]["path"] == path.resolve()
        assert registry[tree_id]["tree_id"] == tree_id


def test_release_tree_contract_uses_clang_for_both_sanitizer_trees() -> None:
    specs = release_tree_model.TREE_SPEC_BY_ID

    assert specs["asan-ubsan-lsan"].compiler_family == "clang"
    assert specs["tsan"].compiler_family == "clang"


def test_full_gate_steps_bind_producer_tree_and_required_artifacts(
    tmp_path: Path,
) -> None:
    args, _ = _six_tree_gate_args(tmp_path)
    run_release_gate._resolve_paths(args, tmp_path)

    steps = run_release_gate._selected_steps(args)
    tree_ids = {step.producer_tree_id for step in steps}
    artifact_ids = {
        artifact_id
        for step in steps
        for artifact_id in step.required_artifact_ids
    }

    assert tree_ids == {
        "release-shared",
        "release-static",
        "test-shared-gcc",
        "test-shared-clang",
        "asan-ubsan-lsan",
        "tsan",
    }
    assert {
        "parity_report",
        "shared_install_consumer_report",
        "static_install_consumer_report",
        "shared_lifecycle_report",
        "static_lifecycle_report",
        "asan_ubsan_lsan_report",
        "tsan_report",
        "uce_report",
        "doxygen_report",
        "core_dynamic_symbols",
        "other_extras_dynamic_symbols",
    }.issubset(artifact_ids)
    assert "abi_current_report" not in artifact_ids
    assert "abi_current_snapshot" not in artifact_ids


def _write_tree_capability_cache(path: Path, tree_id: str) -> None:
    compiler = (
        "clang++"
        if tree_id in {"test-shared-clang", "asan-ubsan-lsan", "tsan"}
        else "g++"
    )
    profile = ""
    flags = ""
    if tree_id == "asan-ubsan-lsan":
        profile = "asan-ubsan-lsan"
        flags = "-fsanitize=address,undefined,leak"
    elif tree_id == "tsan":
        profile = "tsan"
        flags = "-fsanitize=thread"
    build_testing = "OFF" if tree_id.startswith("release-") else "ON"
    shared_libs = "OFF" if tree_id == "release-static" else "ON"
    path.mkdir(parents=True)
    (path / "CMakeCache.txt").write_text(
        "\n".join(
            [
                f"BUILD_TESTING:BOOL={build_testing}",
                f"QCURL_BUILD_SHARED_LIBS:BOOL={shared_libs}",
                f"CMAKE_CXX_COMPILER:FILEPATH={compiler}",
                f"QCURL_SANITIZER_PROFILE:STRING={profile}",
                f"CMAKE_CXX_FLAGS:STRING={flags}",
                "",
            ]
        ),
        encoding="utf-8",
    )


def _prepare_full_tree_caches(tmp_path: Path) -> dict[str, Path]:
    trees = {
        "release-shared": tmp_path / "build",
        "release-static": tmp_path / "build-static",
        "test-shared-gcc": tmp_path / "build-tests",
        "test-shared-clang": tmp_path / "build-tests-clang",
        "asan-ubsan-lsan": tmp_path / "build-asan-ubsan-lsan",
        "tsan": tmp_path / "build-tsan",
    }
    for tree_id, path in trees.items():
        _write_tree_capability_cache(path, tree_id)
    return trees


def test_full_gate_rejects_tree_compiler_and_sanitizer_drift(tmp_path: Path) -> None:
    args, trees = _six_tree_gate_args(tmp_path)
    run_release_gate._resolve_paths(args, tmp_path)
    for tree_id, path in trees.items():
        _write_tree_capability_cache(path, tree_id)

    run_release_gate._validate_build_capabilities(args)

    compiler_cache = trees["test-shared-clang"] / "CMakeCache.txt"
    compiler_cache.write_text(
        compiler_cache.read_text(encoding="utf-8").replace("clang++", "g++"),
        encoding="utf-8",
    )
    with pytest.raises(ValueError, match="compiler family"):
        run_release_gate._validate_build_capabilities(args)
    compiler_cache.write_text(
        compiler_cache.read_text(encoding="utf-8").replace("g++", "clang++"),
        encoding="utf-8",
    )

    sanitizer_cache = trees["tsan"] / "CMakeCache.txt"
    sanitizer_cache.write_text(
        sanitizer_cache.read_text(encoding="utf-8").replace("tsan", "asan"),
        encoding="utf-8",
    )
    with pytest.raises(ValueError, match="sanitizer"):
        run_release_gate._validate_build_capabilities(args)


def test_manifest_rejects_missing_required_release_report(tmp_path: Path) -> None:
    repo = _init_identity_repo(tmp_path)
    manifest = release_identity.create_snapshot(
        repo,
        build_dirs=[],
        authority_paths=[],
        commands=[],
        required_gates=["full_ctest"],
    )
    manifest["gates"]["results"] = {
        "full_ctest": {"result": "pass", "returncode": 0}
    }
    manifest["result"] = "pass"
    _set_manifest_payload_digest(manifest)

    check = release_identity.verify_snapshot(
        manifest, repo, build_dirs=[], authority_paths=[]
    )

    assert not check.valid
    assert any("full_ctest" in reason or "required report" in reason for reason in check.reasons)


def test_manifest_rejects_empty_and_forged_producer_report(tmp_path: Path) -> None:
    repo = _init_identity_repo(tmp_path)
    report = repo / "test-shared-gcc" / "evidence" / "full-ctest.xml"
    report.parent.mkdir(parents=True)
    report.write_text("", encoding="utf-8")
    manifest = release_identity.create_snapshot(
        repo,
        build_dirs=[],
        authority_paths=[],
        commands=[],
        required_gates=["full_ctest"],
    )
    manifest["gates"]["results"] = {
        "full_ctest": {"result": "pass", "returncode": 0}
    }
    manifest["artifacts"]["full_ctest_report"] = {
        "path": "test-shared-gcc/evidence/full-ctest.xml",
        "required": True,
        "stage": "final",
        "treeId": "release-shared",
        "buildDir": "release-shared",
        "command": ["ctest", "--test-dir", "test-shared-gcc"],
        "commandSha256": "0" * 64,
        "kind": "junit",
        "sha256": release_identity.file_sha256(report),
    }
    _set_manifest_payload_digest(manifest)

    check = release_identity.verify_snapshot(
        manifest, repo, build_dirs=[], authority_paths=[]
    )

    assert not check.valid
    assert any("empty" in reason or "producer" in reason for reason in check.reasons)


def _producer_bound_ctest_manifest(tmp_path: Path) -> tuple[Path, dict, dict, list[str]]:
    repo = _init_identity_repo(tmp_path)
    tree = repo / "test-shared-gcc"
    report = tree / "evidence" / "full-ctest.xml"
    report.parent.mkdir(parents=True)
    report.write_text("<testsuite tests='1'/>\n", encoding="utf-8")
    registry = {
        "test-shared-gcc": {
            "tree_id": "test-shared-gcc",
            "path": tree,
            "build_testing": "ON",
            "shared_libs": "ON",
            "compiler_family": "gcc",
            "sanitizer_profile": None,
        }
    }
    command = ["ctest", "--test-dir", str(tree), "--output-on-failure"]
    manifest = release_identity.create_snapshot(
        repo,
        build_dirs=[tree],
        authority_paths=[],
        commands=[command],
        required_gates=["full_ctest"],
        stage="final",
        tree_registry=registry,
    )
    manifest["gates"]["results"] = {
        "full_ctest": {
            "result": "pass",
            "returncode": 0,
            "command": command,
            "producerTreeId": "test-shared-gcc",
        }
    }
    manifest["artifacts"]["full_ctest_report"] = {
        "path": "test-shared-gcc/evidence/full-ctest.xml",
        "required": True,
        "stage": "final",
        "treeId": "test-shared-gcc",
        "buildDir": str(tree),
        "command": command,
        "commandSha256": release_evidence_model.command_digest(command),
        "kind": "junit",
        "sha256": release_identity.file_sha256(report),
    }
    manifest["evidence_contract"] = {
        "artifact_ids": ["full_ctest_report"],
        "stage": "final",
    }
    _set_manifest_payload_digest(manifest)
    return repo, manifest, registry, command


def test_manifest_rejects_producer_tree_and_command_substitution(tmp_path: Path) -> None:
    repo, manifest, registry, command = _producer_bound_ctest_manifest(tmp_path)

    valid = release_identity.verify_snapshot(
        manifest,
        repo,
        build_dirs=[repo / "test-shared-gcc"],
        authority_paths=[],
        commands=[command],
        tree_registry=registry,
    )
    assert valid.valid

    for mutation in (
        lambda value: value["artifacts"]["full_ctest_report"].update(treeId="release-shared"),
        lambda value: value["artifacts"]["full_ctest_report"].update(
            command=["ctest", "--test-dir", "forged-tree"]
        ),
    ):
        candidate = deepcopy(manifest)
        mutation(candidate)
        _set_manifest_payload_digest(candidate)
        check = release_identity.verify_snapshot(
            candidate,
            repo,
            build_dirs=[repo / "test-shared-gcc"],
            authority_paths=[],
            commands=[command],
            tree_registry=registry,
        )
        assert not check.valid
        assert any("producer tree" in reason or "command" in reason for reason in check.reasons)

    forged = deepcopy(manifest)
    forged_command = ["ctest", "--test-dir", "forged-tree"]
    forged["artifacts"]["full_ctest_report"].update(
        command=forged_command,
        commandSha256=release_evidence_model.command_digest(forged_command),
    )
    _set_manifest_payload_digest(forged)
    check = release_identity.verify_snapshot(
        forged,
        repo,
        build_dirs=[repo / "test-shared-gcc"],
        authority_paths=[],
        commands=[command],
        tree_registry=registry,
    )
    assert not check.valid
    assert "required artifact gate command mismatch: full_ctest_report" in check.reasons


def test_release_gate_stage_is_explicit_and_closed() -> None:
    parser = run_release_gate.build_parser()

    assert parser.parse_args([]).stage == "remediation"
    for stage in ("remediation", "promotion", "final"):
        assert parser.parse_args(["--stage", stage]).stage == stage

    with pytest.raises(SystemExit):
        parser.parse_args(["--stage", "candidate"])


def test_full_gate_artifact_contract_is_complete_and_unique(tmp_path: Path) -> None:
    args, _ = _six_tree_gate_args(tmp_path)
    run_release_gate._resolve_paths(args, tmp_path)

    bindings = [
        (step, artifact_id)
        for step in run_release_gate._selected_steps(args)
        for artifact_id in step.required_artifact_ids
    ]
    artifact_ids = [artifact_id for _, artifact_id in bindings]

    assert len(artifact_ids) == len(set(artifact_ids))
    for step, artifact_id in bindings:
        contract = release_evidence_model.ARTIFACT_CONTRACTS[artifact_id]
        assert contract.gate == step.name
        assert contract.tree_id == step.producer_tree_id


def test_current_and_promotion_abi_artifacts_use_distinct_contracts(
    tmp_path: Path,
) -> None:
    args, trees = _six_tree_gate_args(tmp_path)
    args.abi_mode = "current"
    run_release_gate._resolve_paths(args, tmp_path)
    current_ids = {
        artifact_id
        for step in run_release_gate._selected_steps(args)
        if step.name == "abi_current_baseline_diff"
        for artifact_id in step.required_artifact_ids
    }

    args.abi_mode = "promotion-candidate"
    args.stage = "promotion"
    args.abi_hardbreak_baseline = tmp_path / "qcurl-core-v1.abi.xml"
    args.abi_hardbreak_report = trees["release-shared"] / "abi" / "qcurl-core-v1-to-v2.abidiff.txt"
    args.abi_hardbreak_current_snapshot = (
        trees["release-shared"] / "abi" / "qcurl-core-v2.promotion-candidate.abi.xml"
    )
    run_release_gate._resolve_paths(args, tmp_path)
    promotion_ids = {
        artifact_id
        for step in run_release_gate._selected_steps(args)
        if step.name == "abi_hardbreak_report"
        for artifact_id in step.required_artifact_ids
    }

    assert current_ids == {"abi_current_report", "abi_current_snapshot"}
    assert promotion_ids == {
        "abi_hardbreak_report",
        "abi_hardbreak_current_snapshot",
    }
    assert current_ids.isdisjoint(promotion_ids)
    assert current_ids | promotion_ids <= set(
        release_evidence_model.ARTIFACT_CONTRACTS
    )


def test_release_gate_rejects_abi_stage_and_output_route_drift(
    tmp_path: Path,
) -> None:
    args, trees = _six_tree_gate_args(tmp_path)
    args.abi_mode = "promotion-candidate"
    args.stage = "promotion"
    args.abi_hardbreak_baseline = tmp_path / "qcurl-core-v1.abi.xml"
    args.abi_hardbreak_report = tmp_path / "forged-report.txt"
    args.abi_hardbreak_current_snapshot = tmp_path / "forged-snapshot.xml"
    with pytest.raises(ValueError, match="fixed release-shared ABI output"):
        run_release_gate._resolve_paths(args, tmp_path)

    args.abi_hardbreak_report = (
        trees["release-shared"] / "abi" / "qcurl-core-v1-to-v2.abidiff.txt"
    )
    args.abi_hardbreak_current_snapshot = (
        trees["release-shared"]
        / "abi"
        / "qcurl-core-v2.promotion-candidate.abi.xml"
    )
    args.stage = "remediation"
    with pytest.raises(ValueError, match="promotion stage"):
        run_release_gate._resolve_paths(args, tmp_path)

    args.abi_mode = "current"
    args.stage = "promotion"
    args.abi_hardbreak_baseline = None
    args.abi_hardbreak_report = None
    args.abi_hardbreak_current_snapshot = None
    with pytest.raises(ValueError, match="promotion-candidate ABI mode"):
        run_release_gate._resolve_paths(args, tmp_path)


def test_manifest_recomputes_required_artifact_ids(tmp_path: Path) -> None:
    repo, manifest, registry, command = _producer_bound_ctest_manifest(tmp_path)
    manifest["evidence_contract"] = {"artifact_ids": [], "stage": "final"}
    _set_manifest_payload_digest(manifest)

    check = release_identity.verify_snapshot(
        manifest,
        repo,
        build_dirs=[repo / "test-shared-gcc"],
        authority_paths=[],
        commands=[command],
        tree_registry=registry,
    )

    assert not check.valid
    assert "required artifact contract mismatch" in check.reasons


def test_manifest_rejects_gate_producer_tree_substitution(tmp_path: Path) -> None:
    repo, manifest, registry, command = _producer_bound_ctest_manifest(tmp_path)
    manifest["gates"]["results"]["full_ctest"]["producerTreeId"] = "release-shared"
    _set_manifest_payload_digest(manifest)

    check = release_identity.verify_snapshot(
        manifest,
        repo,
        build_dirs=[repo / "test-shared-gcc"],
        authority_paths=[],
        commands=[command],
        tree_registry=registry,
    )

    assert not check.valid
    assert "required artifact gate producer mismatch: full_ctest_report" in check.reasons


def test_manifest_rejects_self_consistent_artifact_schema_substitution(
    tmp_path: Path,
) -> None:
    repo, manifest, registry, command = _producer_bound_ctest_manifest(tmp_path)
    report = repo / "test-shared-gcc" / "evidence" / "full-ctest.xml"
    report.write_text("not junit\n", encoding="utf-8")
    manifest["artifacts"]["full_ctest_report"]["sha256"] = (
        release_identity.file_sha256(report)
    )
    _set_manifest_payload_digest(manifest)

    check = release_identity.verify_snapshot(
        manifest,
        repo,
        build_dirs=[repo / "test-shared-gcc"],
        authority_paths=[],
        commands=[command],
        tree_registry=registry,
    )

    assert not check.valid
    assert "required artifact schema mismatch: full_ctest_report" in check.reasons


def _artifact_fixture_content(kind: str) -> str:
    if kind == "json":
        return '{"schema":"fixture"}\n'
    if kind == "junit":
        return "<testsuite tests='1'/>\n"
    if kind == "xml":
        return "<abi-corpus/>\n"
    if kind == "html":
        return "<!doctype html><html></html>\n"
    return "release evidence\n"


def test_full_manifest_fixture_binds_every_fixed_required_artifact(
    tmp_path: Path,
) -> None:
    repo = _init_identity_repo(tmp_path)
    trees = {
        "release-shared": repo / "build" / "release-shared",
        "release-static": repo / "build" / "release-static",
        "test-shared-gcc": repo / "build" / "test-shared-gcc",
        "test-shared-clang": repo / "build" / "test-shared-clang",
        "asan-ubsan-lsan": repo / "build" / "asan-ubsan-lsan",
        "tsan": repo / "build" / "tsan",
    }
    for tree_id, path in trees.items():
        _write_tree_capability_cache(path, tree_id)
    argv = ["--tier", "full", "--manifest", str(repo / "artifacts" / "full.json")]
    for tree_id, flag in (
        ("release-shared", "--release-shared-build-dir"),
        ("release-static", "--release-static-build-dir"),
        ("test-shared-gcc", "--test-shared-gcc-build-dir"),
        ("test-shared-clang", "--test-shared-clang-build-dir"),
        ("asan-ubsan-lsan", "--asan-ubsan-lsan-build-dir"),
        ("tsan", "--tsan-build-dir"),
    ):
        argv.extend((flag, str(trees[tree_id])))
    args = run_release_gate.build_parser().parse_args(argv)
    run_release_gate._resolve_paths(args, repo)
    steps = run_release_gate._selected_steps(args)
    registry = run_release_gate._tree_registry(args)

    required_ids = {
        artifact_id
        for step in steps
        for artifact_id in step.required_artifact_ids
    }
    for artifact_id in required_ids:
        contract = release_evidence_model.ARTIFACT_CONTRACTS[artifact_id]
        path = Path(registry[contract.tree_id]["path"]) / contract.relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(_artifact_fixture_content(contract.kind), encoding="utf-8")

    commands = [step.command for step in steps]
    start_identity = release_identity.build_identity(
        repo,
        build_dirs=run_release_gate._identity_build_dirs(args),
        authority_paths=[],
        commands=commands,
        tree_registry=registry,
    )
    results = {
        step.name: {
            "result": "pass",
            "returncode": 0,
            "command": step.command,
            "producerTreeId": step.producer_tree_id,
        }
        for step in steps
    }

    check = run_release_gate._write_gate_manifest(
        args,
        repo,
        steps,
        [],
        results,
        start_identity,
    )

    assert check.valid
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    assert manifest["abiMode"] == "none"
    assert set(manifest["evidence_contract"]["artifact_ids"]) == required_ids
    assert required_ids <= set(manifest["artifacts"])
