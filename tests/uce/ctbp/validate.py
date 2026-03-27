"""Validate CTBP evidence against `ctbp@v1`."""

from __future__ import annotations

import argparse
import json
from datetime import datetime
from datetime import timezone
from pathlib import Path
from typing import Any


_CTBP_SCHEMA = "qcurl-lc/ctbp@v1"


def _utc_now_iso() -> str:
    """Return the current UTC time in ISO-8601 format."""

    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _write_json(path: Path, payload: dict[str, Any]) -> None:
    """Write a JSON document with UTF-8 encoding."""

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _load_contract(contract_path: Path) -> dict[str, Any]:
    """Load the CTBP contract file."""

    return json.loads(contract_path.read_text(encoding="utf-8"))


def _iter_artifact_paths(root: Path) -> list[Path]:
    """Return candidate artifact paths below a root."""

    if not root.exists():
        return []
    paths: list[Path] = []
    for name in ("baseline.json", "qcurl.json"):
        paths.extend(sorted(path for path in root.rglob(name) if path.is_file()))
    return paths


def collect_ctbp_entries(artifacts_roots: list[Path]) -> dict[str, Any]:
    """Collect CTBP-bearing entries from artifact roots."""

    entries: list[dict[str, Any]] = []
    missing_roots: list[str] = []
    scanned_artifacts = 0

    for root in artifacts_roots:
        if not root.exists():
            missing_roots.append(str(root))
            continue
        for artifact_path in _iter_artifact_paths(root):
            scanned_artifacts += 1
            try:
                payload = json.loads(artifact_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                continue
            ctbp = payload.get("ctbp")
            if not isinstance(ctbp, dict):
                continue
            observed = payload.get("observed") if isinstance(payload.get("observed"), dict) else {}
            derived = payload.get("derived") if isinstance(payload.get("derived"), dict) else {}
            case_id = artifact_path.parent.name or artifact_path.stem
            entries.append(
                {
                    "artifact_path": str(artifact_path),
                    "runner": str(payload.get("runner") or artifact_path.stem),
                    "case_id": case_id,
                    "kind": str(ctbp.get("kind") or ""),
                    "ctbp": ctbp,
                    "response": payload.get("response") if isinstance(payload.get("response"), dict) else {},
                    "observed_error": observed.get("error") if isinstance(observed.get("error"), dict) else {},
                    "derived_error": derived.get("error") if isinstance(derived.get("error"), dict) else {},
                    "legacy_error": payload.get("error") if isinstance(payload.get("error"), dict) else {},
                }
            )

    return {
        "entries": entries,
        "missing_roots": missing_roots,
        "scanned_artifacts": scanned_artifacts,
    }


def _dedupe_violations(violations: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Drop duplicated validation errors while preserving order."""

    deduped: list[dict[str, Any]] = []
    seen: set[tuple[str, str]] = set()
    for item in violations:
        marker = (str(item.get("id") or ""), str(item.get("message") or ""))
        if marker in seen:
            continue
        seen.add(marker)
        deduped.append(item)
    return deduped


def _error_kind(entry: dict[str, Any]) -> str:
    """Return the normalized error kind for an artifact entry."""

    derived = entry.get("derived_error") if isinstance(entry.get("derived_error"), dict) else {}
    legacy = entry.get("legacy_error") if isinstance(entry.get("legacy_error"), dict) else {}
    return str(derived.get("kind") or legacy.get("kind") or "")


def _validate_connection_reuse(entry: dict[str, Any]) -> list[dict[str, Any]]:
    """Validate a single connection-reuse proof entry."""

    payload = entry["ctbp"]
    violations: list[dict[str, Any]] = []

    if payload.get("schema") != _CTBP_SCHEMA:
        violations.append(
            {
                "id": "ctbp_schema",
                "message": f"schema mismatch: {payload.get('schema')!r} != {_CTBP_SCHEMA!r}",
            }
        )

    boundary = payload.get("boundary")
    if not isinstance(boundary, dict) or not boundary:
        violations.append({"id": "boundary_missing", "message": "boundary missing or invalid"})

    boundary_key = payload.get("boundary_key")
    if not isinstance(boundary_key, str) or not boundary_key:
        violations.append({"id": "boundary_key_missing", "message": "boundary_key missing or invalid"})

    connection_group_id = payload.get("connection_group_id")
    if not isinstance(connection_group_id, str) or not connection_group_id:
        violations.append({"id": "connection_group_missing", "message": "connection_group_id missing or invalid"})

    request_count = payload.get("request_count")
    if not isinstance(request_count, int) or request_count <= 0:
        violations.append(
            {
                "id": "request_count_positive",
                "message": f"request_count must be > 0, got {request_count!r}",
            }
        )
        request_count = 0

    unique_connections = payload.get("unique_connections")
    if not isinstance(unique_connections, int) or unique_connections <= 0:
        violations.append(
            {
                "id": "unique_connections_positive",
                "message": f"unique_connections must be > 0, got {unique_connections!r}",
            }
        )

    conn_seq = payload.get("conn_seq")
    if not isinstance(conn_seq, list) or not all(isinstance(item, int) and item > 0 for item in conn_seq):
        violations.append({"id": "conn_seq_invalid", "message": "conn_seq missing or contains invalid conn_id"})
        conn_seq = []

    if request_count and len(conn_seq) != request_count:
        violations.append(
            {
                "id": "conn_seq_matches_request_count",
                "message": f"len(conn_seq)={len(conn_seq)} != request_count={request_count}",
            }
        )

    unique_observed = len(set(conn_seq))
    if isinstance(unique_connections, int) and unique_connections != unique_observed:
        violations.append(
            {
                "id": "unique_connections_consistent",
                "message": f"unique_connections={unique_connections} != len(set(conn_seq))={unique_observed}",
            }
        )

    expected_unique = payload.get("expected_unique_connections")
    if expected_unique is not None and expected_unique != unique_connections:
        violations.append(
            {
                "id": "expected_unique_connections",
                "message": f"expected_unique_connections={expected_unique} != unique_connections={unique_connections}",
            }
        )

    requests = payload.get("requests")
    if not isinstance(requests, list):
        violations.append({"id": "requests_invalid", "message": "requests missing or invalid"})
        requests = []

    if request_count and len(requests) != request_count:
        violations.append(
            {
                "id": "requests_match_request_count",
                "message": f"len(requests)={len(requests)} != request_count={request_count}",
            }
        )

    conn_boundaries: dict[int, str] = {}
    for index, request in enumerate(requests):
        if not isinstance(request, dict):
            violations.append({"id": "requests_invalid", "message": f"requests[{index}] is not an object"})
            continue
        conn_id = request.get("conn_id")
        request_boundary_key = request.get("boundary_key")
        if not isinstance(conn_id, int) or conn_id <= 0:
            violations.append({"id": "conn_id_invalid", "message": f"requests[{index}].conn_id invalid: {conn_id!r}"})
            continue
        if not isinstance(request_boundary_key, str) or not request_boundary_key:
            violations.append(
                {
                    "id": "boundary_key_missing",
                    "message": f"requests[{index}].boundary_key missing or invalid",
                }
            )
            continue
        if isinstance(boundary_key, str) and boundary_key and request_boundary_key != boundary_key:
            violations.append(
                {
                    "id": "boundary_key_mismatch",
                    "message": f"requests[{index}].boundary_key={request_boundary_key!r} != {boundary_key!r}",
                }
            )
        if index < len(conn_seq) and conn_seq[index] != conn_id:
            violations.append(
                {
                    "id": "conn_seq_request_mismatch",
                    "message": f"requests[{index}].conn_id={conn_id} != conn_seq[{index}]={conn_seq[index]}",
                }
            )
        previous_boundary = conn_boundaries.setdefault(conn_id, request_boundary_key)
        if previous_boundary != request_boundary_key:
            violations.append(
                {
                    "id": "cross_boundary_reuse_forbidden",
                    "message": f"conn_id {conn_id} observed under multiple boundary keys: {previous_boundary!r} vs {request_boundary_key!r}",
                }
            )

    return _dedupe_violations(violations)


def _validate_tls_boundary(entry: dict[str, Any]) -> list[dict[str, Any]]:
    """Validate a single TLS-boundary proof entry."""

    payload = entry["ctbp"]
    violations: list[dict[str, Any]] = []

    if payload.get("schema") != _CTBP_SCHEMA:
        violations.append(
            {
                "id": "ctbp_schema",
                "message": f"schema mismatch: {payload.get('schema')!r} != {_CTBP_SCHEMA!r}",
            }
        )

    boundary = payload.get("boundary")
    if not isinstance(boundary, dict) or not boundary:
        violations.append({"id": "boundary_missing", "message": "boundary missing or invalid"})
        boundary = {}

    boundary_key = payload.get("boundary_key")
    if not isinstance(boundary_key, str) or not boundary_key:
        violations.append({"id": "boundary_key_missing", "message": "boundary_key missing or invalid"})

    expected_result = str(payload.get("expected_result") or "")
    if expected_result not in {"pass", "tls_error"}:
        violations.append(
            {
                "id": "expected_result_invalid",
                "message": f"expected_result must be 'pass' or 'tls_error', got {expected_result!r}",
            }
        )

    response = entry.get("response") if isinstance(entry.get("response"), dict) else {}
    actual_status = int(response.get("status") or 0)
    actual_http_version = str(response.get("http_version") or "")
    error_kind = _error_kind(entry)

    if expected_result == "pass":
        if actual_status != 200:
            violations.append(
                {
                    "id": "tls_success_matches_response",
                    "message": f"expected status=200, got {actual_status}",
                }
            )
        boundary_http_version = str(boundary.get("http_version") or "")
        if boundary_http_version and actual_http_version != boundary_http_version:
            violations.append(
                {
                    "id": "tls_success_matches_response",
                    "message": f"http_version mismatch: {actual_http_version!r} != {boundary_http_version!r}",
                }
            )
        if error_kind:
            violations.append(
                {
                    "id": "tls_success_matches_response",
                    "message": f"unexpected error.kind for success case: {error_kind!r}",
                }
            )
    elif expected_result == "tls_error":
        expected_error_kind = str(payload.get("expected_error_kind") or "tls")
        if actual_status != 0:
            violations.append(
                {
                    "id": "tls_failure_matches_error_kind",
                    "message": f"expected status=0 for tls_error, got {actual_status}",
                }
            )
        if error_kind != expected_error_kind:
            violations.append(
                {
                    "id": "tls_failure_matches_error_kind",
                    "message": f"expected error.kind={expected_error_kind!r}, got {error_kind!r}",
                }
            )

    return _dedupe_violations(violations)


def validate_ctbp(
    contract_path: Path,
    artifacts_roots: list[Path],
    required_runners: set[str] | None = None,
    required_kinds: set[str] | None = None,
) -> dict[str, Any]:
    """Validate CTBP evidence and return a structured report."""

    contract = _load_contract(contract_path)
    collection = collect_ctbp_entries(artifacts_roots)
    entries = collection["entries"]

    required_runners = set(required_runners or contract.get("required_runners") or [])
    required_kinds = set(required_kinds or contract.get("required_kinds") or [])

    entry_reports: list[dict[str, Any]] = []
    coverage: set[tuple[str, str]] = set()
    runner_summary: dict[str, dict[str, Any]] = {}
    kind_summary: dict[str, dict[str, Any]] = {}

    for entry in sorted(entries, key=lambda item: (item["runner"], item["kind"], item["case_id"], item["artifact_path"])):
        runner = str(entry["runner"])
        kind = str(entry["kind"])
        coverage.add((runner, kind))

        if kind == "connection_reuse":
            violations = _validate_connection_reuse(entry)
        elif kind == "tls_boundary":
            violations = _validate_tls_boundary(entry)
        else:
            violations = [
                {
                    "id": "kind_unknown",
                    "message": f"unsupported ctbp kind: {kind!r}",
                }
            ]

        entry_reports.append(
            {
                "artifact_path": entry["artifact_path"],
                "runner": runner,
                "case_id": entry["case_id"],
                "kind": kind,
                "boundary_key": entry["ctbp"].get("boundary_key"),
                "result": "pass" if not violations else "fail",
                "violations": violations,
            }
        )

        runner_bucket = runner_summary.setdefault(runner, {"entry_count": 0, "kinds": set()})
        runner_bucket["entry_count"] += 1
        runner_bucket["kinds"].add(kind)

        kind_bucket = kind_summary.setdefault(kind, {"entry_count": 0, "runners": set()})
        kind_bucket["entry_count"] += 1
        kind_bucket["runners"].add(runner)

    coverage_violations: list[dict[str, Any]] = []
    for runner in sorted(required_runners):
        for kind in sorted(required_kinds):
            if (runner, kind) in coverage:
                continue
            coverage_violations.append(
                {
                    "code": "ctbp_evidence_missing",
                    "runner": runner,
                    "kind": kind,
                    "message": f"missing CTBP evidence for runner={runner} kind={kind}",
                }
            )

    failed_entries = [item for item in entry_reports if item["result"] != "pass"]
    policy_codes: list[str] = []
    if coverage_violations:
        policy_codes.append("ctbp_evidence_missing")
    if failed_entries:
        policy_codes.append("ctbp_contract_failed")

    normalized_runner_summary = {}
    for runner in sorted(set(runner_summary) | required_runners):
        bucket = runner_summary.get(runner, {"entry_count": 0, "kinds": set()})
        normalized_runner_summary[runner] = {
            "required": runner in required_runners,
            "entry_count": bucket["entry_count"],
            "kinds": sorted(bucket["kinds"]),
        }

    normalized_kind_summary = {}
    for kind in sorted(set(kind_summary) | required_kinds):
        bucket = kind_summary.get(kind, {"entry_count": 0, "runners": set()})
        normalized_kind_summary[kind] = {
            "required": kind in required_kinds,
            "entry_count": bucket["entry_count"],
            "runners": sorted(bucket["runners"]),
        }

    return {
        "generated_at_utc": _utc_now_iso(),
        "contract_id": contract.get("contract_id"),
        "contract_schema": contract.get("schema"),
        "ctbp_schema": contract.get("ctbp_schema"),
        "required_runners": sorted(required_runners),
        "required_kinds": sorted(required_kinds),
        "summary": {
            "scanned_artifacts": collection["scanned_artifacts"],
            "entry_count": len(entry_reports),
            "failed_entries": len(failed_entries),
            "missing_roots": collection["missing_roots"],
        },
        "runner_summary": normalized_runner_summary,
        "kind_summary": normalized_kind_summary,
        "entries": entry_reports,
        "violations": coverage_violations,
        "policy_violations": policy_codes,
    }


def main(argv: list[str] | None = None) -> int:
    """CLI entry point."""

    parser = argparse.ArgumentParser(description="Validate UCE CTBP evidence.")
    parser.add_argument("--contract", required=True, help="Path to CTBP contract YAML/JSON.")
    parser.add_argument(
        "--artifacts-root",
        action="append",
        default=[],
        help="Artifact root to scan for baseline/qcurl JSON payloads (repeatable).",
    )
    parser.add_argument("--report", required=True, help="Output JSON report path.")
    args = parser.parse_args(argv)

    report = validate_ctbp(
        Path(args.contract),
        [Path(item) for item in args.artifacts_root],
    )
    _write_json(Path(args.report), report)
    return 0 if not report["policy_violations"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
