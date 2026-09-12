"""锁定 libcurl_consistency 目录级 conftest 的管辖边界。

回归背景：pytest 会把**整个 session** 的 items 传给目录级 conftest 的
`pytest_collection_modifyitems`。该 hook 曾对全部 items 做文件名白名单判断，把仓库其它
目录的测试误判为需要 curl testenv；环境缺失时 `pytest.exit(returncode=2)` 会连带终止整个
session，使 `pytest tests/` 这类混合运行完全无法进行。

本测试用子进程做端到端验证：只要边界回退，混合运行就会再次被终止而立刻失败。
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]

# 一个目录外的纯单元测试 + 一个 libcurl_consistency 目录内的纯单元测试。
# 两者都不需要 curl testenv，混合收集必须成功。
_OUTSIDE_MODULE = "tests/test_uce_manifest.py"
_INSIDE_UNIT_MODULE = "tests/libcurl_consistency/test_compare_unit.py"


def _collect_only(*targets: str) -> subprocess.CompletedProcess[str]:
    """在干净环境下收集指定目标，不注入任何 testenv 变量。"""

    return subprocess.run(
        [sys.executable, "-m", "pytest", "--collect-only", "-q", "-p", "no:cacheprovider", *targets],
        cwd=_REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=180,
    )


def test_mixed_run_is_not_terminated_by_consistency_conftest() -> None:
    """目录外测试与目录内单元测试混合收集时，session 不得被 pytest.exit 终止。"""

    result = _collect_only(_OUTSIDE_MODULE, _INSIDE_UNIT_MODULE)

    combined = result.stdout + result.stderr
    assert "libcurl consistency 环境无效" not in combined, (
        "目录级 conftest 越界：目录外测试被误判为需要 curl testenv\n" + combined
    )
    assert result.returncode == 0, combined


def test_outside_module_alone_is_unaffected() -> None:
    """仅收集目录外测试时，libcurl_consistency 的环境要求完全不参与。"""

    result = _collect_only(_OUTSIDE_MODULE)

    combined = result.stdout + result.stderr
    assert "libcurl consistency" not in combined, combined
    assert result.returncode == 0, combined


def test_consistency_unit_module_alone_is_unaffected() -> None:
    """目录内的纯单元测试不受 testenv 环境要求绑架。"""

    result = _collect_only(_INSIDE_UNIT_MODULE)

    combined = result.stdout + result.stderr
    assert "libcurl consistency 环境无效" not in combined, combined
    assert result.returncode == 0, combined


def test_missing_environment_fails_before_testenv_import_errors() -> None:
    """缺失一致性环境时只报告一个可读门禁错误，不暴露导入级噪声。"""

    result = _collect_only("tests/libcurl_consistency")

    combined = result.stdout + result.stderr
    assert result.returncode == 2, combined
    assert "libcurl consistency 环境无效:" in combined, combined
    assert "缺少必需环境变量 `CURL_BUILD_DIR`" in combined, combined
    assert "ModuleNotFoundError: No module named 'testenv'" not in combined, combined


def test_conftest_declares_its_scope_boundary() -> None:
    """conftest 必须显式声明管辖边界常量并在 hook 中据此过滤。

    这是对实现方式的最小约束：边界一旦被删除，端到端测试的失败原因会难以定位。
    """

    conftest = (_REPO_ROOT / "tests" / "libcurl_consistency" / "conftest.py").read_text(
        encoding="utf-8"
    )

    assert "_LC_TEST_DIR" in conftest, "缺少管辖边界常量 _LC_TEST_DIR"
    assert "_LC_TEST_DIR not in item_path.parents" in conftest, (
        "pytest_collection_modifyitems 未按 _LC_TEST_DIR 过滤 items"
    )
