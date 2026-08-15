"""Tests for staged public consumer fixture contracts."""

from __future__ import annotations

from pathlib import Path

import pytest

from tests.public_api.consumer_contracts import validate_metatype_fixture


REPO_ROOT = Path(__file__).resolve().parents[2]


def test_metatype_fixture_requires_typed_queued_delivery(tmp_path: Path) -> None:
    """The fixture must exercise a real typed queued signal delivery."""

    source_dir = tmp_path / "consumer_metatype_smoke"
    source_dir.mkdir()
    source = (
        REPO_ROOT / "tests" / "public_api" / "consumer_metatype_smoke" / "main.cpp"
    ).read_text(encoding="utf-8")
    (source_dir / "main.cpp").write_text(
        source.replace("Qt::QueuedConnection", "Qt::DirectConnection"),
        encoding="utf-8",
    )

    with pytest.raises(RuntimeError, match="typed queued"):
        validate_metatype_fixture(source_dir)


def test_metatype_fixture_requires_initialize_before_first_connection(tmp_path: Path) -> None:
    """QCurl initialization must precede the first queued connection."""

    source_dir = tmp_path / "consumer_metatype_smoke"
    source_dir.mkdir()
    source = (
        REPO_ROOT / "tests" / "public_api" / "consumer_metatype_smoke" / "main.cpp"
    ).read_text(encoding="utf-8")
    source = source.replace("    QCurl::initialize();\n\n", "", 1)
    source = source.replace(
        "    if (!connection) {",
        "    QCurl::initialize();\n\n    if (!connection) {",
        1,
    )
    (source_dir / "main.cpp").write_text(source, encoding="utf-8")

    with pytest.raises(RuntimeError, match="before the first Qt connection"):
        validate_metatype_fixture(source_dir)
