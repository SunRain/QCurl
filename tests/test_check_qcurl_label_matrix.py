from __future__ import annotations

from scripts import check_qcurl_label_matrix as matrix


def test_parse_preflight_targets_reads_flags() -> None:
    cmake = """
add_qcurl_test_with_preflight(
    tst_QCNetworkReply
    REQUIRE_HTTPBIN
    REQUIRE_LOCAL_PORT
    SOURCES tst_QCNetworkReply.cpp
)
"""

    assert matrix._parse_preflight_targets(cmake) == {
        "tst_QCNetworkReply": {"REQUIRE_HTTPBIN", "REQUIRE_LOCAL_PORT"}
    }


def test_parse_labels_supports_multiple_targets() -> None:
    cmake = """
set_tests_properties(tst_A tst_B PROPERTIES
    LABELS "offline;local_port;node"
    TIMEOUT 30
)
"""

    assert matrix._parse_labels(cmake) == {
        "tst_A": "offline;local_port;node",
        "tst_B": "offline;local_port;node",
    }


def test_main_accepts_complete_label_matrix(tmp_path) -> None:
    cmake = tmp_path / "CMakeLists.txt"
    cmake.write_text(
        """
add_qcurl_test_with_preflight(tst_A REQUIRE_HTTPBIN REQUIRE_LOCAL_PORT SOURCES a.cpp)
set_tests_properties(tst_A PROPERTIES LABELS "offline;httpbin;local_port")
""",
        encoding="utf-8",
    )

    assert matrix.main(["--cmake", str(cmake)]) == 0


def test_main_rejects_missing_labels(tmp_path, capsys) -> None:
    cmake = tmp_path / "CMakeLists.txt"
    cmake.write_text(
        "add_qcurl_test_with_preflight(tst_A REQUIRE_NODE SOURCES a.cpp)\n",
        encoding="utf-8",
    )

    rc = matrix.main(["--cmake", str(cmake)])

    assert rc == 3
    assert "missing LABELS" in capsys.readouterr().err


def test_main_rejects_missing_required_label(tmp_path, capsys) -> None:
    cmake = tmp_path / "CMakeLists.txt"
    cmake.write_text(
        """
add_qcurl_test_with_preflight(tst_A REQUIRE_HTTP2_SUITE REQUIRE_NODE SOURCES a.cpp)
set_tests_properties(tst_A PROPERTIES LABELS "offline;node")
""",
        encoding="utf-8",
    )

    rc = matrix.main(["--cmake", str(cmake)])

    assert rc == 3
    err = capsys.readouterr().err
    assert "REQUIRE_HTTP2_SUITE requires label `http2`" in err
