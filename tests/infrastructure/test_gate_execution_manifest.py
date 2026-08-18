from __future__ import annotations

import json
from pathlib import Path

from tests.libcurl_consistency.pytest_support.artifacts import artifacts_root, write_json
from tests.libcurl_consistency.pytest_support.artifacts import redact_command_args
from tests.libcurl_consistency.pytest_support.gate_manifest import collect_nodeids_from_output
from tests.libcurl_consistency.pytest_support.gate_manifest import evaluate_execution_contract
from tests.libcurl_consistency.pytest_support.gate_manifest import parse_junit_nodeids
from tests.libcurl_consistency.pytest_support.gate_report import policy_violations_from_report


class _Env:
    def __init__(self, gen_dir: Path) -> None:
        self.gen_dir = gen_dir


def _write_junit(path: Path, nodeids: list[str]) -> None:
    testcases = []
    for index, nodeid in enumerate(nodeids):
        testcases.append(
            "<testcase classname=\"gate\" name=\"case-{index}\">"
            "<properties><property name=\"nodeid\" value=\"{nodeid}\"/></properties>"
            "</testcase>".format(index=index, nodeid=nodeid)
        )
    path.write_text(
        '<testsuite tests="{count}">{cases}</testsuite>'.format(
            count=len(testcases),
            cases="".join(testcases),
        ),
        encoding="utf-8",
    )


def _artifact_payload(
    runner: str,
    *,
    run_id: str,
    nodeid: str,
    execution_token: str = "",
) -> dict[str, object]:
    gate_evidence = {"run_id": run_id, "pytest_nodeid": nodeid}
    if execution_token:
        gate_evidence["execution_token"] = execution_token
    return {
        "schema": "qcurl-lc/artifacts@v1",
        "runner": runner,
        "gate_evidence": gate_evidence,
        "request": {"method": "GET", "url": "http://example.test", "headers": {}, "body_len": 0,
                    "body_sha256": ""},
        "response": {"status": 200, "http_version": "HTTP/1.1", "headers": {}, "body_len": 0,
                     "body_sha256": ""},
    }


def _evaluate(
    tmp_path: Path,
    *,
    artifact_flavors: list[str],
    artifact_run_id: str = "run-1",
    required_fields: list[str] | None = None,
    case_names: list[str] | None = None,
    declared_artifact_cases: dict[str, list[str]] | None = None,
    execution_token: str = "",
    artifact_execution_token: str = "",
):
    nodeid = "tests/libcurl_consistency/test_case.py::test_pair[param]"
    junit = tmp_path / "junit.xml"
    artifacts = tmp_path / "artifacts"
    tmp_path.mkdir(parents=True, exist_ok=True)
    _write_junit(junit, [nodeid])
    for case_name in case_names or ["p0/case"]:
        case_dir = artifacts / case_name
        case_dir.mkdir(parents=True)
        for flavor in artifact_flavors:
            runner = "libcurl" if flavor == "baseline" else "qcurl"
            payload = _artifact_payload(
                runner,
                run_id=artifact_run_id,
                nodeid=nodeid,
                execution_token=artifact_execution_token,
            )
            (case_dir / f"{flavor}.json").write_text(json.dumps(payload), encoding="utf-8")

    return evaluate_execution_contract(
        run_id="run-1",
        execution_token=execution_token,
        candidate_files=["tests/libcurl_consistency/test_case.py"],
        planned_files=["tests/libcurl_consistency/test_case.py"],
        planner_exclusions={},
        planned_nodeids=[nodeid],
        junit_xml=junit,
        artifacts_dir=artifacts,
        file_evidence_types={"tests/libcurl_consistency/test_case.py": "contract"},
        node_contracts={
            nodeid: {
                "evidence_type": "contract",
                "required_fields": required_fields or [],
                "artifact_cases": declared_artifact_cases or {},
            }
        },
    )


def test_artifacts_root_uses_run_scoped_environment_path(tmp_path, monkeypatch) -> None:
    scoped = tmp_path / "run" / "artifacts"
    monkeypatch.setenv("QCURL_LC_ARTIFACTS_DIR", str(scoped))

    assert artifacts_root(_Env(tmp_path / "legacy")) == scoped


def test_write_json_binds_run_and_current_pytest_nodeid(tmp_path, monkeypatch) -> None:
    target = tmp_path / "artifact.json"
    monkeypatch.setenv("QCURL_LC_RUN_ID", "run-42")
    monkeypatch.setenv("PYTEST_CURRENT_TEST", "tests/test_sample.py::test_case[param] (call)")

    write_json(target, {"schema": "qcurl-lc/artifacts@v1", "runner": "qcurl"})

    payload = json.loads(target.read_text(encoding="utf-8"))
    assert payload["gate_evidence"] == {
        "run_id": "run-42",
        "pytest_nodeid": "tests/test_sample.py::test_case[param]",
    }


