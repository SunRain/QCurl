#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
一致性 Gate 入口（Task1 / LC-16）：
- 统一完成必要构建（可选）与 pytest 执行
- 输出 JUnit XML + JSON 报告
- 默认开启失败日志收集：QCURL_LC_COLLECT_LOGS=1
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional


_FORBIDDEN_LOCAL_HTTPBIN_ENDPOINTS = (
    "localhost:8935",
    "127.0.0.1:8935",
)

_EXPECTED_ARTIFACTS_SCHEMA = "qcurl-lc/artifacts@v1"


@dataclass(frozen=True)
class GateConfig:
    repo_root: Path
    qcurl_build_dir: Path
    curl_build_dir: Path
    suite: str
    build: bool
    with_ext: bool
    junit_xml: Path
    json_report: Path
    qt_timeout_s: float


def _print_cmd(cmd: List[str]) -> None:
    sys.stderr.write("+ " + " ".join(shlex.quote(x) for x in cmd) + "\n")


def _run(
    cmd: List[str],
    *,
    cwd: Optional[Path] = None,
    env: Optional[Dict[str, str]] = None,
    capture: bool = False,
) -> subprocess.CompletedProcess:
    _print_cmd(cmd)
    return subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        env=env,
        check=False,
        text=True,
        capture_output=capture,
    )


def _parse_junit_counts(junit_xml: Path) -> Dict[str, object]:
    """
    取证式 Gate：将“跳过=通过”的风险显式化。
    - 解析 pytest 生成的 JUnit XML，抽取 tests/failures/errors/skipped 计数。
    - 解析失败/文件缺失时返回 parse_error，供上层转为门禁失败（避免“无执行也绿”）。
    """
    if not junit_xml.exists():
        return {
            "exists": False,
            "tests": 0,
            "failures": 0,
            "errors": 0,
            "skipped": 0,
            "parse_error": f"junit xml not found: {junit_xml}",
        }
    try:
        root = ET.parse(junit_xml).getroot()
    except Exception as exc:
        return {
            "exists": True,
            "tests": 0,
            "failures": 0,
            "errors": 0,
            "skipped": 0,
            "parse_error": f"failed to parse junit xml: {exc}",
        }

    suites = []
    if root.tag == "testsuites":
        suites = list(root.findall("testsuite"))
    elif root.tag == "testsuite":
        suites = [root]
    else:
        suites = list(root.findall(".//testsuite"))

    def _attr_int(node: ET.Element, key: str) -> int:
        raw = (node.attrib.get(key) or "").strip()
        try:
            return int(raw) if raw else 0
        except ValueError:
            return 0

    totals = {"tests": 0, "failures": 0, "errors": 0, "skipped": 0}
    for s in suites:
        for k in totals:
            totals[k] += _attr_int(s, k)

    return {"exists": True, **totals}


def _ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

def _redact_text(text: str) -> str:
    """
    Gate 报告中禁止落盘敏感信息（最小脱敏）：
    - Authorization / Proxy-Authorization：仅保留 scheme（Basic/Digest/Bearer...）
    - Cookie / Set-Cookie：替换为固定占位（避免明文）
    """
    if not text:
        return ""

    patterns: list[tuple[re.Pattern[str], str]] = [
        # Header lines
        (re.compile(r"(?im)^(\s*authorization:\s*)([^\r\n]+)$"), r"\1<REDACTED>"),
        (re.compile(r"(?im)^(\s*proxy-authorization:\s*)([^\r\n]+)$"), r"\1<REDACTED>"),
        (re.compile(r"(?im)^(\s*cookie:\s*)([^\r\n]+)$"), r"\1<REDACTED>"),
        (re.compile(r"(?im)^(\s*set-cookie:\s*)([^\r\n]+)$"), r"\1<REDACTED>"),
        # JSON-ish fragments
        (re.compile(r"(?i)(\"authorization\"\s*:\s*\")([^\"]+)(\")"), r"\1<REDACTED>\3"),
        (re.compile(r"(?i)(\"proxy-authorization\"\s*:\s*\")([^\"]+)(\")"), r"\1<REDACTED>\3"),
        (re.compile(r"(?i)(\"cookie\"\s*:\s*\")([^\"]+)(\")"), r"\1<REDACTED>\3"),
        (re.compile(r"(?i)(\"set-cookie\"\s*:\s*\")([^\"]+)(\")"), r"\1<REDACTED>\3"),
    ]
    out = text
    for rx, repl in patterns:
        out = rx.sub(repl, out)
    return out

