from __future__ import annotations

import json
from pathlib import Path

from scripts import qcurl_abi_gate
from scripts import run_release_gate


def test_hardbreak_abidiff_returncode_accepts_only_abi_change_bits() -> None:
    assert qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(0)
    assert qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(4)
    assert qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(8)
    assert qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(12)

    assert not qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(1)
    assert not qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(2)
    assert not qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(5)
    assert not qcurl_abi_gate.abidiff_returncode_is_hardbreak_evidence(13)


def test_release_gate_inserts_archived_abi_comparison_before_current_baseline(
    tmp_path: Path,
    capsys,
) -> None:
    result = run_release_gate.main(
        [
            "--tier",
            "full",
            "--build-dir",
            str(tmp_path / "build"),
            "--static-build-dir",
            str(tmp_path / "build-static"),
            "--abi-hardbreak-baseline",
            str(tmp_path / "previous.abi.xml"),
            "--dry-run",
        ]
    )

    assert result == 0

    plan = json.loads(capsys.readouterr().out)
    steps = plan["steps"]
    names = [step["name"] for step in steps]

    assert "abi_hardbreak_report" in names
    assert "abi_current_baseline_diff" in names
    assert names.index("abi_hardbreak_report") < names.index("abi_current_baseline_diff")

    hardbreak_step = steps[names.index("abi_hardbreak_report")]
    assert "hardbreak-report" in hardbreak_step["command"]


def test_release_gate_default_uses_single_current_baseline_track(
    tmp_path: Path,
    capsys,
) -> None:
    result = run_release_gate.main(
        [
            "--tier",
            "full",
            "--build-dir",
            str(tmp_path / "build"),
            "--static-build-dir",
            str(tmp_path / "build-static"),
            "--dry-run",
        ]
    )

    assert result == 0

    plan = json.loads(capsys.readouterr().out)
    names = [step["name"] for step in plan["steps"]]

    assert "abi_current_baseline_diff" in names
    assert "abi_hardbreak_report" not in names
