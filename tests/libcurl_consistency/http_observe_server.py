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
import socket
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
    parser.add_argument("--tls-client-ca", type=str, default="", help="要求客户端证书时使用的 CA PEM 路径")
    parser.add_argument("--tls-require-client-cert", action="store_true", help="要求 TLS 客户端证书")
    parser.add_argument("--tls-min", type=str, default="", help="TLS 最低版本，例如 tls1.2")
    parser.add_argument("--tls-max", type=str, default="", help="TLS 最高版本，例如 tls1.2")
    parser.add_argument("--bind-host", type=str, default="localhost", help="监听地址")
    return parser


def _tls_version(value: str) -> ssl.TLSVersion | None:
    versions = {
        "tls1.0": ssl.TLSVersion.TLSv1,
        "tls1.1": ssl.TLSVersion.TLSv1_1,
        "tls1.2": ssl.TLSVersion.TLSv1_2,
        "tls1.3": ssl.TLSVersion.TLSv1_3,
    }
    return versions.get(value.strip().lower())


def configure_tls(
    httpd: ObserveHTTPServer,
    cert_path: Path,
    key_path: Path,
    *,
    client_ca_path: Path | None = None,
    require_client_cert: bool = False,
    minimum: str = "",
    maximum: str = "",
) -> None:
    """Wrap the server socket with TLS."""

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certfile=str(cert_path), keyfile=str(key_path))
    minimum_version = _tls_version(minimum)
    maximum_version = _tls_version(maximum)
    if minimum and minimum_version is None:
        raise ValueError(f"unsupported TLS minimum version: {minimum}")
    if maximum and maximum_version is None:
        raise ValueError(f"unsupported TLS maximum version: {maximum}")
    if minimum_version is not None:
        ctx.minimum_version = minimum_version
    if maximum_version is not None:
        ctx.maximum_version = maximum_version
    if client_ca_path is not None:
        ctx.load_verify_locations(cafile=str(client_ca_path))
        ctx.verify_mode = ssl.CERT_REQUIRED if require_client_cert else ssl.CERT_OPTIONAL
    httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    logging.basicConfig(format="%(asctime)s %(levelname)s %(message)s", level=logging.INFO)
    set_log_file(Path(args.log_file))

    if ":" in args.bind_host:
        ObserveHTTPServer.address_family = socket.AF_INET6
    httpd = ObserveHTTPServer((args.bind_host, args.port), Handler)
    if args.tls_cert and args.tls_key:
        configure_tls(
            httpd,
            Path(args.tls_cert),
            Path(args.tls_key),
            client_ca_path=Path(args.tls_client_ca) if args.tls_client_ca else None,
            require_client_cert=args.tls_require_client_cert,
            minimum=args.tls_min,
            maximum=args.tls_max,
        )
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