def _redaction_scan_roots(cfg: GateConfig) -> List[Path]:
    roots: List[Path] = []
    artifacts_dir = cfg.repo_root / "curl" / "tests" / "http" / "gen" / "artifacts"
    if artifacts_dir.exists():
        roots.append(artifacts_dir)
    reports_dir = cfg.json_report.parent
    if reports_dir.exists():
        roots.append(reports_dir)
    return roots

def _artifacts_dir(cfg: GateConfig) -> Path:
    return cfg.repo_root / "curl" / "tests" / "http" / "gen" / "artifacts"

def _postflight_artifacts_schema_check(cfg: GateConfig, *, since_ts: float) -> Dict[str, object]:
    """
    artifacts schema/version 门禁：
    - baseline/qcurl artifacts 必须声明 schema（版本）
    - schema 不识别/缺失视为 Gate 失败（避免“字段漂移”导致误判）

    说明：
    - 仅检查本次运行产生/更新的 artifacts（mtime >= since_ts-1s），避免历史残留造成误报。
    - 未知字段允许存在（向前兼容）；仅校验版本与 required 字段的存在性/类型。
    """

    root = _artifacts_dir(cfg)
    if not root.exists():
        return {
            "schema_expected": _EXPECTED_ARTIFACTS_SCHEMA,
            "since_ts": float(since_ts),
            "scanned_files": 0,
            "violations": [],
            "note": "artifacts dir not found",
        }

    targets: List[Path] = []
    for name in ("baseline.json", "qcurl.json"):
        for p in root.rglob(name):
            if not p.is_file():
                continue
            try:
                if float(p.stat().st_mtime) < (float(since_ts) - 1.0):
                    continue
            except OSError:
                continue
            targets.append(p)

    def rel(path: Path) -> str:
        try:
            return str(path.relative_to(cfg.repo_root))
        except Exception:
            return str(path)

    required_request_fields = ("method", "url", "headers", "body_len", "body_sha256")
    required_response_fields = ("status", "http_version", "headers", "body_len", "body_sha256")

    violations: List[Dict[str, object]] = []

    def add_violation(path: Path, reason: str) -> None:
        violations.append({"file": rel(path), "reason": reason})

    for p in sorted(targets):
        try:
            payload = json.loads(p.read_text(encoding="utf-8", errors="replace"))
        except Exception as exc:
            add_violation(p, f"invalid json: {exc}")
            continue

        schema = payload.get("schema")
        if schema != _EXPECTED_ARTIFACTS_SCHEMA:
            add_violation(p, f"schema mismatch: {schema!r} != {_EXPECTED_ARTIFACTS_SCHEMA!r}")
            continue

        runner = payload.get("runner")
        if not isinstance(runner, str) or not runner:
            add_violation(p, "runner missing or invalid")

        req = payload.get("request")
        if not isinstance(req, dict):
            add_violation(p, "request missing or invalid")
        else:
            for k in required_request_fields:
                if k not in req:
                    add_violation(p, f"request.{k} missing")
            if "headers" in req and not isinstance(req.get("headers"), dict):
                add_violation(p, "request.headers not a dict")

        resp = payload.get("response")
        if not isinstance(resp, dict):
            add_violation(p, "response missing or invalid")
        else:
            for k in required_response_fields:
                if k not in resp:
                    add_violation(p, f"response.{k} missing")
            if "headers" in resp and not isinstance(resp.get("headers"), dict):
                add_violation(p, "response.headers not a dict")

    return {
        "schema_expected": _EXPECTED_ARTIFACTS_SCHEMA,
        "since_ts": float(since_ts),
        "scanned_files": len(targets),
        "violations": violations,
    }

