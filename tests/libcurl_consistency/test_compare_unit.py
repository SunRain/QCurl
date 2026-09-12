from __future__ import annotations

import json
from pathlib import Path

import pytest

from tests.libcurl_consistency.pytest_support.compare import compare_artifacts
from tests.libcurl_consistency.pytest_support.artifacts import apply_error_namespaces
from tests.libcurl_consistency.pytest_support.observed import httpd_observed_list_for_id
from tests.libcurl_consistency.pytest_support.observed import nghttpx_observed_list_for_id


def _payload() -> dict[str, object]:
    return {
        "request": {
            "method": "GET",
            "url": "http://example.test/",
            "headers": {"accept": "*/*"},
            "body_len": 0,
            "body_sha256": "",
            "headers_raw_lines": ["GET / HTTP/1.1", "Host: example.test"],
            "headers_raw_len": 36,
            "headers_raw_sha256": "raw-req",
            "headers_semantic": {"host": "example.test"},
        },
        "response": {
            "status": 200,
            "http_version": "HTTP/1.1",
            "headers": {"content-type": "text/plain"},
            "body_len": 2,
            "body_sha256": "body-hash",
            "headers_raw_lines": ["HTTP/1.1 200 OK"],
            "headers_raw_len": 15,
            "headers_raw_sha256": "raw-resp",
        },
    }


def _write(path: Path, payload: dict[str, object]) -> None:
    path.write_text(json.dumps(payload), encoding="utf-8")


@pytest.mark.parametrize("proto", ["HTTP/1.1", "HTTP/2.0", "h3"])
@pytest.mark.parametrize("count", [1, 2])
def test_single_request_observation_rejects_extra_posts(tmp_path, proto, count) -> None:
    access_log = tmp_path / "access.log"
    line = f"2026-09-10T00:00:00Z|{proto}|POST|/echo?id=selected|200|-|8\n"
    access_log.write_text(
        line.replace("id=selected", "id=other") + line * count, encoding="utf-8"
    )
    observe = nghttpx_observed_list_for_id if proto == "h3" else httpd_observed_list_for_id

    if count != 1:
        with pytest.raises(AssertionError, match="got=2, expected=1"):
            observe(access_log, "selected", expected_count=1)
    else:
        observed = observe(access_log, "selected", expected_count=1)
        assert len(observed) == 1
        assert observed[0].method == "POST"
        assert observed[0].url == "/echo"


def test_raw_request_header_missing_on_one_side_is_a_diff(tmp_path) -> None:
    baseline = _payload()
    qcurl = _payload()
    del qcurl["request"]["headers_raw_lines"]  # type: ignore[index]

    left = tmp_path / "baseline.json"
    right = tmp_path / "qcurl.json"
    _write(left, baseline)
    _write(right, qcurl)

    ok, diffs = compare_artifacts(left, right)

    assert not ok
    assert "request.headers_raw_lines missing in one side" in diffs


def test_response_raw_header_mismatch_is_a_diff(tmp_path) -> None:
    baseline = _payload()
    qcurl = _payload()
    qcurl["response"]["headers_raw_sha256"] = "other"  # type: ignore[index]

    left = tmp_path / "baseline.json"
    right = tmp_path / "qcurl.json"
    _write(left, baseline)
    _write(right, qcurl)

    ok, diffs = compare_artifacts(left, right)

    assert not ok
    assert "response.headers_raw_sha256 mismatch: raw-resp != other" in diffs


def test_cookiejar_missing_on_one_side_is_a_diff(tmp_path) -> None:
    baseline = _payload()
    qcurl = _payload()
    baseline["cookiejar"] = {"records": [{"name": "sid"}], "sha256": "cookie"}

    left = tmp_path / "baseline.json"
    right = tmp_path / "qcurl.json"
    _write(left, baseline)
    _write(right, qcurl)

    ok, diffs = compare_artifacts(left, right)

    assert not ok
    assert "cookiejar missing in one side" in diffs


def test_error_namespace_mismatch_is_a_diff(tmp_path) -> None:
    baseline = _payload()
    qcurl = _payload()
    baseline["observed"] = {"error": {"http_status": 417, "http_code": 417}}
    baseline["derived"] = {"error": {"kind": "http_error", "curlcode": 22}}
    qcurl["observed"] = {"error": {"http_status": 500, "http_code": 500}}
    qcurl["derived"] = {"error": {"kind": "http_error", "curlcode": 22}}

    left = tmp_path / "baseline.json"
    right = tmp_path / "qcurl.json"
    _write(left, baseline)
    _write(right, qcurl)

    ok, diffs = compare_artifacts(left, right)

    assert not ok
    assert "observed.error.http_status mismatch: 417 != 500" in diffs


