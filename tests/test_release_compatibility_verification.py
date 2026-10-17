"""Independent verifier controls; synthetic ELF fixtures are not patch releases."""

from __future__ import annotations

from copy import deepcopy
from pathlib import Path

import pytest

from scripts import release_identity, run_release_gate
from scripts.qcurl_compatibility_common import inventory, load_json, write_json
from scripts.qcurl_compatibility_reference import INPUT_DIRECTORIES
from scripts.release_compatibility_verification import compatibility_reasons
from scripts.release_compatibility_policy import require_clean_identity
from tests.compatibility.fixture import make_reference_fixture
from tests.release_compatibility_fixture import bind_fixture_identity
from tests.test_release_final_policy import _verify
from tests.test_release_gate_unit import _full_manifest_fixture, _init_identity_repo
from tests.test_release_identity import _git


def _root(fixture) -> Path:
    return fixture[1].release_shared_build_dir / "evidence/compatibility"


def _reasons(fixture) -> list[str]:
    repo, args, _, manifest = fixture
    return compatibility_reasons(
        manifest, repo, run_release_gate._tree_registry(args), args=args
    )


@pytest.mark.parametrize("section", ["tracked_patch", "untracked", "submodules"])
def test_clean_history_requires_explicit_source_sections(section: str) -> None:
    identity = {
        "head": "a" * 40,
        "tracked_patch": {"entries": []},
        "untracked": {"entries": []},
        "submodules": {"entries": []},
    }
    identity.pop(section)
    with pytest.raises(
        ValueError, match="complete source identity|recursive submodules"
    ):
        require_clean_identity(identity)


@pytest.mark.parametrize(
    "name",
    [
        "install/include/qcurl/QCurlConfig.h",
        "install/lib/libQCurl.so.2.1.0",
        "install/lib/libQCurlOtherExtras.so.2.1.0",
        "bin/Core",
        "bin/TestSupport",
        "sources/Core/main.cpp",
    ],
)
def test_verifier_rechecks_actual_retained_inputs_not_only_reference_index(
    tmp_path: Path, name: str
) -> None:
    fixture = _full_manifest_fixture(tmp_path)
    path = _root(fixture) / "reference" / name
    path.write_bytes(path.read_bytes() + b"\nchanged retained input\n")
    reasons = _reasons(fixture)
    assert any(
        "reference input content changed" in reason for reason in reasons
    ), reasons


def test_reindexed_reference_cannot_substitute_a_different_installed_header(
    tmp_path: Path,
) -> None:
    fixture = _full_manifest_fixture(tmp_path)
    reference = _root(fixture) / "reference"
    header = reference / "install/include/qcurl/QCurlConfig.h"
    header.write_text(
        header.read_text(encoding="utf-8") + "// changed installed input\n",
        encoding="utf-8",
    )
    payload = load_json(reference / "reference.json")
    payload["files"] = inventory(reference, INPUT_DIRECTORIES, ("validation.json",))
    write_json(reference / "reference.json", payload)
    reasons = _reasons(fixture)
    assert any(
        "capture installed inputs differ" in reason for reason in reasons
    ), reasons


@pytest.mark.parametrize(
    "mutation",
    ["classification", "rebuilt", "tuple", "bridge", "binary", "other-result"],
)
def test_roundtrip_report_cannot_self_declare_missing_or_different_evidence(
    tmp_path: Path, mutation: str
) -> None:
    fixture = _full_manifest_fixture(tmp_path)
    path = _root(fixture) / "roundtrip/report.json"
    report = load_json(path)
    if mutation == "classification":
        report.update(classification="PATCH_COMPATIBLE", patchCompatibility=True)
    elif mutation == "rebuilt":
        report["sourceRebuild"] = "rebuilt-with-candidate"
    elif mutation == "tuple":
        report["tuple"]["compilerVersion"] = "different"
    elif mutation == "bridge":
        report["candidateRun"]["TestSupport"]["testSupportBridge"] = False
    elif mutation == "binary":
        report["candidateRun"]["Core"]["binarySha256"] = "0" * 64
    else:
        report["abi"].pop("OtherExtras")
    write_json(path, report)
    assert _reasons(fixture), mutation


