"""GET scenario handlers for the observable test server."""

from __future__ import annotations

from urllib.parse import parse_qs, urlsplit
import gzip
import zlib

from tests.libcurl_consistency.pytest_support.observe_get_flows import COOKIE_SID
from tests.libcurl_consistency.pytest_support.observe_get_flows import ObserveGetFlowMixin

try:
    import brotli  # type: ignore
except Exception:  # pragma: no cover - optional dependency
    brotli = None


class ObserveScenarioMixin(ObserveGetFlowMixin):
    """Reusable GET scenario routes for the observable server."""

    path: str
    headers: object
    wfile: object

    def _write_log(self, status: int, response_headers: dict[str, str], request_body: bytes = b"") -> None:
        raise NotImplementedError

    def _handle_abs_redirect(self) -> bool:
        """Handle absolute redirect test routes."""

        if self.path.startswith("/redir_abs"):
            q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
            req_id = q.get("id", [""])[0]
            try:
                to_port = int(q.get("to_port", ["0"])[0])
            except Exception:
                to_port = 0
            if to_port <= 0:
                body = b"missing to_port\n"
                self.send_response(400)  # type: ignore[attr-defined]
                self.send_header("Content-Type", "text/plain")  # type: ignore[attr-defined]
                self.send_header("Content-Length", str(len(body)))  # type: ignore[attr-defined]
                self.end_headers()  # type: ignore[attr-defined]
                self.wfile.write(body)  # type: ignore[attr-defined]
                self._write_log(400, {}, body)
                return True

            loc = f"http://localhost:{to_port}/abs_target"
            if req_id:
                loc = f"{loc}?id={req_id}"
            self.send_response(302)  # type: ignore[attr-defined]
            self.send_header("Location", loc)  # type: ignore[attr-defined]
            self.send_header("Content-Length", "0")  # type: ignore[attr-defined]
            self.end_headers()  # type: ignore[attr-defined]
            self._write_log(302, {"Location": loc})
            return True

        if self.path.startswith("/abs_target"):
            body = b"abs-target-ok\n"
            self.send_response(200)  # type: ignore[attr-defined]
            self.send_header("Content-Type", "text/plain")  # type: ignore[attr-defined]
            self.send_header("Content-Length", str(len(body)))  # type: ignore[attr-defined]
            self.end_headers()  # type: ignore[attr-defined]
            self.wfile.write(body)  # type: ignore[attr-defined]
            self._write_log(200, {})
            return True

        return False

    def _handle_encoding(self) -> bool:
        """Handle Accept-Encoding response scenarios."""

        if not self.path.startswith("/enc"):
            return False

        raw_body = b"accept-encoding-ok\n"
        accept = str(self.headers.get("accept-encoding") or "")  # type: ignore[attr-defined]
        accept_lower = accept.lower()
        encoding = ""
        out = raw_body
        if "br" in accept_lower and brotli is not None:
            try:
                encoding = "br"
                out = brotli.compress(raw_body)
            except Exception:
                encoding = ""
                out = raw_body
        elif "gzip" in accept_lower:
            encoding = "gzip"
            out = gzip.compress(raw_body)
        elif "deflate" in accept_lower:
            encoding = "deflate"
            out = zlib.compress(raw_body)

        self.send_response(200)  # type: ignore[attr-defined]
        self.send_header("Content-Type", "application/octet-stream")  # type: ignore[attr-defined]
        if encoding:
            self.send_header("Content-Encoding", encoding)  # type: ignore[attr-defined]
        self.send_header("Content-Length", str(len(out)))  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        if out:
            self.wfile.write(out)  # type: ignore[attr-defined]
        self._write_log(200, {"Content-Encoding": encoding, "Content-Length": str(len(out))})
        return True

    def _handle_post_301_final(self) -> bool:
        """Handle the GET target for POST-to-301 redirect conversion."""

        if not self.path.startswith("/final_post_301"):
            return False
        body = b"post-301-ok\n"
        self.send_response(200)  # type: ignore[attr-defined]
        self.send_header("Content-Type", "text/plain")  # type: ignore[attr-defined]
        self.send_header("Content-Length", str(len(body)))  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self.wfile.write(body)  # type: ignore[attr-defined]
        self._write_log(200, {})
        return True

    def _handle_resp_headers(self) -> bool:
        """Handle deterministic response header scenarios."""

        if not self.path.startswith("/resp_headers"):
            return False

        q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
        scenario = (q.get("scenario", [""])[0] or "").strip().lower()
        if scenario == "1940":
            self.close_connection = True  # type: ignore[attr-defined]
            raw = (
                "HTTP/1.1 200 OK\r\n"
                "Date: Thu, 09 Nov 2010 14:49:00 GMT\r\n"
                "Server:       test with trailing space     \r\n"
                "Content-Type: text/html\r\n"
                "Fold: is\r\n"
                " folding a     \r\n"
                "   line\r\n"
                "Content-Length: 0\r\n"
                "Test:\t\r\n"
                "\t \r\n"
                "\tword\r\n"
                "Set-Cookie: onecookie=data;\r\n"
                "Set-Cookie: secondcookie=2data;\r\n"
                "Set-Cookie: cookie3=data3;\r\n"
                "Blank:\r\n"
                "Blank2:\r\n"
                "Location: /19400002\r\n"
                "\r\n"
            ).encode("iso-8859-1", errors="replace")
            self.wfile.write(raw)  # type: ignore[attr-defined]
            self._write_log(200, {"Location": "/19400002", "Set-Cookie": "cookie3=data3;"})
            return True

        self.send_response(200)  # type: ignore[attr-defined]
        self.send_header("Content-Type", "text/plain")  # type: ignore[attr-defined]
        self.send_header("Content-Length", "0")  # type: ignore[attr-defined]
        self.send_header("Set-Cookie", "a=1; Path=/")  # type: ignore[attr-defined]
        self.send_header("Set-Cookie", "b=2; Path=/")  # type: ignore[attr-defined]
        self.send_header("X-Dupe", "1")  # type: ignore[attr-defined]
        self.send_header("X-Dupe", "2")  # type: ignore[attr-defined]
        self.send_header("X-Case", "A")  # type: ignore[attr-defined]
        self.send_header("x-case", "b")  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self._write_log(200, {"Set-Cookie": "b=2; Path=/"})
        return True

    def _handle_redirect_chain(self) -> bool:
        """Handle /redir/<n> multi-hop redirects."""

        if not self.path.startswith("/redir/"):
            return False
        try:
            seg = self.path.split("/", 3)[2]
            n = int(seg.split("?", 1)[0])
        except Exception:
            n = 0

        q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
        req_id = q.get("id", [""])[0]
        if n > 0:
            next_path = f"/redir/{n - 1}"
            if req_id:
                next_path = f"{next_path}?id={req_id}"
            self.send_response(302)  # type: ignore[attr-defined]
            self.send_header("Location", next_path)  # type: ignore[attr-defined]
            self.send_header("Content-Length", "0")  # type: ignore[attr-defined]
            self.end_headers()  # type: ignore[attr-defined]
            self._write_log(302, {"Location": next_path})
            return True

        body = b"redirect-ok\n"
        self.send_response(200)  # type: ignore[attr-defined]
        self.send_header("Content-Type", "text/plain")  # type: ignore[attr-defined]
        self.send_header("Content-Length", str(len(body)))  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self.wfile.write(body)  # type: ignore[attr-defined]
        self._write_log(200, {})
        return True
