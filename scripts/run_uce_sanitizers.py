#!/usr/bin/env python3
"""Configure sanitizer builds and run UCE/nightly verification commands."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from dataclasses import dataclass
from datetime import datetime
from datetime import timezone
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class SanitizerProfile:
    name: str
    sanitizer_flags: str
    enable_libcurl_consistency: bool


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def sanitizer_profiles() -> dict[str, SanitizerProfile]:
    return {
        "asan-ubsan-lsan": SanitizerProfile(
            name="asan-ubsan-lsan",
            sanitizer_flags="-fsanitize=address,undefined,leak -fno-omit-frame-pointer",
            enable_libcurl_consistency=True,
        ),
        "tsan": SanitizerProfile(
            name="tsan",
            sanitizer_flags="-fsanitize=thread -fno-omit-frame-pointer",
            enable_libcurl_consistency=False,
        ),
    }


def cmake_configure_command(source_dir: Path, build_dir: Path, profile: SanitizerProfile) -> list[str]:
    return [
        "cmake",
        "-S",
        str(source_dir),
        "-B",
        str(build_dir),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
        "-DBUILD_TESTING=ON",
        "-DBUILD_EXAMPLES=OFF",
        "-DBUILD_BENCHMARKS=OFF",
        f"-DQCURL_BUILD_LIBCURL_CONSISTENCY={'ON' if profile.enable_libcurl_consistency else 'OFF'}",
        f"-DCMAKE_C_FLAGS={profile.sanitizer_flags}",
        f"-DCMAKE_CXX_FLAGS={profile.sanitizer_flags}",
        f"-DCMAKE_EXE_LINKER_FLAGS={profile.sanitizer_flags}",
        f"-DCMAKE_SHARED_LINKER_FLAGS={profile.sanitizer_flags}",
    ]


def tsan_subject_regex() -> str:
    return "^(tst_QCNetworkReply|tst_QCNetworkScheduler|tst_QCNetworkConnectionPool)$"


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def run_command(command: list[str], *, cwd: Path, log_path: Path, env: dict[str, str] | None = None) -> int:
    completed = subprocess.run(
        command,
        cwd=str(cwd),
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(completed.stdout or "", encoding="utf-8")
    return int(completed.returncode)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run QCurl sanitizer builds and archive reports.")
    parser.add_argument("--profile", choices=tuple(sanitizer_profiles()), required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--run-id", default="")
    parser.add_argument("--nproc", default=os.environ.get("QCURL_SANITIZER_NPROC", "0"))
    args = parser.parse_args(argv)

    repo_root = Path(__file__).resolve().parent.parent
    profile = sanitizer_profiles()[args.profile]
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = (repo_root / build_dir).resolve()
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = (repo_root / output_dir).resolve()
    logs_dir = output_dir / "logs"
    report_path = output_dir / "report.json"
    run_id = (args.run_id or "").strip() or datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")

    configure_cmd = cmake_configure_command(repo_root, build_dir, profile)
    configure_rc = run_command(configure_cmd, cwd=repo_root, log_path=logs_dir / "configure.log")
    build_targets = ["tst_QCNetworkReply"]
    if args.profile == "tsan":
        build_targets.extend(["tst_QCNetworkScheduler", "tst_QCNetworkConnectionPool"])
    build_cmd = ["cmake", "--build", str(build_dir), "--target", *build_targets]
    if str(args.nproc).isdigit() and int(args.nproc) > 0:
        build_cmd.append(f"-j{int(args.nproc)}")
    build_rc = 0 if configure_rc != 0 else run_command(build_cmd, cwd=repo_root, log_path=logs_dir / "build.log")

    subject_rc = 0
    subject_cmd: list[str]
    if configure_rc == 0 and build_rc == 0:
        if args.profile == "asan-ubsan-lsan":
            subject_cmd = [
                "python3",
                str(repo_root / "scripts" / "run_uce_gate.py"),
                "--tier",
                "nightly",
                "--build-dir",
                str(build_dir),
                "--run-id",
                f"{run_id}-{profile.name}",
                "--evidence-root",
                str(output_dir / "uce"),
            ]
        else:
            subject_cmd = [
                "ctest",
                "--test-dir",
                str(build_dir),
                "--output-on-failure",
                "-R",
                tsan_subject_regex(),
            ]
        subject_rc = run_command(subject_cmd, cwd=repo_root, log_path=logs_dir / "subject.log")
    else:
        subject_cmd = []

    policy_violations: list[str] = []
    if configure_rc != 0:
        policy_violations.append("sanitizer_config_failed")
    if build_rc != 0:
        policy_violations.append("sanitizer_build_failed")
    if configure_rc == 0 and build_rc == 0 and subject_rc != 0:
        policy_violations.append("sanitizer_subject_failed")

    report = {
        "generated_at_utc": utc_now_iso(),
        "schema": "qcurl-uce/sanitizer-report@v1",
        "profile": profile.name,
        "build_dir": str(build_dir),
        "run_id": run_id,
        "configure_command": configure_cmd,
        "build_command": build_cmd,
        "subject_command": subject_cmd,
        "configure_returncode": configure_rc,
        "build_returncode": build_rc,
        "subject_returncode": subject_rc,
        "policy_violations": policy_violations,
        "result": "pass" if not policy_violations else "fail",
    }
    write_json(report_path, report)
    return 0 if not policy_violations else 3


if __name__ == "__main__":
    raise SystemExit(main())