def _postflight_redaction_scan(cfg: GateConfig, *, since_ts: float) -> Dict[str, object]:
    """
    脱敏扫描门禁（更强落地）：
    - 扫描 Gate 会落盘/会随 CI 上传的文本产物（artifacts + reports）
    - 发现敏感头明文则直接 fail（避免“空指令”）
    """

    rules: list[dict[str, object]] = [
        {
            "id": "auth_basic_unredacted",
            "desc": "Authorization: Basic 明文（应仅保留 scheme 或摘要）",
            "patterns": [
                re.compile(br"(?i)\"authorization\"\s*:\s*\"basic\s+"),
                re.compile(br"(?im)^\s*authorization:\s*basic\s+"),
            ],
        },
        {
            "id": "auth_bearer_unredacted",
            "desc": "Authorization: Bearer 明文（token 不得落盘）",
            "patterns": [
                re.compile(br"(?i)\"authorization\"\s*:\s*\"bearer\s+"),
                re.compile(br"(?im)^\s*authorization:\s*bearer\s+"),
            ],
        },
        {
            "id": "auth_digest_unredacted",
            "desc": "Authorization: Digest 明文（参数不应落盘）",
            "patterns": [
                re.compile(br"(?i)\"authorization\"\s*:\s*\"digest\s+"),
                re.compile(br"(?im)^\s*authorization:\s*digest\s+"),
            ],
        },
        {
            "id": "proxy_auth_basic_unredacted",
            "desc": "Proxy-Authorization: Basic 明文（应仅保留 scheme 或摘要）",
            "patterns": [
                re.compile(br"(?i)\"proxy-authorization\"\s*:\s*\"basic\s+"),
                re.compile(br"(?im)^\s*proxy-authorization:\s*basic\s+"),
            ],
        },
        {
            "id": "cookie_header_unredacted",
            "desc": "Cookie 请求头明文（value 不得落盘）",
            "patterns": [
                re.compile(br"(?i)\"cookie\"\s*:\s*\"[^\r\n\"]*="),
                re.compile(br"(?im)^\s*cookie:\s*[^\r\n]*="),
            ],
        },
        {
            "id": "set_cookie_unredacted",
            "desc": "Set-Cookie 响应头明文（value 不得落盘）",
            "patterns": [
                re.compile(br"(?i)\"set-cookie\"\s*:\s*\"[^\r\n\"]*="),
                re.compile(br"(?im)^\s*set-cookie:\s*[^\r\n]*="),
            ],
        },
    ]

    scan_roots = _redaction_scan_roots(cfg)
    scan_files: List[Path] = []

    def should_scan_file(path: Path) -> bool:
        # 仅扫文本类产物，避免把下载文件/二进制当作“敏感命中”。
        if path.name in ("stderr", "stdout"):
            return True
        ext = path.suffix.lower()
        return ext in (".json", ".jsonl", ".xml", ".txt", ".log")

    for root in scan_roots:
        for p in root.rglob("*"):
            if not p.is_file():
                continue
            try:
                if float(p.stat().st_mtime) < (float(since_ts) - 1.0):
                    continue
            except OSError:
                continue
            if should_scan_file(p):
                scan_files.append(p)

    violations: List[Dict[str, object]] = []
    max_hits = 200

    def rel(path: Path) -> str:
        try:
            return str(path.relative_to(cfg.repo_root))
        except Exception:
            return str(path)

    for p in sorted(scan_files):
        try:
            data = p.read_bytes()
        except OSError:
            continue
        for line_no, line in enumerate(data.splitlines(), 1):
            for rule in rules:
                for rx in rule["patterns"]:  # type: ignore[index]
                    if rx.search(line):  # type: ignore[union-attr]
                        violations.append({
                            "rule": str(rule["id"]),
                            "file": rel(p),
                            "line": int(line_no),
                        })
                        if len(violations) >= max_hits:
                            break
                if len(violations) >= max_hits:
                    break
            if len(violations) >= max_hits:
                break
        if len(violations) >= max_hits:
            break

    return {
        "scan_roots": [str(p) for p in scan_roots],
        "since_ts": float(since_ts),
        "scanned_files": len(scan_files),
        "rules": [{"id": r["id"], "desc": r["desc"]} for r in rules],
        "violations": violations,
    }


