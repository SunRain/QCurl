from __future__ import annotations

from copy import deepcopy
from pathlib import Path

import pytest
import yaml

from tests.libcurl_consistency.pytest_support.contract_map import load_coverage_map
from tests.libcurl_consistency.pytest_support.contract_map import load_minimal_set
from tests.libcurl_consistency.pytest_support.contract_map import coverage_node_contracts
from tests.libcurl_consistency.pytest_support.contract_map import validate_collected_cases
from tests.libcurl_consistency.pytest_support.coverage_contract_validation import validate_coverage_map
from tests.libcurl_consistency.pytest_support.contract_map import validate_planner_exclusions
from tests.libcurl_consistency.pytest_support.minimal_set_contract import validate_minimal_collected_cases
from tests.libcurl_consistency.pytest_support.minimal_set_contract import validate_minimal_set


def test_coverage_map_is_structured_and_matches_planner_contract() -> None:
    data = load_coverage_map(Path("tests/libcurl_consistency/coverage-map.yaml"))
    assert validate_coverage_map(data, Path.cwd()) == []


def test_coverage_map_rejects_case_path_and_evidence_drift() -> None:
    data = load_coverage_map(Path("tests/libcurl_consistency/coverage-map.yaml"))
    broken = deepcopy(data)
    broken["contracts"]["raw_response_headers"]["cases"][0]["pytest"] = "missing.py"
    broken["contracts"]["raw_response_headers"]["cases"][0]["evidence_type"] = "unknown"

    errors = validate_coverage_map(broken, Path.cwd())

    assert any("pytest path is not in pytest_files" in error for error in errors)
    assert any("unknown evidence type" in error for error in errors)


def test_coverage_map_rejects_declared_case_missing_from_collection() -> None:
    data = load_coverage_map(Path("tests/libcurl_consistency/coverage-map.yaml"))

    errors = validate_collected_cases(
        data,
        planned_files=["tests/libcurl_consistency/test_p1_resp_headers.py"],
        planned_nodeids=[
            "tests/libcurl_consistency/test_p1_resp_headers.py::test_unmapped_case"
        ],
    )

    assert any("nodeid was not collected" in error for error in errors)


def test_collected_nodeids_must_match_static_authority() -> None:
    data = load_coverage_map(Path("tests/libcurl_consistency/coverage-map.yaml"))

    errors = validate_collected_cases(
        data,
        planned_files=["tests/libcurl_consistency/test_p1_resp_headers.py"],
        planned_nodeids=[
            "tests/libcurl_consistency/test_p1_resp_headers.py::test_unmapped_case"
        ],
        authority_nodeids=[
            "tests/libcurl_consistency/test_p1_resp_headers.py::test_p1_resp_headers_raw",
            "tests/libcurl_consistency/test_p1_resp_headers.py::test_p1_resp_headers_unfold_1940",
        ],
    )

    assert any("static authority" in error for error in errors)


def test_coverage_map_rejects_unauthorized_planner_exclusion() -> None:
    data = load_coverage_map(Path("tests/libcurl_consistency/coverage-map.yaml"))

    errors = validate_planner_exclusions(
        data,
        suite="ext",
        exclusions={
            "tests/libcurl_consistency/test_ext_http3_success_h3.py": "arbitrary reason"
        },
    )

    assert any("not authorized" in error for error in errors)


def test_pyyaml_is_part_of_the_locked_gate_environment() -> None:
    lock = Path("tests/libcurl_consistency/requirements.lock.txt").read_text(encoding="utf-8")

    assert "PyYAML==6.0.3" in lock


def test_minimal_set_uses_all_entries_and_current_nodeids() -> None:
    data = load_minimal_set(Path("tests/libcurl_consistency/minimal_set.yaml"))

    assert len(data["cases"]) == 31
    assert validate_minimal_set(data, Path.cwd()) == []


def test_minimal_set_rejects_stale_nodeid() -> None:
    path = Path("tests/libcurl_consistency/minimal_set.yaml")
    data = load_minimal_set(path)
    broken = deepcopy(data)
    broken["cases"][0]["source"]["nodeid"] = "tests/libcurl_consistency/missing.py::test_missing"

    errors = validate_minimal_set(broken, Path.cwd())

    assert any("nodeid path does not exist" in error for error in errors)


def test_variant_contracts_require_transport_or_error_namespaces() -> None:
    data = load_coverage_map(Path("tests/libcurl_consistency/coverage-map.yaml"))
    nodeids = [
        "tests/libcurl_consistency/test_p2_tls_policy.py::test_p2_tls_policy[minimum_tls13-lc_observe_https_tls13-True]",
        "tests/libcurl_consistency/test_p2_tls_policy.py::test_p2_tls_policy[invalid_cipher-lc_observe_https-False]",
        "tests/libcurl_consistency/test_p2_https_proxy_tls.py::test_p2_https_proxy_tls[success]",
        "tests/libcurl_consistency/test_p2_https_proxy_tls.py::test_p2_https_proxy_tls[fail_no_ca]",
    ]

    contracts = coverage_node_contracts(data, planned_nodeids=nodeids)

    assert "transport" in contracts[nodeids[0]]["required_fields"]
    assert {"observed.error", "derived.error"} <= set(contracts[nodeids[1]]["required_fields"])
    assert "transport" in contracts[nodeids[2]]["required_fields"]
    assert {"observed.error", "derived.error"} <= set(contracts[nodeids[3]]["required_fields"])


def test_minimal_set_rejects_parameterized_function_only_selection() -> None:
    data = load_minimal_set(Path("tests/libcurl_consistency/minimal_set.yaml"))
    broken = deepcopy(data)
    broken["cases"][11]["source"]["nodeid"] = (
        "tests/libcurl_consistency/test_p1_socks_success.py::test_p1_socks_success_http_1_1"
    )

    errors = validate_minimal_set(broken, Path.cwd())

    assert any("exact collected nodeid" in error for error in errors)


def test_coverage_map_promotes_execution_contract_failures() -> None:
    data = load_coverage_map(Path("tests/libcurl_consistency/coverage-map.yaml"))

    assert "execution_contract" in data["gate_policy"]["failure_promotions"]


def test_minimal_set_selected_nodeids_must_be_present_in_real_collection() -> None:
    data = load_minimal_set(Path("tests/libcurl_consistency/minimal_set.yaml"))
    planned_file = "tests/libcurl_consistency/test_p1_socks_success.py"

    errors = validate_minimal_collected_cases(
        data,
        suite="p1",
        with_ext=False,
        planned_files=[planned_file],
        collected_nodeids=[],
    )

    assert any("minimal-set nodeid was not collected" in error for error in errors)