def test_loader_trace_is_parsed_even_if_its_recorded_digest_is_updated(
    tmp_path: Path,
) -> None:
    fixture = _full_manifest_fixture(tmp_path)
    root = _root(fixture)
    path = root / "roundtrip/report.json"
    report = load_json(path)
    loader = root / "roundtrip/logs/candidate/OtherExtras.loader.log"
    loader.write_text(
        loader.read_text(encoding="utf-8").replace(
            "libQCurlOtherExtras.so", "libWrongOtherExtras.so"
        ),
        encoding="utf-8",
    )
    report["candidateRun"]["OtherExtras"]["loaderSha256"] = (
        release_identity.file_sha256(loader)
    )
    write_json(path, report)
    reasons = _reasons(fixture)
    assert any(
        "loaded QCurl libraries" in reason or "load" in reason for reason in reasons
    ), reasons


@pytest.mark.parametrize("component", ["core", "other-extras"])
def test_residual_abi_change_cannot_be_accepted_by_a_compatible_boolean(
    tmp_path: Path, component: str
) -> None:
    fixture = _full_manifest_fixture(tmp_path)
    path = _root(fixture) / "roundtrip/abi" / f"{component}.comparison.json"
    report = load_json(path)
    report["residual"]["returncode"] = 4
    report["compatible"] = True
    write_json(path, report)
    reasons = _reasons(fixture)
    assert any("ABI comparison did not pass" in reason for reason in reasons), reasons


def _history(tmp_path: Path, *, distinct: bool) -> tuple[Path, tuple[Path, Path]]:
    repo = _init_identity_repo(tmp_path)
    versions = ("2.1.0", "2.1.1") if distinct else ("2.1.0",)
    references = []
    for version in versions:
        if version != "2.1.0":
            (repo / "CMakeLists.txt").write_text(
                f"project(QCurl VERSION {version})\n", encoding="utf-8"
            )
            _git(repo, "add", "CMakeLists.txt")
            _git(repo, "commit", "-qm", f"synthetic {version}")
        _git(repo, "tag", f"v{version}")
        head = _git(repo, "rev-parse", "HEAD")
        reference, _, _ = make_reference_fixture(
            tmp_path / f"asset-{version}", version=version, head=head
        )
        bind_fixture_identity(
            reference,
            {
                "head": head,
                "tracked_patch": {"entries": []},
                "untracked": {"entries": []},
                "submodules": {"entries": []},
            },
        )
        references.append(reference)
    candidate = "2.1.2" if distinct else "2.1.1"
    (repo / "CMakeLists.txt").write_text(
        f"project(QCurl VERSION {candidate})\n", encoding="utf-8"
    )
    _git(repo, "add", "CMakeLists.txt")
    _git(repo, "commit", "-qm", f"synthetic candidate {candidate}")
    return repo, (references[0], references[-1])


@pytest.mark.parametrize("distinct", [False, True])
def test_current_manifest_requires_each_historical_gate_and_artifact(
    tmp_path: Path, distinct: bool
) -> None:
    repo, historical = _history(tmp_path, distinct=distinct)
    fixture = _full_manifest_fixture(tmp_path, repo=repo, historical=historical)
    original = fixture[3]
    roles = {"first", "latest"} if distinct else {"first"}
    assert set(original["compatibilityInputs"]) == roles
    assert _verify(fixture) == 0
    required = {
        artifact
        for step in fixture[2]
        for artifact in step.required_artifact_ids
        if step.name
        in {
            f"compatibility_{role}_{kind}"
            for role in roles
            for kind in ("check", "source")
        }
    }
    assert len(required) == len(roles) * 8
    for artifact in sorted(required):
        manifest = deepcopy(original)
        manifest["artifacts"].pop(artifact)
        assert _verify(fixture, manifest) == 1, artifact
    for role in roles:
        for suffix in ("check", "source"):
            manifest = deepcopy(original)
            manifest["gates"]["results"].pop(f"compatibility_{role}_{suffix}")
            assert _verify(fixture, manifest) == 1
