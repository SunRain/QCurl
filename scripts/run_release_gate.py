#!/usr/bin/env python3
"""Run no-git QCurl release readiness gates."""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class GateStep:
    name: str
    tier: str
    command: list[str]
    description: str


def _repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def _build_steps(args: argparse.Namespace) -> list[GateStep]:
    build_dir = args.build_dir
    static_build_dir = args.static_build_dir
    jobs = str(args.jobs)
    python = args.python
    ctest = args.ctest
    cmake = args.cmake

    steps: list[GateStep] = []
    if args.contract_json is not None:
        steps.append(GateStep(
            "contract_json",
            "fast",
            [python, "-m", "json.tool", str(args.contract_json)],
            "validate the explicit readiness contract JSON",
        ))

    steps.extend([
        GateStep(
            "shared_public_api",
            "fast",
            [ctest, "--test-dir", str(build_dir), "-L", "^public-api$", "--output-on-failure"],
            "run shared public header self-compile and manifest checks",
        ),
        GateStep(
            "shared_public_api_slow",
            "fast",
            [ctest, "--test-dir", str(build_dir), "-L", "^public-api-slow$", "--output-on-failure"],
            "run shared staging install/export/pkg-config and consumer smoke checks",
        ),
        GateStep(
            "static_configure",
            "full",
            [
                cmake,
                "-S",
                ".",
                "-B",
                str(static_build_dir),
                "-DCMAKE_BUILD_TYPE=Release",
                "-DBUILD_EXAMPLES=OFF",
                "-DBUILD_BENCHMARKS=OFF",
                "-DBUILD_TESTING=ON",
                "-DQCURL_BUILD_SHARED_LIBS=OFF",
                "-DQCURL_BUILD_LIBCURL_CONSISTENCY=OFF",
            ],
            "configure independent QCURL_BUILD_SHARED_LIBS=OFF build tree",
        ),
        GateStep(
            "static_build",
            "full",
            [
                cmake,
                "--build",
                str(static_build_dir),
                "--target",
                "QCurl",
                "QCurlOtherExtras",
                "qcurl_public_api_self_compile",
                "-j",
                jobs,
            ],
            "build static Core, packaged OtherExtras and static public header self-compile target",
        ),
        GateStep(
            "static_public_api",
            "full",
            [
                ctest,
                "--test-dir",
                str(static_build_dir),
                "-L",
                "^public-api$",
                "--output-on-failure",
            ],
            "run static public-api gate in the independent static build tree",
        ),
        GateStep(
            "static_public_api_slow",
            "full",
            [
                ctest,
                "--test-dir",
                str(static_build_dir),
                "-L",
                "^public-api-slow$",
                "--output-on-failure",
            ],
            "run static install/export/pkg-config and consumer smoke gate",
        ),
        GateStep(
            "strict_qttest",
            "strict",
            [python, "scripts/ctest_strict.py", "--build-dir", str(build_dir)],
            "run skip=fail QtTest gate",
        ),
        GateStep(
            "deprecated_curl_api_guard",
            "strict",
            [
                python,
                "scripts/check_deprecated_curl_apis.py",
                "--curl-header",
                "curl/include/curl/curl.h",
                "--scan-root",
                "src",
            ],
            "scan QCurl sources for deprecated libcurl API usage",
        ),
        GateStep(
            "label_matrix_guard",
            "strict",
            [python, "scripts/check_qcurl_label_matrix.py"],
            "validate qcurl CTest label matrix",
        ),
        GateStep(
            "skip_contract_guard",
            "strict",
            [python, "scripts/check_skip_contract.py"],
            "validate CTest skip contract policy",
        ),
        GateStep(
            "full_ctest",
            "full",
            [ctest, "--test-dir", str(build_dir), "--output-on-failure"],
            "run the full CTest suite configured in the build tree",
        ),
        GateStep(
            "libcurl_consistency_full",
            "full",
            [
                python,
                "tests/libcurl_consistency/run_gate.py",
                "--suite",
                "all",
                "--with-ext",
                "--build",
                "--qcurl-build",
                str(build_dir),
            ],
            "run full QCurl/libcurl observable consistency gate",
        ),
        GateStep(
            "abi_diff",
            "full",
            [
                python,
                "scripts/qcurl_abi_gate.py",
                "--library",
                str(build_dir / "src" / "libQCurl.so.3.0.0"),
                "--headers-dir",
                "src",
                "diff",
            ],
            "compare the current shared library against the release ABI baseline",
        ),
        GateStep(
            "capability_matrix_build",
            "full",
            [cmake, "--build", str(build_dir), "--target", "qcurl_lc_capability_probe", "-j", jobs],
            "build the libcurl capability matrix probe used by the consistency gate",
        ),
        GateStep(
            "capability_matrix_probe",
            "full",
            [
                str(build_dir / "tests" / "qcurl_lc_capability_probe"),
                "--output",
                str(build_dir / "libcurl_consistency" / "reports" / "capabilities.json"),
            ],
            "write the release capability matrix JSON",
        ),
        GateStep(
            "metadata_scan",
            "full",
            [python, "scripts/run_release_gate.py", "--scan-metadata", "--build-dir", str(build_dir)],
            "scan release docs and package metadata for default Stable overclaims",
        ),
    ])
    return steps


