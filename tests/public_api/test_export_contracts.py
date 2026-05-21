from __future__ import annotations

from argparse import Namespace

from tests.public_api import run_public_api_checks as public_api


def test_check_export_contract_rejects_bundled_libcurl_leak(tmp_path, capsys) -> None:
    stage = tmp_path / "stage"
    target_dir = stage / "lib" / "cmake" / "QCurl"
    target_dir.mkdir(parents=True)
    (target_dir / "QCurlTargets.cmake").write_text(
        "target_link_libraries(QCurl::QCurl INTERFACE QCurl::libcurl_shared)\n",
        encoding="utf-8",
    )

    rc = public_api.check_export_contract(Namespace(stage_dir=stage))

    assert rc == 1
    assert "QCurl::libcurl_shared" in capsys.readouterr().err


def test_check_export_contract_accepts_static_libcurl_with_find_dependency(tmp_path, capsys) -> None:
    stage = tmp_path / "stage"
    target_dir = stage / "lib" / "cmake" / "QCurl"
    target_dir.mkdir(parents=True)
    (target_dir / "QCurlTargets.cmake").write_text(
        "add_library(QCurl::QCurl STATIC IMPORTED)\n"
        "set_target_properties(QCurl::QCurl PROPERTIES "
        'INTERFACE_LINK_LIBRARIES "Qt6::Core;CURL::libcurl;ZLIB::ZLIB")\n',
        encoding="utf-8",
    )
    (target_dir / "QCurlConfig.cmake").write_text(
        "find_dependency(CURL 7.85.0 REQUIRED)\n"
        "find_dependency(ZLIB REQUIRED)\n",
        encoding="utf-8",
    )

    rc = public_api.check_export_contract(Namespace(stage_dir=stage))

    assert rc == 0
    assert "export contract passed" in capsys.readouterr().out


def test_check_export_contract_requires_static_libcurl_find_dependency(tmp_path, capsys) -> None:
    stage = tmp_path / "stage"
    target_dir = stage / "lib" / "cmake" / "QCurl"
    target_dir.mkdir(parents=True)
    (target_dir / "QCurlTargets.cmake").write_text(
        "add_library(QCurl::QCurl STATIC IMPORTED)\n"
        "set_target_properties(QCurl::QCurl PROPERTIES "
        'INTERFACE_LINK_LIBRARIES "Qt6::Core;CURL::libcurl")\n',
        encoding="utf-8",
    )
    (target_dir / "QCurlConfig.cmake").write_text("", encoding="utf-8")

    rc = public_api.check_export_contract(Namespace(stage_dir=stage))

    assert rc == 1
    assert "find_dependency(CURL)" in capsys.readouterr().err


def test_check_export_contract_requires_static_zlib_find_dependency(tmp_path, capsys) -> None:
    stage = tmp_path / "stage"
    target_dir = stage / "lib" / "cmake" / "QCurl"
    target_dir.mkdir(parents=True)
    (target_dir / "QCurlTargets.cmake").write_text(
        "add_library(QCurl::QCurl STATIC IMPORTED)\n"
        "set_target_properties(QCurl::QCurl PROPERTIES "
        'INTERFACE_LINK_LIBRARIES "Qt6::Core;CURL::libcurl;ZLIB::ZLIB")\n',
        encoding="utf-8",
    )
    (target_dir / "QCurlConfig.cmake").write_text(
        "find_dependency(CURL 7.85.0 REQUIRED)\n",
        encoding="utf-8",
    )

    rc = public_api.check_export_contract(Namespace(stage_dir=stage))

    assert rc == 1
    assert "find_dependency(ZLIB)" in capsys.readouterr().err
