"""Policy/tag/reference controls using synthetic ELF assets, not QCurl qualification."""

from __future__ import annotations

import subprocess
from pathlib import Path

import pytest

from scripts import qcurl_abi_gate, release_compatibility_policy as policy
from scripts import release_evidence_model, run_release_gate
from scripts.qcurl_compatibility_common import CompatibilityError, load_json, write_json
from scripts.release_compatibility_steps import compatibility_steps
from scripts.release_identity import file_sha256
from tests.compatibility.fixture import make_reference_fixture
from tests.release_compatibility_fixture import bind_fixture_identity


def _git(repo: Path, *arguments: str) -> str:
    return subprocess.run(
        ["git", *arguments],
        cwd=repo,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def _repo(root: Path) -> Path:
    repo = root / "repo"
    repo.mkdir()
    _git(repo, "init", "-q")
    _git(repo, "config", "user.name", "Synthetic Fixture")
    _git(repo, "config", "user.email", "fixture@example.invalid")
    return repo


def _commit(repo: Path, version: str, *, tag: bool = True) -> str:
    (repo / "CMakeLists.txt").write_text(
        f"project(QCurl VERSION {version} LANGUAGES CXX)\n",
        encoding="utf-8",
    )
    _git(repo, "add", "CMakeLists.txt")
    _git(repo, "commit", "-qm", f"synthetic {version}")
    head = _git(repo, "rev-parse", "HEAD")
    if tag:
        _git(repo, "tag", f"v{version}")
    assert not _git(repo, "status", "--porcelain")
    return head


def _reference(root: Path, version: str, head: str) -> Path:
    reference, _, _ = make_reference_fixture(root, version=version, head=head)
    identity = {
        "head": head,
        "tracked_patch": {"entries": []},
        "untracked": {"entries": []},
        "submodules": {"entries": [], "digest": "synthetic-clean-submodules"},
    }
    bind_fixture_identity(reference, identity)
    return reference


def _history(
    root: Path, *, releases: tuple[str, ...] = ("2.1.0",), candidate: str = "2.1.1"
):
    repo = _repo(root)
    references = {}
    for version in releases:
        head = _commit(repo, version)
        references[version] = _reference(root / f"asset-{version}", version, head)
    _commit(repo, candidate, tag=False)
    return repo, references


def _arguments(
    repo: Path, mode: str, first: Path | None = None, latest: Path | None = None
):
    command = ["--tier", "full", "--stage", "final", "--abi-mode", mode]
    for role, path in (("first", first), ("latest", latest)):
        if path is not None:
            command.extend((f"--abi-reference-{role}", str(path)))
    args = run_release_gate.build_parser().parse_args(command)
    args.source_root = repo
    args.release_shared_build_dir = repo / "build/release-shared"
    return args


@pytest.mark.parametrize(
    ("version", "mode", "first", "latest", "message"),
    [
        ("2.1.0", "none", False, False, "requires snapshot or current"),
        ("2.1.0", "current", True, True, "first release requires snapshot"),
        ("2.1.0", "snapshot", True, False, "rejects historical"),
        ("2.1.0", "snapshot", False, True, "rejects historical"),
        ("2.1.1", "snapshot", False, False, "patch release requires current"),
        ("2.1.1", "none", False, False, "requires snapshot or current"),
        ("2.1.1", "current", False, False, "requires --abi-reference-first"),
        ("2.1.1", "current", True, False, "requires --abi-reference-first"),
        ("2.1.1", "current", False, True, "requires --abi-reference-first"),
    ],
)
def test_final_version_line_modes_reject_bypass(
    tmp_path: Path,
    version: str,
    mode: str,
    first: bool,
    latest: bool,
    message: str,
) -> None:
    (tmp_path / "CMakeLists.txt").write_text(f"project(QCurl VERSION {version})\n")
    args = _arguments(
        tmp_path,
        mode,
        tmp_path / "first" if first else None,
        tmp_path / "latest" if latest else None,
    )
    with pytest.raises(ValueError, match=message):
        policy.validate_abi_arguments(args, tmp_path)


@pytest.mark.parametrize(
    ("version", "mode"), [("2.1.0", "snapshot"), ("2.1.1", "current")]
)
def test_final_accepts_only_the_candidate_version_mode(
    tmp_path: Path, version: str, mode: str
) -> None:
    (tmp_path / "CMakeLists.txt").write_text(f"project(QCurl VERSION {version})\n")
    reference = tmp_path / "reference" if mode == "current" else None
    args = _arguments(tmp_path, mode, reference, reference)
    policy.validate_abi_arguments(args, tmp_path)
    assert policy.candidate_version(args) == tuple(map(int, version.split(".")))


@pytest.mark.parametrize(
    "arguments",
    [
        ["--stage", "promotion"],
        ["--abi-mode", "promotion-candidate"],
        ["--abi-hardbreak-baseline", "old.xml"],
        ["--abi-hardbreak-report", "report.txt"],
        ["--abi-hardbreak-snapshot", "snapshot.xml"],
    ],
)
def test_release_cli_rejects_obsolete_promotion_arguments(arguments: list[str]) -> None:
    with pytest.raises(SystemExit) as caught:
        run_release_gate.build_parser().parse_args(arguments)
    assert caught.value.code == 2


def test_abi_cli_rejects_removed_promote_entry() -> None:
    with pytest.raises(SystemExit) as caught:
        qcurl_abi_gate.build_parser().parse_args(["promote"])
    assert caught.value.code == 2


def test_local_real_tags_choose_numeric_highest_same_line_patch(tmp_path: Path) -> None:
    repo = _repo(tmp_path)
    for version in ("2.0.0", "2.1.0", "2.1.2", "2.1.10", "2.2.0", "2.1.20"):
        _commit(repo, version)
    _git(repo, "tag", "v2.1.11-rc1")
    _commit(repo, "2.1.12", tag=False)
    assert policy.published_reference_versions(repo, (2, 1, 12)) == [
        (2, 1, 0),
        (2, 1, 2),
        (2, 1, 10),
    ]


def test_published_first_tag_is_required(tmp_path: Path) -> None:
    repo = _repo(tmp_path)
    _commit(repo, "2.1.1")
    _commit(repo, "2.1.2", tag=False)
    with pytest.raises(ValueError, match="published first reference tag is missing"):
        policy.published_reference_versions(repo, (2, 1, 2))


def test_first_equals_latest_is_validated_then_deduplicated_as_first(
    tmp_path: Path,
) -> None:
    repo, refs = _history(tmp_path)
    reference = refs["2.1.0"]
    args = _arguments(repo, "current", reference, reference)
    records = policy.historical_references(args, repo)
    assert [(role, path) for role, path, _ in records] == [("first", reference)]
    inputs = policy.reference_input_records(args, repo)
    assert set(inputs) == {"first"}
    assert inputs["first"]["files"][0] == {
        "path": "reference.json",
        "sha256": file_sha256(reference / "reference.json"),
    }
    assert {step.name for step in compatibility_steps(args)} == {
        "compatibility_reference_capture",
        "compatibility_reference_roundtrip",
        "compatibility_first_check",
        "compatibility_first_source",
    }


def test_first_and_latest_highest_patch_are_independently_selected(
    tmp_path: Path,
) -> None:
    repo, refs = _history(
        tmp_path, releases=("2.1.0", "2.1.1", "2.1.3"), candidate="2.1.4"
    )
    args = _arguments(repo, "current", refs["2.1.0"], refs["2.1.3"])
    assert [
        (role, payload["version"])
        for role, _, payload in policy.historical_references(args, repo)
    ] == [
        ("first", "2.1.0"),
        ("latest", "2.1.3"),
    ]
    args.abi_reference_latest = refs["2.1.1"]
    with pytest.raises(
        ValueError, match="latest reference must be published version 2.1.3"
    ):
        policy.historical_references(args, repo)


def test_same_release_different_reference_contents_cannot_be_deduplicated(
    tmp_path: Path,
) -> None:
    repo, refs = _history(tmp_path)
    first = refs["2.1.0"]
    latest = _reference(
        tmp_path / "another-asset", "2.1.0", _git(repo, "rev-parse", "v2.1.0")
    )
    args = _arguments(repo, "current", first, latest)
    with pytest.raises(ValueError, match="same release have different contents"):
        policy.historical_references(args, repo)


@pytest.mark.parametrize(
    "change",
    ["wrong-line", "candidate-head", "candidate-tag", "dirty", "latest-corrupt-bytes"],
)
def test_historical_inputs_cannot_substitute_wrong_or_mutated_assets(
    tmp_path: Path, change: str
) -> None:
    repo, refs = _history(tmp_path)
    first = refs["2.1.0"]
    latest = first
    if change == "wrong-line":
        latest = _reference(
            tmp_path / "wrong", "2.2.0", _git(repo, "rev-parse", "v2.1.0")
        )
    elif change in {"candidate-head", "candidate-tag"}:
        if change == "candidate-tag":
            _git(repo, "tag", "-f", "v2.1.0", "HEAD")
        payload = load_json(first / "reference.json")
        payload["identity"]["head"] = _git(repo, "rev-parse", "HEAD")
        write_json(first / "reference.json", payload)
    elif change == "dirty":
        payload = load_json(first / "reference.json")
        payload["identity"]["tracked_patch"]["entries"] = [{"path": "dirty.cpp"}]
        write_json(first / "reference.json", payload)
    else:
        latest = _reference(
            tmp_path / "latest", "2.1.0", _git(repo, "rev-parse", "v2.1.0")
        )
        library = latest / "install/lib/libQCurlOtherExtras.so.2.1.0"
        library.write_bytes(library.read_bytes() + b"corrupted reference bytes")
    args = _arguments(repo, "current", first, latest)
    with pytest.raises(
        (ValueError, CompatibilityError),
        match="published version|historical tag|clean candidate|content changed",
    ):
        policy.historical_references(args, repo)


def _assert_comparison_artifacts(step, role: str) -> None:
    assert set(step.required_artifact_ids) == {
        f"compatibility_{role}_{name}"
        for name in (
            "report",
            "core_full",
            "core_residual",
            "core_comparison",
            "other_extras_full",
            "other_extras_residual",
            "other_extras_comparison",
        )
    }


def test_patch_steps_cover_both_abis_snapshots_old_binary_and_old_source(
    tmp_path: Path,
) -> None:
    repo, refs = _history(tmp_path, releases=("2.1.0", "2.1.2"), candidate="2.1.3")
    args = _arguments(repo, "current", refs["2.1.0"], refs["2.1.2"])
    steps = compatibility_steps(args)
    by_name = {step.name: step for step in steps}
    assert set(by_name) == {
        "compatibility_reference_capture",
        "compatibility_reference_roundtrip",
        "compatibility_first_check",
        "compatibility_first_source",
        "compatibility_latest_check",
        "compatibility_latest_source",
    }
    assert set(by_name["compatibility_reference_capture"].required_artifact_ids) == {
        "compatibility_reference",
        "compatibility_reference_validation",
        "abi_core_snapshot",
        "abi_other_extras_snapshot",
    }
    roundtrip = by_name["compatibility_reference_roundtrip"]
    assert "--require-history" not in roundtrip.command
    _assert_comparison_artifacts(roundtrip, "roundtrip")
    for role in ("first", "latest"):
        check = by_name[f"compatibility_{role}_check"]
        assert "--require-history" in check.command
        _assert_comparison_artifacts(check, role)
        source = by_name[f"compatibility_{role}_source"]
        assert "rebuild-source" in source.command
        assert source.required_artifact_ids == (f"compatibility_{role}_source",)
        assert all(
            step.producer_tree_id == "release-shared" for step in (check, source)
        )
    for step in steps:
        for artifact_id in step.required_artifact_ids:
            contract = release_evidence_model.ARTIFACT_CONTRACTS[artifact_id]
            assert contract.gate == step.name
            assert contract.tree_id == "release-shared"