def _preflight_forbid_local_httpbin(cfg: GateConfig) -> List[Dict[str, object]]:
    """
    一致性 Gate 的前置约束：禁止引入本地 httpbin（localhost:8935）作为依赖。

    说明：
    - 一致性测试以 `curl/tests/http/testenv` 与本仓库 `http_observe_server.py` 为唯一服务端依赖；
    - 本地 httpbin 仅用于 `tests/` 下的 QCurl 单侧集成测试，不得进入一致性 Gate（避免 Docker/端口成为门禁前置条件）。

    实现：
    - 仅扫描“一致性套件可能执行的代码文件”（.py/.cpp/.h/.hpp...），避免文档出现端口号导致误报。
    - 发现禁用端点则直接 fail，便于在 CI/本地第一时间阻断误引入。
    """

    code_suffixes = {".py", ".cpp", ".cc", ".cxx", ".c", ".h", ".hpp"}
    skip_files = {Path(__file__).resolve()}
    scan_targets: List[Path] = [
        cfg.repo_root / "tests" / "libcurl_consistency",
        cfg.repo_root / "tests" / "tst_LibcurlConsistency.cpp",
    ]

    violations: List[Dict[str, object]] = []

    def scan_file(path: Path) -> None:
        try:
            if path.resolve() in skip_files:
                return
        except OSError:
            return
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return

        hits: Dict[str, List[int]] = {}
        for line_no, line in enumerate(text.splitlines(), 1):
            for needle in _FORBIDDEN_LOCAL_HTTPBIN_ENDPOINTS:
                if needle in line:
                    hits.setdefault(needle, []).append(line_no)
        if not hits:
            return
        violations.append({
            "file": str(path.relative_to(cfg.repo_root)),
            "hits": hits,
        })

    for target in scan_targets:
        if target.is_file():
            if target.suffix in code_suffixes:
                scan_file(target)
            continue
        if not target.exists():
            continue
        for path in target.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix not in code_suffixes:
                continue
            scan_file(path)

    return violations


def _default_reports_dir(qcurl_build_dir: Path) -> Path:
    return qcurl_build_dir / "libcurl_consistency" / "reports"


def _detect_repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _resolve_config(args: argparse.Namespace) -> GateConfig:
    repo_root = _detect_repo_root()
    qcurl_build_dir = (repo_root / args.qcurl_build).resolve()
    curl_build_dir = (repo_root / args.curl_build).resolve() if args.curl_build else (qcurl_build_dir / "curl").resolve()

    reports_dir = (repo_root / args.reports_dir).resolve() if args.reports_dir else _default_reports_dir(qcurl_build_dir)
    junit_xml = reports_dir / f"junit_{args.suite}.xml"
    json_report = reports_dir / f"gate_{args.suite}.json"
    return GateConfig(
        repo_root=repo_root,
        qcurl_build_dir=qcurl_build_dir,
        curl_build_dir=curl_build_dir,
        suite=args.suite,
        build=bool(args.build),
        with_ext=bool(args.with_ext),
        junit_xml=junit_xml,
        json_report=json_report,
        qt_timeout_s=float(args.qt_timeout_s),
    )


