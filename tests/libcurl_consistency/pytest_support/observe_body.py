"""Request body and generic route helpers for the observable test server."""

from __future__ import annotations

from urllib.parse import parse_qs, urlsplit
import socket
import time


class ObserveBodyMixin:
    """Reusable body reading and generic response helpers for BaseHTTPRequestHandler."""

    path: str
    headers: object
    connection: socket.socket
    rfile: object
    wfile: object

    def _write_log(self, status: int, response_headers: dict[str, str], request_body: bytes = b"") -> None:
        raise NotImplementedError

    def _drain_request_body_best_effort(self) -> int:
        """Best-effort drain current request body without blocking indefinitely."""

        try:
            length = int(self.headers.get("content-length") or "0")  # type: ignore[attr-defined]
        except ValueError:
            return 0
        if length <= 0:
            return 0

        sock = getattr(self, "connection", None)
        if sock is None:
            return 0

        drained = 0
        original_timeout = sock.gettimeout()
        try:
            sock.settimeout(0.05)
            idle_deadline_s = 0.75
            hard_deadline_s = 3.0
            started = time.monotonic()
            last_progress = started
            while drained < length and (time.monotonic() - started) < hard_deadline_s:
                remaining = min(64 * 1024, length - drained)
                try:
                    chunk = self.rfile.read1(remaining)  # type: ignore[attr-defined]
                except (BlockingIOError, InterruptedError, TimeoutError, socket.timeout):
                    if (time.monotonic() - last_progress) >= idle_deadline_s:
                        break
                    time.sleep(0.005)
                    continue
                except OSError:
                    break
                if not chunk:
                    break
                drained += len(chunk)
                last_progress = time.monotonic()
        finally:
            try:
                sock.settimeout(original_timeout)
            except Exception:
                pass
        return drained

    def _read_chunked_body(self) -> bytes:
        """Read a minimal HTTP/1.1 chunked request body for test observation."""

        out = bytearray()
        max_total = 64 * 1024 * 1024

        while True:
            line = self.rfile.readline(65536)  # type: ignore[attr-defined]
            if not line:
                raise ValueError("chunked body: unexpected EOF while reading chunk size")

            raw = line.strip()
            if not raw:
                continue
            size_hex = raw.split(b";", 1)[0].strip()
            try:
                n = int(size_hex, 16)
            except ValueError as exc:
                raise ValueError(f"chunked body: invalid chunk size line: {raw!r}") from exc

            if n == 0:
                while True:
                    trailer = self.rfile.readline(65536)  # type: ignore[attr-defined]
                    if not trailer or trailer in (b"\r\n", b"\n"):
                        break
                break

            chunk = self.rfile.read(n)  # type: ignore[attr-defined]
            if len(chunk) != n:
                raise ValueError("chunked body: unexpected EOF while reading chunk data")

            crlf = self.rfile.read(2)  # type: ignore[attr-defined]
            if crlf != b"\r\n":
                raise ValueError("chunked body: missing CRLF after chunk data")

            out.extend(chunk)
            if len(out) > max_total:
                raise ValueError("chunked body: body too large")

        return bytes(out)

    def _read_body(self) -> bytes:
        """Read the current request body."""

        te = str(self.headers.get("transfer-encoding") or "")  # type: ignore[attr-defined]
        if "chunked" in te.lower():
            return self._read_chunked_body()

        try:
            length = int(self.headers.get("content-length") or "0")  # type: ignore[attr-defined]
        except ValueError:
            length = 0
        if length <= 0:
            return b""
        return self.rfile.read(length)  # type: ignore[attr-defined]

    def _handle_method_redirect(self, status: int) -> bool:
        """Redirect method/body cases to their matching target endpoint."""

        body = self._read_body()
        q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
        req_id = q.get("id", [""])[0]
        next_path = f"/redir_target_{status}"
        if req_id:
            next_path = f"{next_path}?id={req_id}"
        self.send_response(status)  # type: ignore[attr-defined]
        self.send_header("Location", next_path)  # type: ignore[attr-defined]
        self.send_header("Content-Length", "0")  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self._write_log(status, {"Location": next_path}, body)
        return bool(body) or True

    def _handle_redirect_target(self, status: int) -> bool:
        """Return a redirect target response and echo body when present."""

        body = self._read_body()
        resp = f"redir-{status}-ok\n".encode("utf-8")
        if body:
            resp = body
        self.send_response(200)  # type: ignore[attr-defined]
        self.send_header("Content-Type", "application/octet-stream")  # type: ignore[attr-defined]
        self.send_header("Content-Length", str(len(resp)))  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        self.wfile.write(resp)  # type: ignore[attr-defined]
        self._write_log(200, {}, body)
        return True

    def _handle_range_boundary(self) -> bool:
        """Serve deterministic range-boundary scenarios."""

        q = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
        scenario = (q.get("scenario", ["n_dash"])[0] or "n_dash").strip().lower()
        total = 32
        body = bytes((ord("a") + (i % 26)) for i in range(total))
        range_header = str(self.headers.get("range") or "").strip()  # type: ignore[attr-defined]

        if scenario == "complete_416":
            self.send_response(416)  # type: ignore[attr-defined]
            self.send_header("Content-Range", f"bytes */{total}")  # type: ignore[attr-defined]
            self.send_header("Content-Length", "0")  # type: ignore[attr-defined]
            self.end_headers()  # type: ignore[attr-defined]
            self._write_log(416, {"Content-Range": f"bytes */{total}", "Content-Length": "0"})
            return True

        if scenario == "mismatch_start":
            start = 5
            content = body[start:]
            self.send_response(206)  # type: ignore[attr-defined]
            self.send_header("Content-Range", f"bytes {start}-{total - 1}/{total}")  # type: ignore[attr-defined]
            self.send_header("Content-Type", "application/octet-stream")  # type: ignore[attr-defined]
            self.send_header("Content-Length", str(len(content)))  # type: ignore[attr-defined]
            self.end_headers()  # type: ignore[attr-defined]
            self.wfile.write(content)  # type: ignore[attr-defined]
            self._write_log(
                206,
                {"Content-Range": f"bytes {start}-{total - 1}/{total}", "Content-Length": str(len(content))},
            )
            return True

        start = 0
        if range_header.lower().startswith("bytes=") and range_header.endswith("-"):
            try:
                start = int(range_header[len("bytes="):-1])
            except ValueError:
                start = 0
        start = max(0, min(start, total))
        content = body[start:]
        status = 206 if range_header else 200
        self.send_response(status)  # type: ignore[attr-defined]
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{total - 1}/{total}")  # type: ignore[attr-defined]
        self.send_header("Content-Type", "application/octet-stream")  # type: ignore[attr-defined]
        self.send_header("Content-Length", str(len(content)))  # type: ignore[attr-defined]
        self.end_headers()  # type: ignore[attr-defined]
        if content:
            self.wfile.write(content)  # type: ignore[attr-defined]
        resp_headers = {"Content-Length": str(len(content))}
        if status == 206:
            resp_headers["Content-Range"] = f"bytes {start}-{total - 1}/{total}"
        self._write_log(status, resp_headers)
        return True
