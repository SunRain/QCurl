"""Protect hard-break checks when current documentation changes location."""

from argparse import Namespace
from pathlib import Path

import pytest

from scripts import release_metadata_gate
from tests.public_api import run_public_api_checks as public_api


@pytest.mark.parametrize(
    "relative,content,rule",
    [
        (
            "CHANGELOG.md",
            "reply->setWriteCallback();",
            "removed QCNetworkReply callback setter call",
        ),
        ("CHANGELOG.md", "manager.scheduler();", "removed manager scheduler workflow"),
        (
            "CHANGELOG.md",
            "void release(QCWebSocket *socket);",
            "old QCWebSocketPool void mutator contract",
        ),
        (
            "CHANGELOG.md",
            "release(QCWebSocket *socket);",
            "removed QCWebSocketPool pointer release contract",
        ),
        ("CHANGELOG.md", "three-tree release", "legacy three-tree release topology"),
        (
            "CHANGELOG.md",
            "python3 scripts/run_release_gate.py --build-dir build",
            "legacy release gate build-dir option",
        ),
        (
            "CHANGELOG.md",
            "cmake -S . -B build-static -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=OFF",
            "unsupported static testing command",
        ),
        (
            "docs/dev/architecture/public-header-boundary.md",
            "void open();",
            "old QCWebSocket void command contract",
        ),
        (
            "docs/dev/architecture/public-header-boundary.md",
            "qint64 sendTextMessage();",
            "old QCWebSocket qint64 send contract",
        ),
        (
            "docs/dev/uce/README.md",
            "reply->setWriteCallback();",
            "removed QCNetworkReply callback setter call",
        ),
        (
            "docs/dev/uce/README.md",
            "three-tree release",
            "legacy three-tree release topology",
        ),
        (
            "docs/dev/uce/README.md",
            "cmake -S . -B build-static -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=OFF",
            "unsupported static testing command",
        ),
        (
            "docs/user/migration-2.0.md",
            "manager.scheduler();",
            "removed manager scheduler workflow",
        ),
        (
            "docs/dev/architecture/libcurl-binding-contract.md",
            "reply->setWriteCallback();",
            "removed QCNetworkReply callback setter call",
        ),
        (
            "docs/dev/architecture/overview.md",
            "reply->setWriteCallback();",
            "removed QCNetworkReply callback setter call",
        ),
        (
            "docs/dev/release/2.0.0-hard-break-release-contract.md",
            "manager.scheduler();",
            "removed manager scheduler workflow",
        ),
        (
            "docs/dev/release/2.0.0-hard-break-release-contract.md",
            "cmake -S . -B build-static -DBUILD_TESTING=ON -DQCURL_BUILD_SHARED_LIBS=OFF",
            "unsupported static testing command",
        ),
    ],
)
def test_relocated_docs_reject_previous_contract_violations(
    tmp_path: Path, capsys, relative: str, content: str, rule: str
) -> None:
    path = tmp_path / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content + "\n", encoding="utf-8")

    assert public_api.scan_hard_break_guards(Namespace(repo_root=tmp_path)) == 1
    error = capsys.readouterr().err
    assert f"{relative}:1:" in error
    assert rule in error


@pytest.mark.parametrize(
    "relative",
    [
        "CHANGELOG.md",
        "docs/dev/architecture/public-header-boundary.md",
        "docs/dev/uce/README.md",
        "docs/user/migration-2.0.md",
        "docs/dev/architecture/libcurl-binding-contract.md",
        "docs/dev/architecture/overview.md",
        "docs/dev/release/2.0.0-hard-break-release-contract.md",
    ],
)
def test_relocated_current_documentation_passes_without_old_copies(
    tmp_path: Path, capsys, relative: str
) -> None:
    source = Path(__file__).resolve().parents[2] / relative
    destination = tmp_path / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(source.read_bytes())

    assert public_api.scan_hard_break_guards(Namespace(repo_root=tmp_path)) == 0
    assert capsys.readouterr().err == ""


@pytest.mark.parametrize(
    "relative,expected_status",
    [
        ("docs/user/migration-2.0.md", 0),
        ("docs/dev/archive/1.0/first-stable-release-contract.md", 0),
        ("docs/dev/archive/1.0/first-stable-readiness-report.md", 0),
        ("docs/dev/archive/1.0/1.0.0-release-notes.md", 0),
        ("docs/dev/archive/topics/libcurl-consistency.md", 0),
        ("docs/dev/archive/reviews/2026-09-01-historical-review.md", 0),
        ("CHANGELOG.md", 1),
        ("docs/dev/release/release-procedure.md", 1),
        ("docs/dev/architecture/public-header-boundary.md", 1),
        ("docs/dev/uce/README.md", 1),
        ("docs/dev/archive-old/notes.md", 1),
        ("docs/internal/archived-release/obsolete.md", 1),
        ("docs/reviews/obsolete.md", 1),
        ("docs/arch/1.0-first-stable-release-contract.md", 1),
    ],
)
def test_metadata_retains_history_exemptions_without_exempting_current_docs(
    tmp_path: Path, capsys, relative: str, expected_status: int
) -> None:
    """历史迁移仍可解释旧身份，当前正文和已退出路径不能冒充旧发布线。"""
    for name in ("CMakeLists.txt", "README.md", "docs/dev/architecture/overview.md"):
        baseline = tmp_path / name
        baseline.parent.mkdir(parents=True, exist_ok=True)
        baseline.write_text("QCurl 2.0.0\n", encoding="utf-8")
    destination = tmp_path / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("SOVERSION 1\nQCurl 3.0.0\n", encoding="utf-8")

    assert release_metadata_gate.scan_metadata(tmp_path) == expected_status
    error = capsys.readouterr().err
    if expected_status:
        assert f"{relative}:1: forbidden current release identity" in error
        assert f"{relative}:2: forbidden legacy release identity" in error
    else:
        assert error == ""
