"""验证 CI 的正式输入可随检出获取，不依赖本地代理工作区。"""

from pathlib import Path
import shutil
import subprocess
import sys

import pytest

from scripts import release_identity, run_release_gate
from scripts.uce_gate.candidate import capture_candidate_fingerprint
from scripts.validate_policy_violations_dictionary import _scan_targets


_SOURCE_ROOT = Path(__file__).resolve().parents[1]
_RELEASE_CONTRACT = "docs/arch/2.0.0-hard-break-release-contract.md"
_DICTIONARIES = (
    "docs/uce/policy_violations_dictionary.json",
    "docs/uce/policy_violations_dictionary.md",
)


@pytest.fixture
def ci_checkout(tmp_path: Path) -> Path:
    """从仅含正式 CI 输入的临时仓库执行真实克隆。"""
    repo = tmp_path / "source"
    repo.mkdir()
    shutil.copytree(
        _SOURCE_ROOT / "scripts", repo / "scripts",
        ignore=shutil.ignore_patterns("__pycache__"),
    )
    paths = {".gitignore", _RELEASE_CONTRACT, *_DICTIONARIES}
    paths.update(target.path.relative_to(_SOURCE_ROOT).as_posix()
                 for target in _scan_targets(_SOURCE_ROOT))
    for relative in sorted(paths):
        destination = repo / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(_SOURCE_ROOT / relative, destination)
    for arguments in (
        ["init", "-q"], ["add", "--", "."],
        ["-c", "user.name=CI Fixture", "-c", "user.email=ci@example.invalid",
         "commit", "-qm", "fixture inputs"],
    ):
        subprocess.run(["git", *arguments], cwd=repo, check=True, capture_output=True)
    checkout = tmp_path / "checkout"
    subprocess.run(
        ["git", "clone", "--quiet", "--no-local", str(repo), str(checkout)],
        check=True, capture_output=True,
    )
    return checkout


def _run_dictionary(checkout: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "scripts/validate_policy_violations_dictionary.py"],
        cwd=checkout, check=False, capture_output=True, text=True,
    )


def test_default_release_authority_is_the_versioned_contract() -> None:
    assert release_identity.AUTHORITY_RELATIVE_PATHS == (_RELEASE_CONTRACT,)


def test_local_workspace_is_fully_ignored(tmp_path: Path) -> None:
    subprocess.run(["git", "init", "-q", str(tmp_path)], check=True, capture_output=True)
    shutil.copy2(_SOURCE_ROOT / ".gitignore", tmp_path / ".gitignore")
    for relative in (
        ".helloagents/context.md",
        ".helloagents/modules/policy_violations_dictionary.json",
        ".helloagents/plans/202609021948_qcurl_overdesign_compat_cleanup_remediation/plan.md",
        ".helloagents/sessions/fixture/STATE.md",
    ):
        result = subprocess.run(
            ["git", "check-ignore", "--no-index", "--", relative],
            cwd=tmp_path, check=False, capture_output=True,
        )
        assert result.returncode == 0, relative


def test_release_cli_rejects_obsolete_local_contract_option() -> None:
    with pytest.raises(SystemExit) as error:
        run_release_gate.build_parser().parse_args(["--contract-json", "local-plan.json"])
    assert error.value.code == 2


def test_ci_contracts_work_in_clean_checkout(ci_checkout: Path) -> None:
    assert not (ci_checkout / ".helloagents").exists()
    status = subprocess.run(
        ["git", "status", "--porcelain"], cwd=ci_checkout,
        check=True, capture_output=True, text=True,
    )
    assert status.stdout == ""
    result = _run_dictionary(ci_checkout)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "scan target not found" not in result.stderr
    fingerprint = capture_candidate_fingerprint(ci_checkout)
    assert [entry["path"] for entry in fingerprint["authority"]["files"]] == [_RELEASE_CONTRACT]
    identity = release_identity.build_identity(
        ci_checkout, build_dirs=[], commands=[],
        authority_paths=release_identity.default_authority_paths(ci_checkout),
    )
    assert [entry["path"] for entry in identity["authority"]["entries"]] == [_RELEASE_CONTRACT]


@pytest.mark.parametrize("relative", _DICTIONARIES)
def test_dictionary_rejects_missing_repository_input(ci_checkout: Path, relative: str) -> None:
    (ci_checkout / relative).unlink()
    result = _run_dictionary(ci_checkout)
    assert result.returncode != 0
    assert relative in result.stderr


def test_release_and_uce_reject_missing_formal_contract(ci_checkout: Path) -> None:
    (ci_checkout / _RELEASE_CONTRACT).unlink()
    with pytest.raises(RuntimeError, match="权威文件"):
        capture_candidate_fingerprint(ci_checkout)
    with pytest.raises(ValueError, match="authority"):
        release_identity.build_identity(
            ci_checkout, build_dirs=[], commands=[],
            authority_paths=release_identity.default_authority_paths(ci_checkout),
        )


def test_local_plan_changes_do_not_change_candidate_identity(ci_checkout: Path) -> None:
    before = capture_candidate_fingerprint(ci_checkout)
    local_plan = ci_checkout / ".helloagents/plans/fixture/requirements.md"
    local_plan.parent.mkdir(parents=True)
    local_plan.write_text("本地执行笔记\n", encoding="utf-8")
    assert capture_candidate_fingerprint(ci_checkout) == before
