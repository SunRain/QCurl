"""HTTP method handlers for the observable test server."""

from __future__ import annotations

from urllib.parse import parse_qs, urlsplit
import json

from tests.libcurl_consistency.pytest_support.observe_auth import AUTH_DIGEST_NONCE
from tests.libcurl_consistency.pytest_support.observe_auth import AUTH_PASS
from tests.libcurl_consistency.pytest_support.observe_auth import AUTH_REALM
from tests.libcurl_consistency.pytest_support.observe_auth import AUTH_USER
from tests.libcurl_consistency.pytest_support.observe_auth import basic_auth_valid
from tests.libcurl_consistency.pytest_support.observe_auth import digest_auth_valid
from tests.libcurl_consistency.pytest_support.observe_multipart import multipart_semantic_summary


class ObserveMethodMixin:
    """Request method routes for the observable server."""

    path: str
    headers: object
    wfile: object

    def _write_log(self, status: int, response_headers: dict[str, str], request_body: bytes = b"") -> None:
        raise NotImplementedError

    def _handle_expect_417(self) -> bool:
        """Handle Expect: 100-continue retry semantics."""

        if not self.path.startswith("/expect_417"):
            return False

        expect = str(self.headers.get("expect") or "")  # type: ignore[attr-defined]
        if "100-continue" in expect.lower():
            self.send_response(417)  # type: ignore[attr-defined]
            self.send_header("Content-Length", "0")  # type: ignore[attr-defined]
            self.end_headers()  # type: ignore[attr-defined]
            self._write_log(417, {})
            return True

        body = self._read_body()
        self.send_response(200)  # type: ignore[attr-defined]
        self.send_header("Content-Type", "application/octet-stream")  # type: ignore[attr-defined]
        self.send_header("Content-Length", str(len(body)))  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        if body:
            self.wfile.write(body)  # type: ignore[attr-defined]
        self._write_log(200, {})
        return True

    def do_HEAD(self) -> None:
        if self.path.startswith("/head_with_body"):
            body = b"head-body-should-be-ignored\n"
            self.send_response(200)  # type: ignore[attr-defined]
            self.send_header("Content-Type", "text/plain")  # type: ignore[attr-defined]
            self.send_header("Content-Length", str(len(body)))  # type: ignore[attr-defined]
            self.send_header("Connection", "close")  # type: ignore[attr-defined]
            self.end_headers()  # type: ignore[attr-defined]
            try:
                self.wfile.write(body)  # type: ignore[attr-defined]
                self.wfile.flush()  # type: ignore[attr-defined]
            except BrokenPipeError:
                pass
            self._write_log(200, {}, body)
            self.close_connection = True  # type: ignore[attr-defined]
            return

        if self.path.startswith("/head"):
            self.send_response(200)  # type: ignore[attr-defined]
            self.send_header("Content-Type", "text/plain")  # type: ignore[attr-defined]
            self.send_header("Content-Length", "1234")  # type: ignore[attr-defined]
            self.end_headers()  # type: ignore[attr-defined]
            self._write_log(200, {})
            return

        self._send_not_found(empty=True)

    def do_PATCH(self) -> None:
        if self._handle_method_echo():
            return
        self._send_not_found(empty=True)

    def do_PUT(self) -> None:
        if self._handle_expect_417():
            return
        if self.path.startswith("/redir_308"):
            self._handle_method_redirect(308)  # type: ignore[attr-defined]
            return
        if self.path.startswith("/redir_target_308"):
            self._handle_redirect_target(308)  # type: ignore[attr-defined]
            return
        if self._handle_307_redirect():
            return
        if self._handle_auth_with_body("PUT"):
            return
        if self._handle_method_echo():
            return
        self._send_not_found(empty=True)

    def do_POST(self) -> None:
        if self._handle_expect_417():
            return
        if self.path.startswith("/redir_302"):
            self._handle_method_redirect(302)  # type: ignore[attr-defined]
            return
        if self.path.startswith("/redir_303"):
            self._handle_method_redirect(303)  # type: ignore[attr-defined]
            return
        if self.path.startswith("/redir_308"):
            self._handle_method_redirect(308)  # type: ignore[attr-defined]
            return
        if self.path.startswith("/redir_target_302"):
            self._handle_redirect_target(302)  # type: ignore[attr-defined]
            return
        if self.path.startswith("/redir_target_303"):
            self._handle_redirect_target(303)  # type: ignore[attr-defined]
            return
        if self.path.startswith("/redir_target_308"):
            self._handle_redirect_target(308)  # type: ignore[attr-defined]
            return
        if self._handle_307_redirect():
            return
        if self._handle_auth_with_body("POST"):
            return
        if self._handle_method_echo():
            return
        if self._handle_redir_post_301():
            return
        if self._handle_final_post_301():
            return
        if self._handle_multipart():
            return
        self._send_not_found(empty=True)

    def do_DELETE(self) -> None:
        if self._handle_method_echo():
            return
        self._send_not_found(empty=True)

    def do_GET(self) -> None:
        if self._handle_request_headers():
            return
        if self.path.startswith("/range_boundary"):
            self._handle_range_boundary()  # type: ignore[attr-defined]
            return
        if self.path.startswith("/redir_target_302"):
            self._handle_redirect_target(302)  # type: ignore[attr-defined]
            return
        if self.path.startswith("/redir_target_303"):
            self._handle_redirect_target(303)  # type: ignore[attr-defined]
            return
        if self.path.startswith("/redir_target_308"):
            self._handle_redirect_target(308)  # type: ignore[attr-defined]
            return
        if self._handle_get_auth():
            return
        if self._handle_abs_redirect():  # type: ignore[attr-defined]
            return
        if self._handle_post_301_final():  # type: ignore[attr-defined]
            return
        if self._handle_encoding():  # type: ignore[attr-defined]
            return
        if self._handle_resp_headers():  # type: ignore[attr-defined]
            return
        if self._handle_empty_or_no_content():
            return
        if self._handle_delayed_or_slow_body():  # type: ignore[attr-defined]
            return
        if self._handle_redirect_chain():  # type: ignore[attr-defined]
            return
        if self._handle_cookie_flow():  # type: ignore[attr-defined]
            return
        if self._handle_status():  # type: ignore[attr-defined]
            return
        self._send_not_found(empty=False)

    def _handle_307_redirect(self) -> bool:
        if not self.path.startswith("/redir_307"):
            return False
        _ = self._read_body()
        self.send_response(307)  # type: ignore[attr-defined]
        next_path = self._request_id_path("/method")
        self.send_header("Location", next_path)  # type: ignore[attr-defined]
        self.send_header("Content-Length", "0")  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self._write_log(307, {"Location": next_path})
        return True

    def _request_id_path(self, target: str) -> str:
        q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
        req_id = q.get("id", [""])[0]
        return f"{target}?id={req_id}" if req_id else target

    def _handle_auth_with_body(self, method: str) -> bool:
        if self.path.startswith("/auth/basic"):
            return self._handle_basic_auth_with_body()
        if self.path.startswith("/auth/digest"):
            return self._handle_digest_auth_with_body(method)
        return False

    def _handle_basic_auth_with_body(self) -> bool:
        body = self._read_body()
        auth = str(self.headers.get("authorization") or "")  # type: ignore[attr-defined]
        if basic_auth_valid(auth, expected_user=AUTH_USER, expected_pass=AUTH_PASS):
            self._send_octet_body(body, request_body=body)
            return True
        self._send_auth_challenge("Basic realm=\"qcurl_lc\"", body)
        return True

    def _handle_digest_auth_with_body(self, method: str) -> bool:
        body = self._read_body()
        auth = str(self.headers.get("authorization") or "")  # type: ignore[attr-defined]
        if digest_auth_valid(
            auth,
            method=method,
            request_uri=str(self.path),
            body=body,
            expected_user=AUTH_USER,
            expected_pass=AUTH_PASS,
            expected_realm=AUTH_REALM,
            expected_nonce=AUTH_DIGEST_NONCE,
        ):
            self._send_octet_body(body, request_body=body if method == "PUT" else b"")
            return True
        self._send_auth_challenge(f"Digest realm=\"{AUTH_REALM}\", nonce=\"{AUTH_DIGEST_NONCE}\"")
        return True

    def _handle_method_echo(self) -> bool:
        if not self.path.startswith("/method"):
            return False
        body = self._read_body()
        self._send_octet_body(body, request_body=body)
        return True

    def _send_octet_body(self, body: bytes, *, request_body: bytes) -> None:
        self.send_response(200)  # type: ignore[attr-defined]
        self.send_header("Content-Type", "application/octet-stream")  # type: ignore[attr-defined]
        self.send_header("Content-Length", str(len(body)))  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        if body:
            self.wfile.write(body)  # type: ignore[attr-defined]
        self._write_log(200, {}, request_body)

    def _send_auth_challenge(self, value: str, request_body: bytes = b"") -> None:
        self.send_response(401)  # type: ignore[attr-defined]
        self.send_header("WWW-Authenticate", value)  # type: ignore[attr-defined]
        self.send_header("Content-Length", "0")  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self._write_log(401, {"WWW-Authenticate": value}, request_body)

    def _handle_redir_post_301(self) -> bool:
        if not self.path.startswith("/redir_post_301"):
            return False
        _ = self._read_body()
        next_path = self._request_id_path("/final_post_301")
        self.send_response(301)  # type: ignore[attr-defined]
        self.send_header("Location", next_path)  # type: ignore[attr-defined]
        self.send_header("Content-Length", "0")  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self._write_log(301, {"Location": next_path})
        return True

    def _handle_final_post_301(self) -> bool:
        if not self.path.startswith("/final_post_301"):
            return False
        body = self._read_body()
        response = b"post-301-ok\n"
        self.send_response(200)  # type: ignore[attr-defined]
        self.send_header("Content-Type", "text/plain")  # type: ignore[attr-defined]
        self.send_header("Content-Length", str(len(response)))  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self.wfile.write(response)  # type: ignore[attr-defined]
        self._write_log(200, {}, body)
        return True

    def _handle_multipart(self) -> bool:
        if not self.path.startswith("/multipart"):
            return False
        body = self._read_body()
        content_type = str(self.headers.get("content-type") or "")  # type: ignore[attr-defined]
        try:
            payload: dict[str, object] = multipart_semantic_summary(content_type, body)
            status = 200
        except Exception as exc:
            payload = {"kind": "error", "error": str(exc)}
            status = 400
        response = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
        self.send_response(status)  # type: ignore[attr-defined]
        self.send_header("Content-Type", "application/json")  # type: ignore[attr-defined]
        self.send_header("Content-Length", str(len(response)))  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self.wfile.write(response)  # type: ignore[attr-defined]
        self._write_log(status, {})
        return True

    def _handle_request_headers(self) -> bool:
        if not self.path.startswith("/request_headers"):
            return False
        body = b"headers-ok\n"
        self.send_response(200)  # type: ignore[attr-defined]
        self.send_header("Content-Type", "text/plain")  # type: ignore[attr-defined]
        self.send_header("Content-Length", str(len(body)))  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self.wfile.write(body)  # type: ignore[attr-defined]
        self._write_log(200, {"Content-Length": str(len(body))})
        return True

    def _handle_get_auth(self) -> bool:
        if self.path.startswith("/auth/basic"):
            return self._handle_get_basic_auth()
        if self.path.startswith("/auth/digest"):
            return self._handle_get_digest_auth()
        return False

    def _handle_get_basic_auth(self) -> bool:
        auth = str(self.headers.get("authorization") or "")  # type: ignore[attr-defined]
        if basic_auth_valid(auth, expected_user=AUTH_USER, expected_pass=AUTH_PASS):
            self._send_text_body(200, b"basic-ok\n", request_body=b"basic-ok\n")
            return True
        self._send_auth_challenge("Basic realm=\"qcurl_lc\"")
        return True

    def _handle_get_digest_auth(self) -> bool:
        auth = str(self.headers.get("authorization") or "")  # type: ignore[attr-defined]
        if digest_auth_valid(
            auth,
            method="GET",
            request_uri=str(self.path),
            body=b"",
            expected_user=AUTH_USER,
            expected_pass=AUTH_PASS,
            expected_realm=AUTH_REALM,
            expected_nonce=AUTH_DIGEST_NONCE,
        ):
            self._send_text_body(200, b"digest-ok\n")
            return True
        self._send_auth_challenge(f"Digest realm=\"{AUTH_REALM}\", nonce=\"{AUTH_DIGEST_NONCE}\"")
        return True

    def _send_text_body(self, status: int, body: bytes, *, request_body: bytes = b"") -> None:
        self.send_response(status)  # type: ignore[attr-defined]
        self.send_header("Content-Type", "text/plain")  # type: ignore[attr-defined]
        self.send_header("Content-Length", str(len(body)))  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        if body:
            self.wfile.write(body)  # type: ignore[attr-defined]
        self._write_log(status, {}, request_body)

    def _handle_empty_or_no_content(self) -> bool:
        if self.path.startswith("/empty_200"):
            self.send_response(200)  # type: ignore[attr-defined]
            self.send_header("Content-Length", "0")  # type: ignore[attr-defined]
            self.end_headers()  # type: ignore[attr-defined]
            self._write_log(200, {})
            return True
        if self.path.startswith("/no_content"):
            self.send_response(204)  # type: ignore[attr-defined]
            self.send_header("Content-Length", "0")  # type: ignore[attr-defined]
            self.end_headers()  # type: ignore[attr-defined]
            self._write_log(204, {})
            return True
        return False

    def _send_not_found(self, *, empty: bool) -> None:
        if empty:
            self.send_response(404)  # type: ignore[attr-defined]
            self.send_header("Content-Length", "0")  # type: ignore[attr-defined]
            self.end_headers()  # type: ignore[attr-defined]
            self._write_log(404, {})
            return

        body = b"not found\n"
        self.send_response(404)  # type: ignore[attr-defined]
        self.send_header("Content-Type", "text/plain")  # type: ignore[attr-defined]
        self.send_header("Content-Length", str(len(body)))  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self.wfile.write(body)  # type: ignore[attr-defined]
        self._write_log(404, {})
