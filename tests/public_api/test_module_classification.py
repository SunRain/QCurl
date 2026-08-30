from __future__ import annotations

import json
import re
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def _cmake_list(content: str, variable: str) -> str:
    match = re.search(
        rf"set\s*\(\s*{re.escape(variable)}\b(?P<body>.*?)\)",
        content,
        re.DOTALL,
    )
    assert match is not None, f"missing CMake source list: {variable}"
    return match.group("body")


def test_delivery_targets_keep_four_consumer_surfaces_with_three_physical_artifacts() -> None:
    cmake = (REPO_ROOT / "src" / "CMakeLists.txt").read_text(encoding="utf-8")

    assert re.search(r"add_library\s*\(\s*QCurlBlockingExtras\s+INTERFACE\b", cmake)
    assert not re.search(
        r"add_library\s*\(\s*QCurlBlockingExtras\s+(?:STATIC|SHARED|\$\{_qcurl_library_type\})\b",
        cmake,
    )
    assert re.search(
        r"target_link_libraries\s*\(\s*QCurlBlockingExtras\s+INTERFACE\s+QCurl\s*\)",
        cmake,
    )
    assert not re.search(r"add_library\s*\(\s*QCurlTestSupport\s+INTERFACE\b", cmake)
    assert re.search(r"add_library\s*\(\s*QCurlTestSupport\s+STATIC\b", cmake)


def test_core_producer_embeds_blocking_but_excludes_test_support_implementations() -> None:
    cmake = (REPO_ROOT / "src" / "CMakeLists.txt").read_text(encoding="utf-8")
    features = (REPO_ROOT / "src" / "QCurlFeatureSources.cmake").read_text(encoding="utf-8")

    core_sources = _cmake_list(cmake, "QCURL_CORE_SOURCES")
    core_headers = _cmake_list(cmake, "QCURL_CORE_HEADERS")
    all_sources = _cmake_list(cmake, "QCURL_ALL_SOURCES")
    feature_sources = _cmake_list(features, "QCURL_FEATURE_SOURCES")

    assert "QCNetworkTestSupport.cpp" not in core_sources
    assert "QCBlockingCookieStore.h" not in core_headers
    assert "${QCURL_BLOCKING_EXTRAS_SOURCES}" not in all_sources
    assert "QCNetworkMockHandler.cpp" not in feature_sources
    assert "QCNetworkCapturedRequest.cpp" not in feature_sources

    core_target = re.search(
        r"add_library\s*\(\s*QCurl\s+\$\{_qcurl_library_type\}(?P<body>.*?)\)",
        cmake,
        re.DOTALL,
    )
    assert core_target is not None
    assert "${QCURL_BLOCKING_EXTRAS_ALL_SOURCES}" in core_target.group("body")
    assert "${QCURL_INSTALL_HEADERS_BLOCKING_EXTRAS}" in core_target.group("body")
    assert "${QCURL_TEST_SUPPORT_ALL_SOURCES}" not in core_target.group("body")


def test_surface_manifest_declares_component_maturity_and_compatibility_axes() -> None:
    manifest = json.loads(
        (REPO_ROOT / "tests" / "public_api" / "surface_manifest.json").read_text(
            encoding="utf-8"
        )
    )

    assert manifest["schemaVersion"] == 2
    assert manifest["components"] == {
        "Core": {
            "cmakeTarget": "QCurl::QCurl",
            "artifact": "runtime-library",
            "defaultConsumer": True,
        },
        "BlockingExtras": {
            "cmakeTarget": "QCurl::BlockingExtras",
            "artifact": "interface-consumer-surface",
            "defaultConsumer": False,
        },
        "OtherExtras": {
            "cmakeTarget": "QCurl::OtherExtras",
            "artifact": "runtime-library",
            "defaultConsumer": False,
        },
        "TestSupport": {
            "cmakeTarget": "QCurl::TestSupport",
            "artifact": "development-static-library",
            "defaultConsumer": False,
        },
    }
    assert manifest["compatibilityContract"] == {
        "source": "2.x-compatible",
        "abi": "unstable-rebuild-required",
    }
    assert set(manifest["maturityLevels"]) == {"Stable", "Preview"}

    for entry in manifest["headers"]:
        assert entry["component"] in manifest["components"]
        assert entry["maturity"] in manifest["maturityLevels"]


def test_libcurl_consistency_runner_links_blocking_extras() -> None:
    cmake = (
        REPO_ROOT / "tests" / "libcurl_consistency" / "CMakeLists.txt"
    ).read_text(encoding="utf-8")

    assert re.search(
        r"set\s*\(\s*QCURL_LC_BLOCKING_EXTRAS_TARGET\s+QCurlBlockingExtras\s*\)",
        cmake,
    )
    assert "QCurlBlockingExtrasTestInternals" not in cmake
    assert re.search(
        r"set\s*\(\s*QCURL_LC_BLOCKING_EXTRAS_TARGET\s+QCurlTestInternals\s*\)",
        cmake,
    )

    link_block = re.search(
        r"target_link_libraries\s*\(\s*tst_LibcurlConsistency\b(?P<body>.*?)\)",
        cmake,
        re.DOTALL,
    )
    assert link_block is not None
    assert "${QCURL_LC_BLOCKING_EXTRAS_TARGET}" in link_block.group("body")


def test_blocking_extras_has_no_runtime_install_or_independent_abi_gate() -> None:
    root_cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    src_cmake = (REPO_ROOT / "src" / "CMakeLists.txt").read_text(encoding="utf-8")
    abi_symbols = (REPO_ROOT / "scripts" / "qcurl_abi_symbols.py").read_text(
        encoding="utf-8"
    )
    release_steps = (REPO_ROOT / "scripts" / "release_gate_steps.py").read_text(
        encoding="utf-8"
    )

    assert "BlockingExtrasRuntime" not in root_cmake
    assert "QCURL_BLOCKING_EXTRAS_STATIC_DEFINE" not in root_cmake
    assert "QCURL_BUILDING_BLOCKING_EXTRAS_LIBRARY" not in src_cmake
    assert '"blocking-extras":' not in abi_symbols
    assert "blocking_extras_dynamic_symbol_allowlist" not in release_steps