def _build_targets(cfg: GateConfig) -> None:
    if not cfg.qcurl_build_dir.exists():
        raise RuntimeError(f"QCurl build dir 不存在: {cfg.qcurl_build_dir}")
    if not cfg.curl_build_dir.exists():
        raise RuntimeError(
            "curl build dir 不存在（需要将 curl 构建到 <qcurl_build>/curl，例如默认 build/curl）。\n"
            f"- 当前 qcurl_build_dir: {cfg.qcurl_build_dir}\n"
            f"- 当前 curl_build_dir: {cfg.curl_build_dir}\n"
            "请先配置：cmake -B <qcurl_build> -DQCURL_BUILD_LIBCURL_CONSISTENCY=ON"
        )

    jobs = str(os.cpu_count() or 4)
    rc = _run(["cmake", "--build", str(cfg.qcurl_build_dir), "--target", "qcurl_lc_deps", "-j", jobs])
    if rc.returncode != 0:
        raise RuntimeError(
            "build qcurl_lc_deps failed. "
            "Please check the build log above for details."
        )

    # best-effort: 构建 h3-capable nghttpx（依赖不足时允许失败 -> h3 变体将 skip）
    rc = _run(["cmake", "--build", str(cfg.qcurl_build_dir), "--target", "qcurl_nghttpx_h3", "-j", jobs])
    if rc.returncode != 0:
        sys.stderr.write("[warn] build qcurl_nghttpx_h3 failed (h3 variants may be skipped)\n")


def _pytest_files(cfg: GateConfig) -> List[str]:
    base = [
        "tests/libcurl_consistency/test_p0_consistency.py",
    ]
    if cfg.suite in ("p1", "all"):
        base.extend([
            "tests/libcurl_consistency/test_p1_proxy.py",
            "tests/libcurl_consistency/test_p1_redirect_and_login_flow.py",
            "tests/libcurl_consistency/test_p1_redirect_policy.py",
            "tests/libcurl_consistency/test_p1_httpauth.py",
            "tests/libcurl_consistency/test_p1_accept_encoding.py",
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
        ])
    if cfg.suite in ("p2", "all"):
        base.extend([
            "tests/libcurl_consistency/test_p2_tls_verify.py",
            "tests/libcurl_consistency/test_p2_tls_pinned_public_key.py",
            "tests/libcurl_consistency/test_p2_cookie_request_header.py",
            "tests/libcurl_consistency/test_p2_fixed_http_errors.py",
            "tests/libcurl_consistency/test_p2_error_paths.py",
            "tests/libcurl_consistency/test_p2_socks5_proxy_fail.py",
            "tests/libcurl_consistency/test_p2_expect_100_continue.py",
            "tests/libcurl_consistency/test_p2_stream_upload_chunked_post.py",
            "tests/libcurl_consistency/test_p2_pause_resume.py",
            "tests/libcurl_consistency/test_p2_pause_resume_strict.py",
            "tests/libcurl_consistency/test_p2_backpressure_contract.py",
            "tests/libcurl_consistency/test_p2_upload_readfunc_pause_resume.py",
            "tests/libcurl_consistency/test_p2_connection_limits.py",
            "tests/libcurl_consistency/test_p2_share_handle.py",
        ])
    if cfg.with_ext:
        base.extend([
            "tests/libcurl_consistency/test_ext_suite.py",
            "tests/libcurl_consistency/test_ext_ws_suite.py",
            "tests/libcurl_consistency/test_ext_tls_policy_and_cache.py",
        ])
    return base