def test_command_args_redact_sensitive_values_without_changing_shape() -> None:
    args = [
        "--cert",
        "/tmp/client.pem",
        "--key-pass",
        "secret-1",
        "--proxy-pass=secret-2",
        "https://example.test",
    ]

    assert redact_command_args(args) == [
        "--cert",
        "/tmp/client.pem",
        "--key-pass",
        "<REDACTED>",
        "--proxy-pass=<REDACTED>",
        "https://example.test",
    ]


def test_collect_and_junit_parsers_preserve_parameterized_nodeids(tmp_path) -> None:
    nodeids = [
        "tests/libcurl_consistency/test_a.py::test_one",
        "tests/libcurl_consistency/test_a.py::test_two[param-a]",
    ]
    output = "\n".join([*nodeids, "2 tests collected in 0.01s"])
    junit = tmp_path / "junit.xml"
    _write_junit(junit, nodeids)

    assert collect_nodeids_from_output(output) == nodeids
    assert parse_junit_nodeids(junit) == {"nodeids": nodeids, "parse_error": ""}


def test_execution_contract_rejects_zero_and_one_sided_artifacts(tmp_path) -> None:
    zero = _evaluate(tmp_path / "zero", artifact_flavors=[])
    one_sided = _evaluate(tmp_path / "one", artifact_flavors=["baseline"])

    assert "artifact_pair_missing" in zero["violation_codes"]
    assert "artifact_pair_missing" in one_sided["violation_codes"]


def test_execution_contract_rejects_stale_run_artifact(tmp_path) -> None:
    result = _evaluate(
        tmp_path,
        artifact_flavors=["baseline", "qcurl"],
        artifact_run_id="previous-run",
    )

    assert "artifact_run_id_mismatch" in result["violation_codes"]


def test_execution_contract_rejects_same_run_id_with_old_execution_token(tmp_path) -> None:
    result = _evaluate(
        tmp_path,
        artifact_flavors=["baseline", "qcurl"],
        execution_token="current-token",
        artifact_execution_token="old-token",
    )

    assert "artifact_execution_token_mismatch" in result["violation_codes"]


def test_execution_contract_accepts_exact_nodeids_and_paired_artifacts(tmp_path) -> None:
    result = _evaluate(tmp_path, artifact_flavors=["baseline", "qcurl"])

    assert result["violations"] == []
    assert result["executed_nodeids"] == [
        "tests/libcurl_consistency/test_case.py::test_pair[param]"
    ]
    assert result["artifact_cases"] == 1
    assert result["evidence_summary"]["planned_nodeids"]["contract"] == 1
    assert result["evidence_summary"]["artifact_cases"]["contract"] == 1


def test_execution_contract_rejects_missing_case_required_field(tmp_path) -> None:
    result = _evaluate(
        tmp_path,
        artifact_flavors=["baseline", "qcurl"],
        required_fields=["observed.error"],
    )

    assert "artifact_required_field_missing" in result["violation_codes"]


def test_execution_contract_rejects_undeclared_duplicate_case_keys(tmp_path) -> None:
    result = _evaluate(
        tmp_path,
        artifact_flavors=["baseline", "qcurl"],
        case_names=["p0/case-a", "p0/case-b"],
    )

    assert "artifact_case_count_mismatch" in result["violation_codes"]


def test_execution_contract_applies_fields_to_declared_artifact_case(tmp_path) -> None:
    result = _evaluate(
        tmp_path,
        artifact_flavors=["baseline", "qcurl"],
        case_names=["p0/success", "p0/failure"],
        declared_artifact_cases={
            "p0/success": ["response.status"],
            "p0/failure": ["observed.error"],
        },
    )

    assert "artifact_required_field_missing" in result["violation_codes"]
    assert "artifact_case_set_mismatch" not in result["violation_codes"]


def test_execution_contract_violations_are_promoted_to_gate_policy() -> None:
    report = {
        "junit_counts": {"tests": 1, "skipped": 0},
        "execution_contract": {"violations": [{"code": "artifact_pair_missing"}]},
    }

    assert "execution_contract" in policy_violations_from_report(report)


def test_evidence_integrity_violations_are_promoted_to_gate_policy() -> None:
    report = {
        "junit_counts": {"tests": 1, "skipped": 0},
        "evidence_integrity": {"valid": False, "errors": ["execution plan hash mismatch"]},
    }

    assert "evidence_integrity" in policy_violations_from_report(report)
