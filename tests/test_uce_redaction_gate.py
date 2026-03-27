from __future__ import annotations

from pathlib import Path

from scripts.uce.redaction_gate import scan_paths


def test_scan_paths_reports_sensitive_headers(tmp_path: Path) -> None:
    scan_root = tmp_path / "evidence"
    scan_root.mkdir()
    (scan_root / "gate.log").write_text("Authorization: Bearer secret-token\n", encoding="utf-8")

    report = scan_paths([scan_root])

    assert report["missing_roots"] == []
    assert report["scanned_files"] == 1
    assert report["violations"][0]["rule"] == "auth_bearer_unredacted"


def test_scan_paths_reports_missing_roots() -> None:
    report = scan_paths([Path("/definitely/missing/root")])

    assert report["missing_roots"] == ["/definitely/missing/root"]