def test_socks_protocol_and_pause_contracts_report_mismatches(tmp_path) -> None:
    baseline = _payload()
    qcurl = _payload()
    baseline["socks"] = {"version": 5, "cmd": 1, "atyp": 3, "dst": "example.test", "dst_port": 80, "rep": 0}
    qcurl["socks"] = {"version": 5, "cmd": 1, "atyp": 1, "dst": "127.0.0.1", "dst_port": 80, "rep": 0}
    baseline["protocol"] = {"requested": "h2", "observed": "h2"}
    qcurl["protocol"] = {"requested": "h2", "observed": "http/1.1"}
    baseline["pause_resume"] = {"pause_offset": 5, "pause_count": 1, "resume_count": 1, "event_seq": ["pause", "resume", "finished"]}
    qcurl["pause_resume"] = {"pause_offset": 6, "pause_count": 1, "resume_count": 1, "event_seq": ["pause", "resume", "finished"]}

    left = tmp_path / "baseline.json"
    right = tmp_path / "qcurl.json"
    _write(left, baseline)
    _write(right, qcurl)

    ok, diffs = compare_artifacts(left, right)

    assert not ok
    assert "atyp mismatch: 3 != 1" in diffs
    assert "observed mismatch: h2 != http/1.1" in diffs
    assert "pause_offset mismatch: 5 != 6" in diffs


def test_backpressure_and_upload_pause_resume_require_expected_schema(tmp_path) -> None:
    baseline = _payload()
    qcurl = _payload()
    baseline["backpressure_contract"] = {
        "schema": "qcurl-lc/backpressure@v1",
        "proto": "h2",
        "limit_bytes": 16384,
        "resume_bytes": 8192,
        "curl_max_write_size": 16384,
        "event_seq": ["bp_on", "bp_off"],
    }
    qcurl["backpressure_contract"] = {
        "schema": "wrong",
        "proto": "h2",
        "limit_bytes": 16384,
        "resume_bytes": 8192,
        "curl_max_write_size": 16384,
        "event_seq": ["bp_on", "bp_off"],
    }
    baseline["upload_pause_resume"] = {
        "schema": "qcurl-lc/upload-pause-resume@v1",
        "proto": "http/1.1",
        "payload_size": 4096,
        "zero_read_count": 1,
        "event_seq": ["pause", "resume"],
    }
    qcurl["upload_pause_resume"] = {
        "schema": "qcurl-lc/upload-pause-resume@v1",
        "proto": "http/1.1",
        "payload_size": 4096,
        "zero_read_count": 0,
        "event_seq": ["pause", "resume"],
    }

    left = tmp_path / "baseline.json"
    right = tmp_path / "qcurl.json"
    _write(left, baseline)
    _write(right, qcurl)

    ok, diffs = compare_artifacts(left, right)

    assert not ok
    assert "qcurl backpressure_contract.schema mismatch: 'wrong'" in diffs
    assert "qcurl upload_pause_resume.zero_read_count invalid: 0" in diffs


def test_required_fields_fail_when_both_sides_are_missing(tmp_path) -> None:
    baseline = _payload()
    qcurl = _payload()
    del baseline["request"]["headers_semantic"]  # type: ignore[index]
    del qcurl["request"]["headers_semantic"]  # type: ignore[index]

    left = tmp_path / "baseline.json"
    right = tmp_path / "qcurl.json"
    _write(left, baseline)
    _write(right, qcurl)

    ok, diffs = compare_artifacts(
        left,
        right,
        required_fields={"request.headers_semantic": dict},
    )

    assert not ok
    assert "baseline request.headers_semantic missing" in diffs
    assert "qcurl request.headers_semantic missing" in diffs


def test_required_fields_reject_legacy_error_fallback_and_bad_types(tmp_path) -> None:
    baseline = _payload()
    qcurl = _payload()
    baseline["error"] = {"http_status": 500}
    qcurl["error"] = {"http_status": 500}
    baseline["observed"] = {"error": {"http_status": 500}}
    qcurl["observed"] = {"error": {"http_status": "500"}}

    left = tmp_path / "baseline.json"
    right = tmp_path / "qcurl.json"
    _write(left, baseline)
    _write(right, qcurl)

    ok, diffs = compare_artifacts(
        left,
        right,
        required_fields={"observed.error.http_status": int},
    )

    assert not ok
    assert any("qcurl observed.error.http_status has invalid type" in diff for diff in diffs)


def test_error_namespace_producer_does_not_write_legacy_payload_error() -> None:
    payload = {}

    apply_error_namespaces(payload, kind="tls", http_status=0, curlcode=35)

    assert "error" not in payload
    assert payload["observed"]["error"] == {"http_status": 0}
    assert payload["derived"]["error"] == {"kind": "tls", "curlcode": 35}
