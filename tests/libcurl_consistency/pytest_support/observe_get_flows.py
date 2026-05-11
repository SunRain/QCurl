"""Cookie, status, and slow-body GET routes for the observable server."""

from __future__ import annotations

from urllib.parse import parse_qs, urlsplit
import time


COOKIE_SID = "lc123"


class ObserveGetFlowMixin:
    """Reusable GET flow routes for the observable server."""

    path: str
    headers: object
    wfile: object

    def _write_log(self, status: int, response_headers: dict[str, str], request_body: bytes = b"") -> None:
        raise NotImplementedError

    def _handle_delayed_or_slow_body(self) -> bool:
        """Handle delay, stall, and slow-body transfer scenarios."""

        if self.path.startswith("/delay_headers/"):
            self._write_log(0, {})
            try:
                seg = self.path.split("/", 3)[2]
                delay_ms = int(seg.split("?", 1)[0])
            except Exception:
                delay_ms = 0
            if delay_ms > 0:
                time.sleep(delay_ms / 1000.0)
            return self._send_small_body(b"ok\n")

        if self.path.startswith("/stall_body/"):
            try:
                total = int(self.path.split("/", 4)[2].split("?", 1)[0])
                stall_ms = int(self.path.split("/", 4)[3].split("?", 1)[0])
            except Exception:
                total = 0
                stall_ms = 0
            return self._send_stalled_body(max(0, total), max(0, stall_ms) / 1000.0)

        if self.path.startswith("/slow_body/"):
            try:
                total = int(self.path.split("/", 5)[2].split("?", 1)[0])
                chunk = int(self.path.split("/", 5)[3].split("?", 1)[0])
                sleep_ms = int(self.path.split("/", 5)[4].split("?", 1)[0])
            except Exception:
                total = 0
                chunk = 0
                sleep_ms = 0
            return self._send_slow_body(max(0, total), max(1, chunk), max(0, sleep_ms) / 1000.0)

        return False

    def _send_small_body(self, body: bytes) -> bool:
        try:
            self.send_response(200)  # type: ignore[attr-defined]
            self.send_header("Content-Type", "text/plain")  # type: ignore[attr-defined]
            self.send_header("Content-Length", str(len(body)))  # type: ignore[attr-defined]
            self.end_headers()  # type: ignore[attr-defined]
            self.wfile.write(body)  # type: ignore[attr-defined]
        except BrokenPipeError:
            return True
        return True

    def _send_stalled_body(self, total: int, stall_s: float) -> bool:
        body = b"x" * total
        self.send_response(200)  # type: ignore[attr-defined]
        self.send_header("Content-Type", "application/octet-stream")  # type: ignore[attr-defined]
        self.send_header("Content-Length", str(len(body)))  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self._write_log(200, {})

        if stall_s > 0:
            time.sleep(stall_s)
        try:
            if body:
                self.wfile.write(body)  # type: ignore[attr-defined]
                self.wfile.flush()  # type: ignore[attr-defined]
        except BrokenPipeError:
            return True
        return True

    def _send_slow_body(self, total: int, chunk: int, sleep_s: float) -> bool:
        self.send_response(200)  # type: ignore[attr-defined]
        self.send_header("Content-Type", "application/octet-stream")  # type: ignore[attr-defined]
        self.send_header("Content-Length", str(total))  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self._write_log(200, {})

        remaining = total
        try:
            while remaining > 0:
                n = min(chunk, remaining)
                self.wfile.write(b"x" * n)  # type: ignore[attr-defined]
                self.wfile.flush()  # type: ignore[attr-defined]
                remaining -= n
                if remaining > 0 and sleep_s > 0:
                    time.sleep(sleep_s)
        except BrokenPipeError:
            return True
        return True

    def _handle_cookie_flow(self) -> bool:
        """Handle login, cookie path, home, and cookie endpoints."""

        if self.path.startswith("/login_path"):
            return self._redirect_with_cookie("/a/step", f"sid={COOKIE_SID}; Path=/a; HttpOnly")

        if self.path.startswith("/a/step"):
            if f"sid={COOKIE_SID}" not in str(self.headers.get("cookie") or ""):  # type: ignore[attr-defined]
                return self._send_text_status(401, b"missing cookie (path should match)\n")
            return self._redirect_to("/b/final")

        if self.path.startswith("/b/final"):
            if f"sid={COOKIE_SID}" in str(self.headers.get("cookie") or ""):  # type: ignore[attr-defined]
                return self._send_text_status(400, b"unexpected cookie (path should NOT match)\n")
            return self._send_text_status(200, b"cookie-path-ok\n")

        if self.path.startswith("/login"):
            return self._redirect_with_cookie("/home", f"sid={COOKIE_SID}; Path=/; HttpOnly")

        if self.path.startswith("/home"):
            cookie = str(self.headers.get("cookie") or "")  # type: ignore[attr-defined]
            if f"sid={COOKIE_SID}" not in cookie:
                return self._send_text_status(401, b"missing cookie\n", {"WWW-Authenticate": "Basic realm=\"qcurl_lc\""})
            return self._send_text_status(200, b"home-ok\n")

        if self.path.startswith("/cookie"):
            response_headers: dict[str, str] = {}
            q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
            scenario = (q.get("scenario", [""])[0] or "").strip().lower()
            self.send_response(200)  # type: ignore[attr-defined]
            self.send_header("Content-Type", "text/plain")  # type: ignore[attr-defined]
            if scenario == "1920":
                self.send_header("Set-Cookie", "cookiename=cookiecontent;")  # type: ignore[attr-defined]
                response_headers["Set-Cookie"] = "cookiename=cookiecontent;"
            body = b"cookie-ok\n"
            self.send_header("Content-Length", str(len(body)))  # type: ignore[attr-defined]
            self.end_headers()  # type: ignore[attr-defined]
            self.wfile.write(body)  # type: ignore[attr-defined]
            self._write_log(200, response_headers)
            return True

        return False

    def _redirect_with_cookie(self, target: str, cookie_value: str) -> bool:
        self.send_response(302)  # type: ignore[attr-defined]
        self.send_header("Location", self._with_request_id(target))  # type: ignore[attr-defined]
        self.send_header("Set-Cookie", cookie_value)  # type: ignore[attr-defined]
        self.send_header("Content-Length", "0")  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self._write_log(302, {"Location": self._with_request_id(target), "Set-Cookie": cookie_value})
        return True

    def _redirect_to(self, target: str) -> bool:
        next_path = self._with_request_id(target)
        self.send_response(302)  # type: ignore[attr-defined]
        self.send_header("Location", next_path)  # type: ignore[attr-defined]
        self.send_header("Content-Length", "0")  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self._write_log(302, {"Location": next_path})
        return True

    def _with_request_id(self, target: str) -> str:
        q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
        req_id = q.get("id", [""])[0]
        return f"{target}?id={req_id}" if req_id else target

    def _send_text_status(
        self,
        status: int,
        body: bytes,
        response_headers: dict[str, str] | None = None,
    ) -> bool:
        response_headers = response_headers or {}
        self.send_response(status)  # type: ignore[attr-defined]
        for key, value in response_headers.items():
            self.send_header(key, value)  # type: ignore[attr-defined]
        self.send_header("Content-Type", "text/plain")  # type: ignore[attr-defined]
        self.send_header("Content-Length", str(len(body)))  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self.wfile.write(body)  # type: ignore[attr-defined]
        self._write_log(status, response_headers)
        return True

    def _handle_status(self) -> bool:
        """Handle /status/<code> responses."""

        if not self.path.startswith("/status/"):
            return False
        try:
            code = int(self.path.split("/", 3)[2].split("?", 1)[0])
        except Exception:
            code = 400
        headers = {"WWW-Authenticate": "Basic realm=\"qcurl_lc\""} if code == 401 else {}
        return self._send_text_status(code, f"status {code}\n".encode("utf-8"), headers)
