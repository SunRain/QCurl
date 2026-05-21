from __future__ import annotations

from argparse import Namespace

from tests.public_api import run_public_api_checks as public_api


def test_check_blocking_extras_install_requires_opt_in_headers(tmp_path, capsys) -> None:
    default_include = tmp_path / "default" / "include" / "qcurl"
    blocking_include = tmp_path / "blocking" / "include" / "qcurl"
    default_include.mkdir(parents=True)
    blocking_include.mkdir(parents=True)
    (blocking_include / "QCBlockingNetworkClient.h").write_text("", encoding="utf-8")
    manifest = tmp_path / "blocking.txt"
    manifest.write_text("QCBlockingNetworkClient.h\n", encoding="utf-8")

    rc = public_api.check_blocking_extras_install(
        Namespace(
            default_stage_dir=tmp_path / "default",
            blocking_stage_dir=tmp_path / "blocking",
            manifest=manifest,
        )
    )

    assert rc == 0
    assert "blocking extras install contract passed" in capsys.readouterr().out

def test_check_blocking_extras_install_rejects_default_leak(tmp_path, capsys) -> None:
    default_include = tmp_path / "default" / "include" / "qcurl"
    blocking_include = tmp_path / "blocking" / "include" / "qcurl"
    default_include.mkdir(parents=True)
    blocking_include.mkdir(parents=True)
    (default_include / "QCBlockingNetworkClient.h").write_text("", encoding="utf-8")
    (blocking_include / "QCBlockingNetworkClient.h").write_text("", encoding="utf-8")
    manifest = tmp_path / "blocking.txt"
    manifest.write_text("QCBlockingNetworkClient.h\n", encoding="utf-8")

    rc = public_api.check_blocking_extras_install(
        Namespace(
            default_stage_dir=tmp_path / "default",
            blocking_stage_dir=tmp_path / "blocking",
            manifest=manifest,
        )
    )

    assert rc == 1
    assert "leaked into default Core stage" in capsys.readouterr().err

def test_check_test_support_install_requires_opt_in_headers(tmp_path, capsys) -> None:
    default_include = tmp_path / "default" / "include" / "qcurl"
    test_support_include = tmp_path / "test_support" / "include" / "qcurl"
    default_include.mkdir(parents=True)
    test_support_include.mkdir(parents=True)
    (test_support_include / "QCNetworkMockHandler.h").write_text("", encoding="utf-8")
    (test_support_include / "QCNetworkTestSupport.h").write_text("", encoding="utf-8")
    manifest = tmp_path / "test_support.txt"
    manifest.write_text("QCNetworkMockHandler.h\nQCNetworkTestSupport.h\n", encoding="utf-8")

    rc = public_api.check_test_support_install(
        Namespace(
            default_stage_dir=tmp_path / "default",
            test_support_stage_dir=tmp_path / "test_support",
            manifest=manifest,
        )
    )

    assert rc == 0
    assert "test support install contract passed" in capsys.readouterr().out

def test_check_test_support_install_rejects_default_leak(tmp_path, capsys) -> None:
    default_include = tmp_path / "default" / "include" / "qcurl"
    test_support_include = tmp_path / "test_support" / "include" / "qcurl"
    default_include.mkdir(parents=True)
    test_support_include.mkdir(parents=True)
    (default_include / "QCNetworkMockHandler.h").write_text("", encoding="utf-8")
    (test_support_include / "QCNetworkMockHandler.h").write_text("", encoding="utf-8")
    manifest = tmp_path / "test_support.txt"
    manifest.write_text("QCNetworkMockHandler.h\n", encoding="utf-8")

    rc = public_api.check_test_support_install(
        Namespace(
            default_stage_dir=tmp_path / "default",
            test_support_stage_dir=tmp_path / "test_support",
            manifest=manifest,
        )
    )

    assert rc == 1
    assert "leaked into default Core stage" in capsys.readouterr().err

def test_check_other_extras_install_requires_opt_in_headers(tmp_path, capsys) -> None:
    default_include = tmp_path / "default" / "include" / "qcurl"
    other_extras_include = tmp_path / "other_extras" / "include" / "qcurl"
    default_include.mkdir(parents=True)
    other_extras_include.mkdir(parents=True)
    (other_extras_include / "QCNetworkDiagnostics.h").write_text("", encoding="utf-8")
    manifest = tmp_path / "other_extras.txt"
    manifest.write_text("QCNetworkDiagnostics.h\n", encoding="utf-8")

    rc = public_api.check_other_extras_install(
        Namespace(
            default_stage_dir=tmp_path / "default",
            other_extras_stage_dir=tmp_path / "other_extras",
            manifest=manifest,
        )
    )

    assert rc == 0
    assert "other extras install contract passed" in capsys.readouterr().out

def test_check_other_extras_install_rejects_default_leak(tmp_path, capsys) -> None:
    default_include = tmp_path / "default" / "include" / "qcurl"
    other_extras_include = tmp_path / "other_extras" / "include" / "qcurl"
    default_include.mkdir(parents=True)
    other_extras_include.mkdir(parents=True)
    (default_include / "QCNetworkDiagnostics.h").write_text("", encoding="utf-8")
    (other_extras_include / "QCNetworkDiagnostics.h").write_text("", encoding="utf-8")
    manifest = tmp_path / "other_extras.txt"
    manifest.write_text("QCNetworkDiagnostics.h\n", encoding="utf-8")

    rc = public_api.check_other_extras_install(
        Namespace(
            default_stage_dir=tmp_path / "default",
            other_extras_stage_dir=tmp_path / "other_extras",
            manifest=manifest,
        )
    )

    assert rc == 1
    assert "leaked into default Core stage" in capsys.readouterr().err


