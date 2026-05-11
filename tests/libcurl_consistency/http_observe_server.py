#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Minimal HTTP/1.1 observation server for libcurl_consistency cases.

The route implementations live in pytest_support/observe_* modules; this file
only owns direct-script imports, CLI parsing, TLS setup, and server lifecycle.
"""

from __future__ import annotations

import argparse
import logging
import ssl
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tests.libcurl_consistency.pytest_support.observe_handler import Handler
from tests.libcurl_consistency.pytest_support.observe_handler import ObserveHTTPServer
from tests.libcurl_consistency.pytest_support.observe_logging import set_log_file


log = logging.getLogger(__name__)


def build_parser() -> argparse.ArgumentParser:
    """Create the observable server CLI parser."""

    parser = argparse.ArgumentParser(description="Run a minimal observable HTTP server for QCurl libcurl_consistency")
    parser.add_argument("--port", type=int, required=True, help="port to listen on")
    parser.add_argument("--log-file", type=str, required=True, help="JSONL file to write observations")
    parser.add_argument("--tls-cert", type=str, default="", help="启用 TLS：服务端证书 PEM 路径（可选）")
    parser.add_argument("--tls-key", type=str, default="", help="启用 TLS：服务端私钥 PEM 路径（可选）")
    return parser


def configure_tls(httpd: ObserveHTTPServer, cert_path: Path, key_path: Path) -> None:
    """Wrap the server socket with TLS."""

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certfile=str(cert_path), keyfile=str(key_path))
    httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    logging.basicConfig(format="%(asctime)s %(levelname)s %(message)s", level=logging.INFO)
    set_log_file(Path(args.log_file))

    httpd = ObserveHTTPServer(("localhost", args.port), Handler)
    if args.tls_cert and args.tls_key:
        configure_tls(httpd, Path(args.tls_cert), Path(args.tls_key))
        log.info("observe https listen on https://localhost:%d", args.port)
    else:
        log.info("observe http listen on http://localhost:%d", args.port)

    try:
        httpd.serve_forever(poll_interval=0.2)
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
