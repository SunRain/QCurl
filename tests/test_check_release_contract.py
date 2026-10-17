"""Source policy guards for the two runtime libraries and installed version matching."""

from pathlib import Path

import pytest

from scripts.check_release_contract import (
    parse_project_version,
    validate_version_policy,
)
from scripts import check_release_contract

ROOT = Path(__file__).resolve().parents[1]


def _source_fixture(tmp_path: Path) -> Path:
    (tmp_path / "src").mkdir()
    for relative in ("CMakeLists.txt", "src/CMakeLists.txt"):
        (tmp_path / relative).write_bytes((ROOT / relative).read_bytes())
    return tmp_path


def test_current_release_has_shared_major_minor_policy() -> None:
    assert parse_project_version(ROOT) == (2, 1, 0)
    assert validate_version_policy(ROOT) == []


@pytest.mark.parametrize(
    "target,value",
    [
        ("QCurl", "2"),
        ("QCurlOtherExtras", "2"),
        ("QCurl", "2.2"),
        ("QCurlOtherExtras", "2.2"),
    ],
)
def test_each_runtime_rejects_independent_or_legacy_soversion(
    tmp_path: Path, target: str, value: str
) -> None:
    fixture = _source_fixture(tmp_path)
    cmake = fixture / "src/CMakeLists.txt"
    content = cmake.read_text(encoding="utf-8")
    before, after = content.split(
        f"set_target_properties({target} PROPERTIES", maxsplit=1
    )
    after = after.replace("SOVERSION ${QCURL_ABI_VERSION}", f"SOVERSION {value}", 1)
    cmake.write_text(
        before + f"set_target_properties({target} PROPERTIES" + after, encoding="utf-8"
    )
    assert any(
        f"{target} SOVERSION" in error for error in validate_version_policy(fixture)
    )


def test_abi_line_cannot_drop_minor(tmp_path: Path) -> None:
    fixture = _source_fixture(tmp_path)
    cmake = fixture / "CMakeLists.txt"
    cmake.write_text(
        cmake.read_text().replace(
            "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}",
            "${PROJECT_VERSION_MAJOR}",
        ),
        encoding="utf-8",
    )
    assert any(
        "QCURL_ABI_VERSION must derive" in error
        for error in validate_version_policy(fixture)
    )


def test_package_matching_cannot_accept_other_minor_lines(tmp_path: Path) -> None:
    fixture = _source_fixture(tmp_path)
    cmake = fixture / "CMakeLists.txt"
    cmake.write_text(
        cmake.read_text().replace("SameMinorVersion", "SameMajorVersion"),
        encoding="utf-8",
    )
    assert any(
        "SameMinorVersion" in error for error in validate_version_policy(fixture)
    )


def test_changed_curl_checkout_still_fails_release_contract(
    monkeypatch, capsys
) -> None:
    """The ABI policy does not weaken the actual checkout/gitlink pin check."""

    monkeypatch.setattr(
        "sys.argv", ["check_release_contract.py", "--source-root", str(ROOT)]
    )
    monkeypatch.setattr(
        check_release_contract, "current_curl_pin", lambda root: "a" * 40
    )
    monkeypatch.setattr(
        check_release_contract, "desired_curl_pin", lambda root: "b" * 40
    )
    monkeypatch.setattr(
        check_release_contract, "parse_bundled_curl_version", lambda root: "8.19.0"
    )
    assert check_release_contract.main() == 1
    assert "curl submodule checkout" in capsys.readouterr().err
