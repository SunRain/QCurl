from __future__ import annotations

from copy import deepcopy
from pathlib import Path

import pytest

from scripts import check_qcurl_label_matrix as label_matrix
from tests.libcurl_consistency.pytest_support.gate_report import policy_violations_from_report


def _load_yaml(path: str) -> dict[str, object]:
    text = Path(path).read_text(encoding="utf-8")
    if path.endswith("libcurl_consistency/coverage-map.yaml"):
        return _parse_libcurl_coverage_map(text)
    return _parse_qcurl_coverage_map(text)


def _parse_inline_list(value: str) -> list[str]:
    assert value.startswith("[") and value.endswith("]"), value
    inner = value[1:-1].strip()
    if not inner:
        return []
    return [item.strip() for item in inner.split(",")]


def _parse_scalar(value: str) -> object:
    value = value.strip()
    if value.isdigit():
        return int(value)
    if value.startswith("[") and value.endswith("]"):
        return _parse_inline_list(value)
    return value


def _parse_qcurl_coverage_map(text: str) -> dict[str, object]:
    data: dict[str, object] = {"evidence_types": {}, "surfaces": {}}
    section = ""
    current_surface: dict[str, object] | None = None
    current_entry: dict[str, object] | None = None

    for raw_line in text.splitlines():
        line = raw_line.rstrip()
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if not line.startswith(" "):
            key, _, value = stripped.partition(":")
            if value.strip():
                data[key] = _parse_scalar(value)
                section = ""
            else:
                section = key
            current_surface = None
            current_entry = None
            continue
        if section == "evidence_types" and line.startswith("  ") and not line.startswith("    "):
            key, _, value = stripped.partition(":")
            data["evidence_types"][key] = value.strip()
            continue
        if section != "surfaces":
            continue
        if line.startswith("  ") and not line.startswith("    "):
            key = stripped.removesuffix(":")
            surfaces = data["surfaces"]
            current_surface = {"entries": []}
            surfaces[key] = current_surface
            current_entry = None
            continue
        assert current_surface is not None
        if line.startswith("    ") and not line.startswith("      "):
            key, _, value = stripped.partition(":")
            if key == "entries":
                current_surface["entries"] = []
            else:
                current_surface[key] = _parse_scalar(value)
            current_entry = None
            continue
        if line.startswith("      - "):
            current_entry = {}
            current_surface["entries"].append(current_entry)
            item = stripped[2:].strip()
            if ":" in item:
                key, _, value = item.partition(":")
                current_entry[key] = _parse_scalar(value)
            continue
        if current_entry is not None and line.startswith("        "):
            key, _, value = stripped.partition(":")
            current_entry[key] = _parse_scalar(value)

    return data


def _parse_libcurl_coverage_map(text: str) -> dict[str, object]:
    data: dict[str, object] = {"contracts": {}, "gate_policy": {}}
    section = ""
    in_failure_promotions = False

    for raw_line in text.splitlines():
        line = raw_line.rstrip()
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if not line.startswith(" "):
            key, _, value = stripped.partition(":")
            if value.strip():
                data[key] = _parse_scalar(value)
                section = ""
            else:
                section = key
            in_failure_promotions = False
            continue
        if section == "contracts" and line.startswith("  ") and not line.startswith("    "):
            data["contracts"][stripped.removesuffix(":")] = {}
            in_failure_promotions = False
            continue
        if section != "gate_policy":
            continue
        if line.startswith("  ") and not line.startswith("    "):
            key, _, value = stripped.partition(":")
            if key == "failure_promotions":
                data["gate_policy"][key] = []
                in_failure_promotions = True
            else:
                data["gate_policy"][key] = _parse_scalar(value)
                in_failure_promotions = False
            continue
        if in_failure_promotions and line.startswith("    - "):
            data["gate_policy"]["failure_promotions"].append(stripped[2:].strip())

    return data


def _registered_labels(*cmake_paths: str) -> dict[str, set[str]]:
    labels: dict[str, set[str]] = {}
    for cmake_path in cmake_paths:
        cmake_text = Path(cmake_path).read_text(encoding="utf-8")
        for target, label_string in label_matrix._parse_labels(cmake_text).items():
            labels[target] = {label for label in label_string.split(";") if label}
    return labels


def _surface_entries(map_data: dict[str, object]) -> list[dict[str, object]]:
    surfaces = map_data.get("surfaces")
    assert isinstance(surfaces, dict)
    entries: list[dict[str, object]] = []
    for surface in surfaces.values():
        assert isinstance(surface, dict)
        raw_entries = surface.get("entries", [])
        assert isinstance(raw_entries, list)
        for entry in raw_entries:
            assert isinstance(entry, dict)
            entries.append(entry)
    return entries


