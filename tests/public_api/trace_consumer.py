#!/usr/bin/env python3
"""构建或执行安装后 trace consumer，复用既有构建与回环 HTTP/2 夹具。"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import selectors
import subprocess
import sys
import time

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tests.public_api.consumer_build import configure_and_build
from tests.public_api.run_public_api_checks import run
from tests.qcurl.run_qttest_with_preflight import _check_http2_suite, _check_local_port


def start_http2(repo: Path) -> tuple[subprocess.Popen[str], str]:
    """启动既有 TLS 夹具；缺 READY 或进程退出时显式失败并停止子进程。"""
    process = subprocess.Popen(
        ["node", str(repo / "tests/qcurl/http2-test-server.js")],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    try:
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            for key, _ in selector.select(timeout=0.1):
                line = key.fileobj.readline()
                if not line:
                    raise RuntimeError("HTTP/2 夹具在 READY 前退出")
                print(line, end="", flush=True)
                if line.startswith("QCURL_HTTP2_TEST_SERVER_READY "):
                    data = json.loads(line.split(" ", 1)[1])
                    return process, f"https://127.0.0.1:{data['h2Port']}/reqinfo"
            if process.poll() is not None:
                raise RuntimeError("HTTP/2 夹具启动失败")
        raise RuntimeError("HTTP/2 夹具 READY 超时")
    except Exception:
        stop_http2(process)
        raise
    finally:
        selector.close()


def stop_http2(process: subprocess.Popen[str]) -> None:
    """停止本进程创建的测试夹具，避免遗留监听。"""
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
    if process.stdout:
        process.stdout.close()


def main() -> int:
    """分别提供 CTest build fixture 与四个独立执行模式。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("build", "run"))
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--stage-dir", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--consumer-cache", type=Path)
    parser.add_argument("--config", default="")
    parser.add_argument("--protocol", choices=("h1", "h2"))
    parser.add_argument("--ids", choices=("on", "off"))
    parser.add_argument("--http2-probe-bin")
    args = parser.parse_args()
    if args.action == "build" and args.consumer_cache is None:
        parser.error("build 需要 --consumer-cache")
    if args.action == "run" and (args.protocol is None or args.ids is None):
        parser.error("run 需要 --protocol 与 --ids")
    repo = Path(__file__).resolve().parents[2]
    try:
        if args.action == "build":
            configure_and_build(
                Path(__file__).parent / "consumer_trace", args.build_dir, args.stage_dir,
                args.cmake, args.config, run, args.consumer_cache,
            )
            return 0
        reason = _check_local_port()
        if not reason and args.protocol == "h2":
            reason = _check_http2_suite(args.http2_probe_bin)
        if reason:
            raise RuntimeError(reason)
        executable = args.build_dir / "qcurl_trace_consumer"
        if args.config and (args.build_dir / args.config / executable.name).is_file():
            executable = args.build_dir / args.config / executable.name
        process = None
        try:
            base = ""
            if args.protocol == "h2":
                process, base = start_http2(repo)
            run([str(executable), args.protocol, args.ids, base])
        finally:
            if process:
                stop_http2(process)
        return 0
    except (RuntimeError, OSError, ValueError) as error:
        print(f"[trace-consumer] {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
