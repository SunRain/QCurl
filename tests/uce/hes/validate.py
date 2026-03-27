"""Validate HES evidence emitted by libcurl_consistency artifacts."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def _load_contract(contract_path: Path) -> dict[str, Any]:
    return json.loads(contract_path.read_text(encoding="utf-8"))


def hes_required_kinds(contract: dict[str, Any], tier: str) -> set[str]:
    tiers = contract.get("tiers") or {}
    tier_payload = tiers.get(tier) or {}
    return {str(item) for item in tier_payload.get("required_kinds") or []}


def _iter_hes_entries(artifacts_roots: list[Path]) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    for root in artifacts_roots:
        if not root.exists():
            continue
        for artifact_path in sorted(root.rglob("*.json")):
            if artifact_path.name not in {"baseline.json", "qcurl.json"}:
                continue
            payload = json.loads(artifact_path.read_text(encoding="utf-8"))
            hes_payload = payload.get("hes")
            if not isinstance(hes_payload, dict):
                continue
            entries.append(
                {
                    "path": str(artifact_path),
                    "runner": str(payload.get("runner") or ""),
                    "case_id": artifact_path.parent.name,
                    "hes": hes_payload,
                }
            )
    return entries


def _validate_entry(entry: dict[str, Any]) -> list[dict[str, str]]:
    hes = entry["hes"]
    kind = str(hes.get("kind") or "")
    violations: list[dict[str, str]] = []

    if kind == "accept_encoding":
        if "gzip" not in str(hes.get("request_accept_encoding") or "").lower():
            violations.append({"id": "accept_encoding_missing", "message": "request missing gzip accept-encoding"})
        if str(hes.get("response_content_encoding") or "").lower() != "gzip":
            violations.append({"id": "content_encoding_mismatch", "message": "response content-encoding is not gzip"})
        if int(hes.get("body_len") or 0) <= 0:
            violations.append({"id": "body_len_invalid", "message": "decoded body len must be > 0"})
    elif kind == "raw_headers":
        if not isinstance(hes.get("headers_raw_lines"), list) or not hes.get("headers_raw_lines"):
            violations.append({"id": "raw_headers_missing", "message": "headers_raw_lines missing"})
        if int(hes.get("set_cookie_count") or 0) < 2:
            violations.append({"id": "set_cookie_count_invalid", "message": "expected duplicate Set-Cookie lines"})
        if int(hes.get("x_dupe_count") or 0) < 2:
            violations.append({"id": "x_dupe_count_invalid", "message": "expected duplicate X-Dupe lines"})
    elif kind == "expect_100_continue":
        if list(hes.get("statuses") or []) != [417, 200]:
            violations.append({"id": "expect100_statuses_invalid", "message": "expected [417, 200] retry sequence"})
        if "100-continue" not in str(hes.get("first_expect_header") or "").lower():
            violations.append({"id": "expect100_header_missing", "message": "first request missing Expect: 100-continue"})
        if bool(hes.get("second_expect_present")):
            violations.append({"id": "expect100_retry_invalid", "message": "retry request still carries Expect header"})
    elif kind == "chunked_upload":
        if "chunked" not in str(hes.get("transfer_encoding") or "").lower():
            violations.append({"id": "chunked_transfer_encoding_missing", "message": "transfer-encoding is not chunked"})
        if str(hes.get("content_length") or "").strip():
            violations.append({"id": "content_length_present", "message": "chunked upload should not set content-length"})
        if int(hes.get("body_len") or 0) <= 0:
            violations.append({"id": "chunked_body_len_invalid", "message": "uploaded body len must be > 0"})
    else:
        violations.append({"id": "hes_kind_unknown", "message": f"unknown hes kind: {kind}"})

    return violations


def validate_hes(contract_path: Path, artifacts_roots: list[Path], tier: str) -> dict[str, Any]:
    contract = _load_contract(contract_path)
    required_runners = {str(item) for item in contract.get("required_runners") or []}
    required_kinds = hes_required_kinds(contract, tier)
    entries = _iter_hes_entries(artifacts_roots)

    coverage = {(entry["runner"], str(entry["hes"].get("kind") or "")) for entry in entries}
    entry_reports: list[dict[str, Any]] = []
    policy_codes: list[str] = []
    for entry in entries:
        violations = _validate_entry(entry)
        entry_reports.append(
            {
                "path": entry["path"],
                "runner": entry["runner"],
                "case_id": entry["case_id"],
                "kind": str(entry["hes"].get("kind") or ""),
                "result": "pass" if not violations else "fail",
                "violations": violations,
            }
        )
        if violations and "hes_contract_failed" not in policy_codes:
            policy_codes.append("hes_contract_failed")

    missing: list[dict[str, str]] = []
    for runner in sorted(required_runners):
        for kind in sorted(required_kinds):
            if (runner, kind) in coverage:
                continue
            missing.append({"runner": runner, "kind": kind})
    if missing and "hes_evidence_missing" not in policy_codes:
        policy_codes.append("hes_evidence_missing")
    if missing and "hes_contract_failed" not in policy_codes:
        policy_codes.append("hes_contract_failed")

    return {
        "generated_at_utc": datetime_now_iso(),
        "contract_id": contract.get("contract_id"),
        "tier": tier,
        "required_runners": sorted(required_runners),
        "required_kinds": sorted(required_kinds),
        "entries": entry_reports,
        "missing": missing,
        "policy_violations": policy_codes,
    }


def datetime_now_iso() -> str:
    from datetime import datetime
    from datetime import timezone

    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate HES evidence.")
    parser.add_argument("--contract", required=True)
    parser.add_argument("--tier", choices=("pr", "nightly", "soak"), required=True)
    parser.add_argument("--artifacts-root", action="append", default=[], help="Artifact root (repeatable).")
    parser.add_argument("--report", required=True)
    args = parser.parse_args(argv)

    report = validate_hes(
        Path(args.contract),
        [Path(item) for item in args.artifacts_root],
        args.tier,
    )
    write_json(Path(args.report), report)
    return 0 if not report["policy_violations"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
