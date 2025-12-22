#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
libcurl_consistency WS 场景服务端（LC-19/LC-20）：
- 默认行为：echo server（文本/二进制原样回显）
- 场景模式：通过 query 参数 `scenario=<name>` 触发固定帧序列（用于一致性对比）
- 观测输出：
  - handshake JSONL：记录稳定的握手头白名单（供请求语义摘要使用）
  - events JSONL：记录服务端侧关键事件（仅用于 debug，不作为默认对比基准）
"""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional
from urllib.parse import parse_qs, urlsplit

from websockets import server
from websockets.exceptions import ConnectionClosedError


log = logging.getLogger(__name__)


@dataclass(frozen=True)
class WsHandshakeLog:
    ts: str
    path: str
    id: str
    headers: dict


HANDSHAKE_LOG_FILE: Optional[Path] = None
EVENTS_LOG_FILE: Optional[Path] = None


def _append_jsonl(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as fp:
        fp.write(json.dumps(payload, ensure_ascii=False) + "\n")


def _log_event(req_id: str, scenario: str, event: str, **extra: object) -> None:
    if EVENTS_LOG_FILE is None:
        return
    _append_jsonl(
        EVENTS_LOG_FILE,
        {
            "ts": datetime.now(tz=timezone.utc).isoformat(),
            "id": req_id,
            "scenario": scenario,
            "event": event,
            **extra,
        },
    )


async def _ping_and_wait_pong(websocket, payload: bytes, *, timeout_s: float) -> bool:
    waiter = websocket.ping(payload)
    if asyncio.iscoroutine(waiter):
        waiter = await waiter
    try:
        await asyncio.wait_for(waiter, timeout=timeout_s)
        return True
    except asyncio.TimeoutError:
        return False


async def _run_scenario(websocket, scenario: str, req_id: str) -> None:
    if scenario == "lc_ping":
        payload = b""
        _log_event(req_id, scenario, "server_ping_sent", payload_hex="")
        ok = await _ping_and_wait_pong(websocket, payload, timeout_s=5.0)
        _log_event(req_id, scenario, "server_pong_ok" if ok else "server_pong_timeout")
        if not ok:
            await websocket.close(code=1011, reason="pong timeout")
            return
        await websocket.close(code=1000, reason="done")
        _log_event(req_id, scenario, "server_close_sent", close_code=1000, reason="done")
        return

    if scenario == "lc_frame_types":
        await websocket.send("txt")
        _log_event(req_id, scenario, "server_text_sent", text="txt")
        await websocket.send(b"bin")
        _log_event(req_id, scenario, "server_binary_sent", payload_hex="62696e")

        ping_payload = b"ping"
        _log_event(req_id, scenario, "server_ping_sent", payload_hex=ping_payload.hex())
        ok = await _ping_and_wait_pong(websocket, ping_payload, timeout_s=5.0)
        _log_event(req_id, scenario, "server_pong_ok" if ok else "server_pong_timeout")
        if not ok:
            await websocket.close(code=1011, reason="pong timeout")
            return

        await websocket.pong(b"pong")
        _log_event(req_id, scenario, "server_pong_sent", payload_hex="706f6e67")

        await websocket.close(code=1000, reason="close")
        _log_event(req_id, scenario, "server_close_sent", close_code=1000, reason="close")
        return

    _log_event(req_id, scenario, "server_unknown_scenario")
    await websocket.close(code=1008, reason="unknown scenario")


async def handler(websocket):
    path = getattr(websocket, "path", "/")
    q = parse_qs(urlsplit(path).query)
    req_id = q.get("id", [""])[0]
    scenario = q.get("scenario", [""])[0]

    if HANDSHAKE_LOG_FILE is not None:
        req_headers = getattr(websocket, "request_headers", None)
        headers_allowlist = [
            "upgrade",
            "connection",
            "sec-websocket-version",
            "sec-websocket-extensions",
            "host",
        ]
        headers = {}
        if req_headers is not None:
            for name in headers_allowlist:
                v = req_headers.get(name)
                if v is not None:
                    headers[name] = v
        log_entry = WsHandshakeLog(
            ts=datetime.now(tz=timezone.utc).isoformat(),
            path=path,
            id=req_id,
            headers=headers,
        )
        _append_jsonl(HANDSHAKE_LOG_FILE, asdict(log_entry))

    if scenario:
        await _run_scenario(websocket, scenario, req_id)
        return

    try:
        async for message in websocket:
            await websocket.send(message)
    except ConnectionClosedError:
        pass


async def run_server(port: int, handshake_log: Optional[Path], events_log: Optional[Path]) -> None:
    global HANDSHAKE_LOG_FILE
    global EVENTS_LOG_FILE
    HANDSHAKE_LOG_FILE = handshake_log
    EVENTS_LOG_FILE = events_log
    async with server.serve(handler, "localhost", port):
        await asyncio.Future()


def main() -> int:
    parser = argparse.ArgumentParser(description="Run a WebSocket echo/scenario server for libcurl_consistency")
    parser.add_argument("--port", type=int, default=9876, help="port to listen on")
    parser.add_argument("--handshake-log", type=str, default="", help="optional JSONL file for handshake observations")
    parser.add_argument("--events-log", type=str, default="", help="optional JSONL file for scenario events")
    args = parser.parse_args()

    logging.basicConfig(
        format="%(asctime)s %(levelname)s %(message)s",
        level=logging.INFO,
    )

    handshake_log = Path(args.handshake_log) if args.handshake_log else None
    events_log = Path(args.events_log) if args.events_log else None
    asyncio.run(run_server(args.port, handshake_log, events_log))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