def _selected_steps(args: argparse.Namespace) -> list[GateStep]:
    order = {"fast": 0, "strict": 1, "full": 2}
    max_order = order[args.tier]
    return [step for step in _build_steps(args) if order[step.tier] <= max_order]


def _format_command(command: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in command)


def _run_step(step: GateStep, repo_root: Path) -> int:
    print(f"[release_gate] RUN {step.name}: {_format_command(step.command)}")
    env = os.environ.copy()
    if step.name == "libcurl_consistency_full":
        env.setdefault("QCURL_LC_EXT", "1")
    proc = subprocess.run(step.command, cwd=repo_root, env=env)
    if proc.returncode != 0:
        print(f"[release_gate] FAIL {step.name}: rc={proc.returncode}", file=sys.stderr)
    else:
        print(f"[release_gate] PASS {step.name}")
    return int(proc.returncode)


def _scan_metadata(repo_root: Path) -> int:
    checks = [
        {
            "path": "CMakeLists.txt",
            "must_not_contain": [
                "WebSocket support\")",
                "HTTP/2 and WebSocket support",
            ],
        },
        {
            "path": "README.md",
            "must_not_contain": [
                "单请求延迟",
                "31,000 ms",
                "~15,000 ms",
                "~10,000 ms",
            ],
        },
        {
            "path": "SYSTEM_DOCUMENTATION.md",
            "must_not_contain": [
                "提供同步和异步两种网络请求方式",
                "| **执行模式** | 同步、异步 |",
                "enum class ExecutionMode",
                "同时支持所有 HTTP 方法和同步/异步模式",
                "QCWebSocket (WebSocket 客户端)",
                "QCNetworkDiagnostics (网络诊断)",
                "sendOptions(",
            ],
        },
    ]
    violations: list[str] = []
    for check in checks:
        path = repo_root / str(check["path"])
        if not path.is_file():
            violations.append(f"missing metadata scan target: {path}")
            continue
        text = path.read_text(encoding="utf-8")
        for needle in check["must_not_contain"]:
            if str(needle) in text:
                violations.append(f"{check['path']}: forbidden release metadata text: {needle}")
    if violations:
        print("\n".join(violations), file=sys.stderr)
        return 1
    print("[release_gate] metadata scan passed")
    return 0


def _write_plan(args: argparse.Namespace, steps: list[GateStep]) -> None:
    payload = {
        "tier": args.tier,
        "buildDir": str(args.build_dir),
        "staticBuildDir": str(args.static_build_dir),
        "steps": [
            {
                "name": step.name,
                "tier": step.tier,
                "description": step.description,
                "command": step.command,
            }
            for step in steps
        ],
    }
    print(json.dumps(payload, ensure_ascii=False, indent=2))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="QCurl no-git release gate. fast is not a Stable release gate; full includes ABI and capability evidence."
    )
    parser.add_argument("--tier", choices=("fast", "strict", "full"), default="fast")
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--static-build-dir", type=Path, default=Path("build-static"))
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument(
        "--contract-json",
        type=Path,
        help="validate this explicit local readiness contract JSON as part of the fast gate",
    )
    parser.add_argument("--dry-run", action="store_true", help="print the selected gate plan without running it")
    parser.add_argument("--scan-metadata", action="store_true", help="run only the release metadata scan")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    repo_root = _repo_root()
    if not args.build_dir.is_absolute():
        args.build_dir = (repo_root / args.build_dir).resolve()
    if not args.static_build_dir.is_absolute():
        args.static_build_dir = (repo_root / args.static_build_dir).resolve()

    if args.scan_metadata:
        return _scan_metadata(repo_root)

    steps = _selected_steps(args)
    if args.dry_run:
        _write_plan(args, steps)
        return 0

    for step in steps:
        rc = _run_step(step, repo_root)
        if rc != 0:
            return rc
    print(f"[release_gate] {args.tier} gate passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
