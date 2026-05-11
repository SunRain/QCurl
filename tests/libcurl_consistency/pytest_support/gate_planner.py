"""Planner helpers for the libcurl consistency gate."""

from __future__ import annotations

from pathlib import Path
from typing import Any


def pytest_files(cfg: Any) -> list[str]:
    """Return pytest files planned for a gate suite."""

    base = [
        "tests/libcurl_consistency/test_p0_consistency.py",
        "tests/libcurl_consistency/test_p0_connection_reuse_keepalive.py",
    ]
    if cfg.suite == "p0":
        base.append("tests/libcurl_consistency/test_p1_resp_headers.py")
    if cfg.suite in ("p1", "all"):
        base.extend([
            "tests/libcurl_consistency/test_p1_proxy.py",
            "tests/libcurl_consistency/test_p1_redirect_and_login_flow.py",
            "tests/libcurl_consistency/test_p1_redirect_policy.py",
            "tests/libcurl_consistency/test_p1_httpauth.py",
            "tests/libcurl_consistency/test_p1_accept_encoding.py",
            "tests/libcurl_consistency/test_p1_resolve_connect_to.py",
            "tests/libcurl_consistency/test_p1_upload_seek_constraints.py",
            "tests/libcurl_consistency/test_p1_empty_body.py",
            "tests/libcurl_consistency/test_p1_resp_headers.py",
            "tests/libcurl_consistency/test_p1_progress.py",
            "tests/libcurl_consistency/test_p1_http_methods.py",
            "tests/libcurl_consistency/test_p1_multipart_formdata.py",
            "tests/libcurl_consistency/test_p1_timeouts.py",
            "tests/libcurl_consistency/test_p1_cancel.py",
            "tests/libcurl_consistency/test_p1_postfields_binary.py",
            "tests/libcurl_consistency/test_p1_cookiejar_1903.py",
            "tests/libcurl_consistency/test_p1_request_headers.py",
            "tests/libcurl_consistency/test_p1_socks_success.py",
            "tests/libcurl_consistency/test_p1_redirect_302_303_308.py",
        ])
    if cfg.suite in ("p2", "all"):
        base.extend([
            "tests/libcurl_consistency/test_p2_tls_verify.py",
            "tests/libcurl_consistency/test_p2_tls_pinned_public_key.py",
            "tests/libcurl_consistency/test_p2_cookie_request_header.py",
            "tests/libcurl_consistency/test_p2_protocol_restrictions.py",
            "tests/libcurl_consistency/test_p2_fixed_http_errors.py",
            "tests/libcurl_consistency/test_p2_error_paths.py",
            "tests/libcurl_consistency/test_p2_socks5_proxy_fail.py",
            "tests/libcurl_consistency/test_p2_expect_100_continue.py",
            "tests/libcurl_consistency/test_p2_stream_upload_chunked_post.py",
            "tests/libcurl_consistency/test_p2_pause_resume.py",
            "tests/libcurl_consistency/test_p2_pause_resume_strict.py",
            "tests/libcurl_consistency/test_p2_backpressure_contract.py",
            "tests/libcurl_consistency/test_p2_upload_readfunc_pause_resume.py",
            "tests/libcurl_consistency/test_p2_share_handle.py",
            "tests/libcurl_consistency/test_p2_range_boundaries.py",
        ])
    if cfg.with_ext:
        base.extend([
            "tests/libcurl_consistency/test_ext_suite.py",
            "tests/libcurl_consistency/test_ext_ws_suite.py",
            "tests/libcurl_consistency/test_ext_tls_policy_and_cache.py",
            "tests/libcurl_consistency/test_ext_speed_limit_smoke.py",
            "tests/libcurl_consistency/test_p2_connection_limits.py",
            "tests/libcurl_consistency/test_ext_http3_version_policy.py",
            "tests/libcurl_consistency/test_ext_http3_success_h3.py",
        ])
    return base


def plan_pytest_files(cfg: Any, capability_manifest: dict[str, object]) -> tuple[list[str], dict[str, str]]:
    """Apply capability manifest rules to the planned pytest files."""

    planned: list[str] = []
    exclusions: dict[str, str] = {}
    tests = capability_manifest.get("tests") if isinstance(capability_manifest, dict) else {}
    tests_map = tests if isinstance(tests, dict) else {}

    for path in pytest_files(cfg):
        rule = tests_map.get(Path(path).name)
        enabled = True
        reason = ""
        if isinstance(rule, dict):
            enabled = bool(rule.get("enabled", True))
            reason = str(rule.get("reason") or "")
        if enabled:
            planned.append(path)
        else:
            exclusions[path] = reason or "disabled by capability manifest"
    return planned, exclusions