def _assert_ctest_labels_match(map_data: dict[str, object], labels_by_target: dict[str, set[str]]) -> None:
    for entry in _surface_entries(map_data):
        target = entry.get("ctest")
        if not target:
            continue
        assert isinstance(target, str)
        expected = labels_by_target.get(target)
        assert expected is not None, f"{target}: missing CTest registration"
        actual = set(entry.get("labels") or [])
        assert actual == expected, f"{target}: labels drift, map={sorted(actual)} cmake={sorted(expected)}"


def _assert_public_api_labels_exist(map_data: dict[str, object], labels_by_target: dict[str, set[str]]) -> None:
    registered_label_sets = list(labels_by_target.values())
    for entry in _surface_entries(map_data):
        label = entry.get("ctest_label")
        if not label:
            continue
        assert isinstance(label, str)
        assert any(label in labels for labels in registered_label_sets), f"{label}: label has no CTest registration"


def _expected_gate_policy_codes() -> set[str]:
    report = {
        "junit_counts": {"parse_error": "bad xml", "tests": 0, "skipped": 1},
        "postflight_artifacts_schema_check": {"violations": [{"file": "bad.json"}]},
        "postflight_redaction_scan": {"violations": [{"file": "leak.txt"}]},
        "preflight_http3_required": {"enabled": True, "violations": ["missing_h3_server"]},
    }
    return set(policy_violations_from_report(report))


def _assert_gate_policy_codes_match(map_data: dict[str, object], expected_codes: set[str]) -> None:
    gate_policy = map_data.get("gate_policy")
    assert isinstance(gate_policy, dict)
    actual = set(gate_policy.get("failure_promotions") or [])
    assert actual == expected_codes, (
        f"failure_promotions drift, map={sorted(actual)} gate_report={sorted(expected_codes)}"
    )


def test_qcurl_coverage_map_lists_core_surfaces_and_evidence_types() -> None:
    map_data = _load_yaml("tests/coverage-map.yaml")

    assert map_data["schema_version"] == 1
    assert set(map_data["evidence_types"]) >= {"contract", "smoke", "diagnostic"}
    assert set(map_data["surfaces"]) >= {
        "qcurl_core_request_reply",
        "qcurl_websocket",
        "public_api_install_surface",
        "libcurl_observability",
    }


def test_qcurl_coverage_map_matches_registered_ctest_labels() -> None:
    map_data = _load_yaml("tests/coverage-map.yaml")
    labels_by_target = _registered_labels("tests/qcurl/CMakeLists.txt", "tests/public_api/CMakeLists.txt")

    _assert_ctest_labels_match(map_data, labels_by_target)
    _assert_public_api_labels_exist(map_data, labels_by_target)


def test_qcurl_coverage_map_rejects_label_drift() -> None:
    map_data = _load_yaml("tests/coverage-map.yaml")
    broken = deepcopy(map_data)
    entries = _surface_entries(broken)
    for entry in entries:
        if entry.get("ctest") == "tst_QCNetworkStreamUpload":
            entry["labels"] = ["local_port"]
            break
    labels_by_target = _registered_labels("tests/qcurl/CMakeLists.txt", "tests/public_api/CMakeLists.txt")

    with pytest.raises(AssertionError, match="labels drift"):
        _assert_ctest_labels_match(broken, labels_by_target)


def test_libcurl_consistency_coverage_map_lists_gate_contracts() -> None:
    map_data = _load_yaml("tests/libcurl_consistency/coverage-map.yaml")

    assert set(map_data["contracts"]) >= {
        "download_upload_p0",
        "websocket_p0",
        "raw_response_headers",
        "p1_request_and_redirect_surface",
        "p2_error_tls_flow_control",
        "ext_opt_in",
    }


def test_libcurl_consistency_coverage_map_matches_gate_policy_codes() -> None:
    map_data = _load_yaml("tests/libcurl_consistency/coverage-map.yaml")

    _assert_gate_policy_codes_match(map_data, _expected_gate_policy_codes())


def test_libcurl_consistency_coverage_map_rejects_policy_code_drift() -> None:
    map_data = _load_yaml("tests/libcurl_consistency/coverage-map.yaml")
    broken = deepcopy(map_data)
    broken["gate_policy"]["failure_promotions"] = ["obsolete_artifacts_schema", "obsolete_redaction"]

    with pytest.raises(AssertionError, match="failure_promotions drift"):
        _assert_gate_policy_codes_match(broken, _expected_gate_policy_codes())
