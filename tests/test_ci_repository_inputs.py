"""验证 CI 的正式输入可随检出获取，不依赖本地代理工作区。"""

from pathlib import Path
import shutil
import subprocess

import pytest

from scripts import release_identity, run_release_gate


_SOURCE_ROOT = Path(__file__).resolve().parents[1]
_RELEASE_CONTRACT = "docs/arch/2.0.0-hard-break-release-contract.md"
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