def _gate_env(cfg: GateConfig) -> Dict[str, str]:
    env = os.environ.copy()
    qt_bin = cfg.qcurl_build_dir / "tests" / "tst_LibcurlConsistency"
    env["QCURL_QTTEST"] = str(qt_bin)
    env["CURL_BUILD_DIR"] = str(cfg.curl_build_dir)
    env["CURL"] = str(cfg.curl_build_dir / "src" / "curl")
    env["CURLINFO"] = str(cfg.curl_build_dir / "src" / "curlinfo")
    env.setdefault("QCURL_LC_COLLECT_LOGS", "1")
    env.setdefault("QCURL_LC_QTTEST_TIMEOUT", str(cfg.qt_timeout_s))
    if cfg.with_ext:
        env["QCURL_LC_EXT"] = "1"
    return env


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description="QCurl ↔ libcurl 一致性 Gate（P0 优先）")
    parser.add_argument("--suite", choices=["p0", "p1", "p2", "all"], default="p0", help="选择要跑的套件（默认 p0）")
    parser.add_argument("--with-ext", action="store_true", help="同时运行 ext suite（需要 QCURL_LC_EXT=1）")
    parser.add_argument("--build", action="store_true", help="先构建一致性门禁所需依赖（qcurl_lc_deps；含 curl testdeps/libtests；nghttpx-h3 best-effort）")
    parser.add_argument("--qcurl-build", default="build", help="QCurl CMake build 目录（默认 build）")
    parser.add_argument("--curl-build", default="", help="curl build 目录（默认 <qcurl_build>/curl，即 build/curl）")
    parser.add_argument("--reports-dir", default="", help="报告输出目录（默认 <qcurl_build>/libcurl_consistency/reports）")
    parser.add_argument("--qt-timeout-s", default="90", help="Qt Test 运行超时秒数（默认 90）")
    args = parser.parse_args(argv)

    cfg = _resolve_config(args)
    _ensure_parent(cfg.junit_xml)
    _ensure_parent(cfg.json_report)

    started = time.time()
    require_http3_raw = (os.environ.get("QCURL_REQUIRE_HTTP3") or "").strip()
    require_http3_enabled = require_http3_raw.lower() in ("1", "true", "yes", "on")
    report: Dict[str, object] = {
        "suite": cfg.suite,
        "with_ext": cfg.with_ext,
        "build": cfg.build,
        "repo_root": str(cfg.repo_root),
        "qcurl_build_dir": str(cfg.qcurl_build_dir),
        "curl_build_dir": str(cfg.curl_build_dir),
        "junit_xml": str(cfg.junit_xml),
        "json_report": str(cfg.json_report),
        "qt_timeout_s": cfg.qt_timeout_s,
        "commands": [],
        "pytest_files": _pytest_files(cfg),
        "env": {
            "QCURL_QTTEST": str(cfg.qcurl_build_dir / "tests" / "tst_LibcurlConsistency"),
            "QCURL_LC_COLLECT_LOGS": "1",
            "QCURL_LC_QTTEST_TIMEOUT": str(cfg.qt_timeout_s),
            "QCURL_LC_EXT": "1" if cfg.with_ext else "0",
            "QCURL_REQUIRE_HTTP3": require_http3_raw if require_http3_raw else "0",
            "CURL_BUILD_DIR": str(cfg.curl_build_dir),
            "CURL": str(cfg.curl_build_dir / "src" / "curl"),
            "CURLINFO": str(cfg.curl_build_dir / "src" / "curlinfo"),
        },
        "warnings": [],
        "preflight_http3_required": {
            "enabled": require_http3_enabled,
            "have_h3_server": None,
            "have_h3_curl": None,
            "violations": [],
        },
    }

    try:
        violations = _preflight_forbid_local_httpbin(cfg)
        report["preflight_forbid_local_httpbin_8935"] = {
            "forbidden_endpoints": list(_FORBIDDEN_LOCAL_HTTPBIN_ENDPOINTS),
            "violations": violations,
        }
        if violations:
            lines = [
                "preflight failed: forbidden local httpbin dependency detected (localhost:8935).",
                "consistency gate must not depend on httpbin; use curl testenv + http_observe_server.py instead.",
                "violations:",
            ]
            for v in violations:
                lines.append(f" - {v.get('file')}: {v.get('hits')}")
            raise RuntimeError("\n".join(lines))

        if cfg.build:
            _build_targets(cfg)

        # 供应链/复跑证据：记录 Python 环境（版本 + pip freeze）并落盘到报告目录，便于审计复核。
        try:
            reports_dir = cfg.json_report.parent
            py_ver = _run(["python3", "--version"], cwd=cfg.repo_root, env=_gate_env(cfg), capture=True)
            pip_freeze = _run(["python3", "-m", "pip", "freeze"], cwd=cfg.repo_root, env=_gate_env(cfg), capture=True)
            env_text = []
            env_text.append("# python environment snapshot (for reproducibility/audit)\n")
            env_text.append(f"python3 --version: {(py_ver.stdout or py_ver.stderr or '').strip()}\n")
            env_text.append("\n# pip freeze\n")
            env_text.append(_redact_text((pip_freeze.stdout or "").strip()))
            env_text.append("\n")
            env_path = reports_dir / f"pip_freeze_{cfg.suite}.txt"
            env_path.write_text("".join(env_text), encoding="utf-8")
            report["pip_freeze_artifact"] = str(env_path)
        except Exception as exc:
            report["warnings"].append(f"failed to write pip_freeze artifact: {exc}")

        # 供应链/复跑证据：记录 nghttpx-h3 版本（若存在），便于跨时间复核。
        try:
            reports_dir = cfg.json_report.parent
            nghttpx_h3_bin = cfg.qcurl_build_dir / "libcurl_consistency" / "nghttpx-h3" / "bin" / "nghttpx"
            if nghttpx_h3_bin.exists():
                ng_ver = _run([str(nghttpx_h3_bin), "--version"], cwd=cfg.repo_root, env=_gate_env(cfg), capture=True)
                out = (ng_ver.stdout or "") + "\n" + (ng_ver.stderr or "")
                text = []
                text.append("# nghttpx-h3 version snapshot (for reproducibility/audit)\n\n")
                text.append(f"command: {nghttpx_h3_bin} --version\n")
                text.append(f"returncode: {ng_ver.returncode}\n\n")
                text.append(out.strip() + "\n" if out.strip() else "<no output>\n")
                ng_path = reports_dir / f"nghttpx_version_{cfg.suite}.txt"
                ng_path.write_text(_redact_text("".join(text)), encoding="utf-8")
                report["nghttpx_version_artifact"] = str(ng_path)
        except Exception as exc:
            report["warnings"].append(f"failed to write nghttpx version artifact: {exc}")

        # 可追溯：记录 HTTP/3/WebSockets 覆盖是否可能缺失（不阻断，避免假失败）
        nghttpx_h3 = cfg.qcurl_build_dir / "libcurl_consistency" / "nghttpx-h3" / "bin" / "nghttpx"
        have_h3_server = nghttpx_h3.exists()
        (report["preflight_http3_required"] or {})["have_h3_server"] = have_h3_server
        if require_http3_enabled and not have_h3_server:
            (report["preflight_http3_required"] or {})["violations"].append("missing_h3_server")

        if not have_h3_server:
            report["warnings"].append(
                "nghttpx-h3 not found; env.have_h3_server() will be False and h3 variants will be skipped. "
                "If you need HTTP/3 coverage, build target qcurl_nghttpx_h3 (may require deps/network or a local archive)."
            )

        curl_bin = cfg.curl_build_dir / "src" / "curl"
        if not curl_bin.exists():
            (report["preflight_http3_required"] or {})["have_h3_curl"] = False
            if require_http3_enabled:
                (report["preflight_http3_required"] or {})["violations"].append("missing_curl_bin")
            report["warnings"].append(
                "bundled curl binary not found; cannot probe HTTP/3/WebSockets support (did you build qcurl_lc_deps?)"
            )
        else:
            rc = _run([str(curl_bin), "-V"], cwd=cfg.repo_root, env=_gate_env(cfg), capture=True)
            out = (rc.stdout or "") + "\n" + (rc.stderr or "")
            if out.strip():
                sys.stderr.write(out.rstrip() + "\n")
            if rc.returncode != 0:
                (report["preflight_http3_required"] or {})["have_h3_curl"] = None
                if require_http3_enabled:
                    (report["preflight_http3_required"] or {})["violations"].append("curl_probe_failed")
                report["warnings"].append(f"failed to probe `curl -V` (rc={rc.returncode})")
            else:
                features_line = next((line for line in out.splitlines() if line.startswith("Features:")), "")
                h3_supported = any(tok.upper() == "HTTP3" for tok in features_line.replace("Features:", "").split())
                (report["preflight_http3_required"] or {})["have_h3_curl"] = bool(h3_supported)
                if require_http3_enabled and not h3_supported:
                    (report["preflight_http3_required"] or {})["violations"].append("missing_curl_http3")
                if not h3_supported:
                    report["warnings"].append(
                        "bundled curl does not report HTTP3 in `curl -V`; env.have_h3_curl() will be False and h3 variants will be skipped."
                    )
                protocols_line = next((line for line in out.splitlines() if line.startswith("Protocols:")), "")
                protocols = protocols_line.replace("Protocols:", "").split()
                ws_supported = ("ws" in protocols) or ("wss" in protocols)
                if not ws_supported:
                    report["warnings"].append(
                        "bundled curl does not report ws/wss in `curl -V`; WS cases may be skipped or fail."
                    )

        env = _gate_env(cfg)
        pytest_cmd = [
            "pytest",
            "-q",
            "--maxfail=1",
            "--junitxml",
            str(cfg.junit_xml),
            *_pytest_files(cfg),
        ]
        report["commands"].append(pytest_cmd)
        rc = _run(pytest_cmd, cwd=cfg.repo_root, env=env, capture=True)
        report["pytest_returncode"] = rc.returncode
        redacted_stdout = _redact_text(rc.stdout)
        redacted_stderr = _redact_text(rc.stderr)
        report["pytest_stdout"] = redacted_stdout
        report["pytest_stderr"] = redacted_stderr
        if redacted_stdout:
            sys.stdout.write(redacted_stdout)
        if redacted_stderr:
            sys.stderr.write(redacted_stderr)
    except Exception as exc:
        report["exception"] = _redact_text(str(exc))
        report["pytest_returncode"] = 2
    finally:
        report["duration_s"] = round(time.time() - started, 3)
        report["junit_counts"] = _parse_junit_counts(cfg.junit_xml)
        # 先落盘一次 report（stdout/stderr 已脱敏），使 redaction scan 也能覆盖 gate_*.json 本身。
        cfg.json_report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

        report["postflight_artifacts_schema_check"] = _postflight_artifacts_schema_check(cfg, since_ts=started)
        report["postflight_redaction_scan"] = _postflight_redaction_scan(cfg, since_ts=started)
        gate_rc = int(report.get("pytest_returncode", 2))
        policy_violations = []
        junit_counts = report.get("junit_counts") or {}
        if isinstance(junit_counts, dict):
            if junit_counts.get("parse_error"):
                policy_violations.append("junit_parse_error")
            if int(junit_counts.get("tests") or 0) <= 0:
                policy_violations.append("no_tests_executed")
            if int(junit_counts.get("skipped") or 0) > 0:
                policy_violations.append("skipped_tests")
        if (report.get("postflight_artifacts_schema_check") or {}).get("violations"):
            policy_violations.append("artifacts_schema")
        if (report.get("postflight_redaction_scan") or {}).get("violations"):
            policy_violations.append("redaction")
        preflight_http3 = report.get("preflight_http3_required") or {}
        if isinstance(preflight_http3, dict) and preflight_http3.get("enabled") and preflight_http3.get("violations"):
            policy_violations.append("http3_required")
        report["policy_violations"] = policy_violations
        if policy_violations:
            gate_rc = 3
        report["gate_returncode"] = gate_rc

        # 写回包含 scan 结果的最终报告。
        cfg.json_report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    return int(report.get("gate_returncode", report.get("pytest_returncode", 2)))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
