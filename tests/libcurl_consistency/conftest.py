"""
Pytest 驱动骨架（LC-2）：
- 复用 `curl/tests/http/testenv` 的服务端/端口分配。
- 暴露 httpd/nghttpx/ws 统一入口，供后续 baseline 与 QCurl 客户端复用。
"""

from __future__ import annotations

import logging
import os
import socket
import subprocess
import sys
import time
from datetime import datetime, timedelta
from pathlib import Path
from typing import Dict, Generator
import uuid

import pytest

# 将上游 http 测试目录放入 sys.path，直接复用 testenv 组件与 fixtures
_REPO_ROOT = Path(__file__).resolve().parents[2]
CURL_HTTP_DIR = _REPO_ROOT / "curl" / "tests" / "http"
if str(CURL_HTTP_DIR) not in sys.path:
    sys.path.insert(0, str(CURL_HTTP_DIR))

# 为 curl/tests/http/testenv 注入 out-of-source build 目录与二进制路径（若用户未显式设置）
os.environ.setdefault("CURL_BUILD_DIR", str(_REPO_ROOT / "curl" / "build"))
os.environ.setdefault("CURL", str(_REPO_ROOT / "curl" / "build" / "src" / "curl"))
os.environ.setdefault("CURLINFO", str(_REPO_ROOT / "curl" / "build" / "src" / "curlinfo"))

TESTENV_IMPORT_ERROR = None
try:
    from testenv import Env, CurlClient  # noqa: E402
    from testenv.ports import alloc_ports_and_do  # noqa: E402
except Exception as exc:  # pragma: no cover - defensive guard for missing config/binaries
    TESTENV_IMPORT_ERROR = exc

log = logging.getLogger(__name__)

# 加载上游 pytest 插件（提供 env/httpd/nghttpx/ssh 等 fixtures）
pytest_plugins = ["conftest"]

# 如果 testenv 无法导入（如缺少 config.ini 或 httpd/nghttpx），跳过本模块所有测试
if TESTENV_IMPORT_ERROR:
    pytest.skip(f"testenv unavailable: {TESTENV_IMPORT_ERROR}", allow_module_level=True)


@pytest.fixture(scope="session")
def worker_id() -> str:
    """兼容上游 pytest-xdist 的 worker_id fixture。"""
    return os.environ.get("PYTEST_XDIST_WORKER", "master")


@pytest.fixture(scope="session")
def testrun_uid() -> str:
    """为上游 testenv 提供本次测试运行的唯一标识。"""
    return os.environ.get("CURL_TESTRUN_UID", uuid.uuid4().hex[:8])


@pytest.fixture(scope="session", autouse=True)
def lc_seed_http_docs(env, httpd):
    """
    为一致性用例准备最小的静态资源文件（对齐上游 test_02_download.py 的 class-scope fixture）。
    """
    indir = httpd.docs_dir
    env.make_data_file(indir=indir, fname="data-1m", fsize=1024 * 1024)
    env.make_data_file(indir=indir, fname="data-10m", fsize=10 * 1024 * 1024)
    cookie_dir = Path(indir) / "we" / "want"
    cookie_dir.mkdir(parents=True, exist_ok=True)
    (cookie_dir / "1903").write_text("cookie-1903\n", encoding="utf-8")
    yield True


def _check_ws_alive(env: Env, port: int, timeout: int) -> bool:
    """轮询 ws echo server 是否可达（使用 curl/http GET 探活）。"""
    curl = CurlClient(env=env)
    url = f"http://localhost:{port}/"
    end = datetime.now() + timedelta(seconds=timeout)
    while datetime.now() < end:
        res = curl.http_download(urls=[url])
        if res.exit_code == 0:
            return True
        time.sleep(0.1)
    return False


@pytest.fixture(scope="session")
def lc_ws_echo(env: Env) -> Generator[Dict[str, int], None, None]:
    """
    启动 WebSocket echo server。
    - 复用上游 `ws_echo_server.py`
    - 分配端口后写回 env.ws_port，便于客户端读取
    """
    if not Env.curl_has_protocol("ws"):
        pytest.skip("curl lacks ws protocol support")

    run_dir = Path(env.gen_dir) / "ws_echo_server"
    cmd = Path(env.project_dir) / "tests" / "http" / "testenv" / "ws_echo_server.py"
    run_dir.mkdir(parents=True, exist_ok=True)
    log_file = run_dir / "ws_handshake.jsonl"

    proc = None
    with open(run_dir / "stderr", "w") as cerr:
        def startup(ports: Dict[str, int]) -> bool:
            nonlocal proc
            args = [str(cmd), "--port", str(ports["ws"]), "--log-file", str(log_file)]
            log.info("start ws echo: %s", args)
            proc = subprocess.Popen(
                args=args,
                cwd=str(run_dir),
                stderr=cerr,
                stdout=cerr,
            )
            if _check_ws_alive(env, ports["ws"], Env.SERVER_TIMEOUT):
                env.update_ports(ports)
                return True
            log.error("ws echo failed to start")
            proc.terminate()
            proc = None
            return False

        ok = alloc_ports_and_do({"ws": socket.SOCK_STREAM}, startup, env.gen_root, max_tries=3)
        if not ok or proc is None:
            pytest.skip("ws echo server did not start")
        try:
            yield {"port": env.ws_port, "log_file": str(log_file)}
        finally:
            if proc:
                proc.terminate()


@pytest.fixture(scope="session")
def lc_services(env, httpd, nghttpx, lc_ws_echo):
    """
    汇总可用服务实例，便于测试用例按需取用。
    - httpd: h1/h2
    - nghttpx: h3
    - lc_ws_echo: ws echo server
    """
    return {
        "env": env,
        "httpd": httpd,
        "nghttpx": nghttpx,
        "ws": lc_ws_echo,
    }


@pytest.fixture(scope="session")
def lc_logs(httpd, nghttpx, lc_ws_echo):
    """
    提供服务端日志路径，便于失败时收集：
    - httpd：logs_dir/error_log
    - nghttpx：nghttpx.log/stderr
    """
    logs = {}
    if httpd:
        # 访问内部路径用于调试，不作为公共 API 暴露
        logs["httpd_logs_dir"] = Path(httpd._logs_dir)  # type: ignore[attr-defined]
        logs["httpd_error_log"] = Path(httpd._error_log)  # type: ignore[attr-defined]
        logs["httpd_access_log"] = Path(httpd._access_log)  # type: ignore[attr-defined]
    if nghttpx:
        logs["nghttpx_log"] = Path(nghttpx._error_log)  # type: ignore[attr-defined]
        logs["nghttpx_stderr"] = Path(nghttpx._stderr)  # type: ignore[attr-defined]
    if lc_ws_echo and "log_file" in lc_ws_echo:
        logs["ws_handshake_log"] = Path(str(lc_ws_echo["log_file"]))
    return logs


def collect_service_logs(logs: Dict[str, Path], dest: Path) -> Dict[str, str]:
    """
    将 httpd/nghttpx 等日志复制到目标目录，返回相对路径映射。
    仅在调用方需要调试时使用，默认不自动复制以避免噪声。
    """
    dest.mkdir(parents=True, exist_ok=True)
    copied = {}
    for name, path in logs.items():
        if path.exists():
            target = dest / path.name
            target.write_bytes(path.read_bytes())
            copied[name] = str(target)
    return copied
