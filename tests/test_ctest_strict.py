from __future__ import annotations

from types import SimpleNamespace

import pytest

from scripts import ctest_strict


@pytest.mark.parametrize(
    ("result_lines", "incomplete"),
    [
        ("1/1 Test #2: required ........... Passed 0.01 sec\n", []),
        ("1/1 Test #2: required ...........***Not Run (Disabled) 0.00 sec\n", ["required"]),
        ("1/1 Test #2: required ...........***Skipped 0.00 sec\n", ["required"]),
        ("1/1 Test #2: another ............ Passed 0.01 sec\n", ["required"]),
        ("", ["required"]),
        ("1/1 Test #2: required ........... Passed 0.01 sec\n" * 2, ["required"]),
    ],
    ids=["passed", "disabled", "skipped", "wrong-target", "missing", "duplicate"],
)
def test_ctest_target_results_ignore_diagnostic_words(result_lines, incomplete) -> None:
    output = (
        "2: Skipped Disabled Sanitizer: diagnostic text\n"
        "1/1 Test #1: not_selected .......***Skipped 0.00 sec\n"
        + result_lines
    )

    assert ctest_strict.ctest_targets_without_unique_pass(output, ["required"]) == incomplete


def test_ctest_args_detection_handles_common_gate_flags() -> None:
    assert ctest_strict._has_verbose_flag(["-VV"])
    assert ctest_strict._has_verbose_flag(["--verbose"])
    assert ctest_strict._has_label_filter(["-Loffline"])
    assert ctest_strict._has_label_filter(["--label-regex=offline"])
    assert ctest_strict._has_label_filter(["-LE", "external"])
    assert ctest_strict._has_no_tests_flag(["--no-tests=error"])
    assert ctest_strict._has_timeout_flag(["--timeout=30"])
    assert ctest_strict._has_timeout_flag(["--timeout", "30"])


def test_main_adds_evidence_defaults_and_accepts_no_skips(tmp_path, monkeypatch) -> None:
    captured: dict[str, object] = {}

    def fake_run(cmd, **kwargs):  # type: ignore[no-untyped-def]
        captured["cmd"] = cmd
        captured["cwd"] = kwargs.get("cwd")
        return SimpleNamespace(
            returncode=0,
            stdout=(
                "    Start 1: tst_Offline\n"
                "1: Totals: 2 passed, 0 failed, 0 skipped, 0 blacklisted, 10ms\n"
            ),
        )

    monkeypatch.setattr(ctest_strict.subprocess, "run", fake_run)

    rc = ctest_strict.main(["--build-dir", str(tmp_path), "--ctest", "ctest-fake"])

    assert rc == 0
    assert captured["cwd"] == str(tmp_path)
    assert captured["cmd"] == [
        "ctest-fake",
        "-V",
        "--no-tests=error",
        "-L",
        "offline",
        "--timeout",
        "300",
    ]


def test_main_fails_when_qttest_skips_exceed_threshold(tmp_path, monkeypatch, capsys) -> None:
    def fake_run(cmd, **kwargs):  # type: ignore[no-untyped-def]
        return SimpleNamespace(
            returncode=0,
            stdout=(
                "    Start 7: tst_QCNetworkReply\n"
                "7: Totals: 3 passed, 0 failed, 2 skipped, 0 blacklisted, 10ms\n"
            ),
        )

    monkeypatch.setattr(ctest_strict.subprocess, "run", fake_run)

    rc = ctest_strict.main(["--build-dir", str(tmp_path), "--max-skips", "1"])

    assert rc == 3
    assert "skipped_total=2 > max_skips=1" in capsys.readouterr().err


def test_main_returns_ctest_failure_before_skip_policy(tmp_path, monkeypatch, capsys) -> None:
    def fake_run(cmd, **kwargs):  # type: ignore[no-untyped-def]
        return SimpleNamespace(
            returncode=8,
            stdout=(
                "    Start 2: tst_Failing\n"
                "2: Totals: 1 passed, 1 failed, 5 skipped, 0 blacklisted, 10ms\n"
            ),
        )

    monkeypatch.setattr(ctest_strict.subprocess, "run", fake_run)

    rc = ctest_strict.main(["--build-dir", str(tmp_path), "--max-skips", "99"])

    assert rc == 8
    assert "ctest failed: returncode=8" in capsys.readouterr().err
