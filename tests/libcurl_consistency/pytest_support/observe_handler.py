"""Composable request handler for the observable HTTP server."""

from __future__ import annotations

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from tests.libcurl_consistency.pytest_support.observe_body import ObserveBodyMixin
from tests.libcurl_consistency.pytest_support.observe_logging import write_observe_log
from tests.libcurl_consistency.pytest_support.observe_methods import ObserveMethodMixin
from tests.libcurl_consistency.pytest_support.observe_scenarios import ObserveScenarioMixin


_OBSERVE_SERVER_BACKLOG = 128


class ObserveHTTPServer(ThreadingHTTPServer):
    """Threaded server with a larger accept backlog for concurrency contract tests."""

    request_queue_size = _OBSERVE_SERVER_BACKLOG


class Handler(ObserveMethodMixin, ObserveScenarioMixin, ObserveBodyMixin, BaseHTTPRequestHandler):
    """Observable HTTP/1.1 handler used by libcurl consistency tests."""

    server_version = "qcurl-lc-observe/0.1"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt: str, *args) -> None:
        return

    def handle_expect_100(self) -> bool:
        """
        Suppress BaseHTTPRequestHandler's automatic 100 Continue for /expect_417.

        The test route needs the server to send final 417 directly. Sending an
        automatic 100 first creates a race around libcurl's retry path.
        """

        expect = str(self.headers.get("expect") or "")
        if self.path.startswith("/expect_417") and "100-continue" in expect.lower():
            return True
        return super().handle_expect_100()

    def _write_log(self, status: int, response_headers: dict[str, str], request_body: bytes = b"") -> None:
        write_observe_log(self, status, response_headers, request_body)
