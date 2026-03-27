"""
Pytest 驱动骨架（LC-2）：
- 复用 `curl/tests/http/testenv` 的服务端/端口分配。
- 暴露 httpd/nghttpx/ws 统一入口，供后续 baseline 与 QCurl 客户端复用。
"""

from __future__ import annotations

import importlib.util
import logging
import os
import socket
import subprocess
import sys
import time
from datetime import datetime, timedelta
from pathlib import Path
from types import ModuleType
from typing import Dict, Generator
import uuid

import pytest

# 将上游 http 测试目录放入 sys.path，直接复用 testenv 组件与 fixtures
_REPO_ROOT = Path(__file__).resolve().parents[2]
# curl testenv 会在 import 阶段实例化 Env.CONFIG（会执行 ${TOP_PATH}/src/curlinfo）。
# 因此必须先将 cwd 切到 out-of-source 的 `<qcurl_build>/curl/src`（默认 build/curl/src），
# 让其推导 TOP_PATH=<qcurl_build>/curl。
_DEFAULT_CURL_BUILD_DIR = _REPO_ROOT / "build" / "curl"
_CURL_BUILD_DIR_FROM_ENV = Path(os.environ.get("CURL_BUILD_DIR", str(_DEFAULT_CURL_BUILD_DIR))).resolve()
_CURL_BUILD_SRC_DIR = _CURL_BUILD_DIR_FROM_ENV / "src"
_TESTENV_IMPORT_CWD = _CURL_BUILD_SRC_DIR if _CURL_BUILD_SRC_DIR.exists() else _REPO_ROOT
os.chdir(str(_TESTENV_IMPORT_CWD))
CURL_HTTP_DIR = _REPO_ROOT / "curl" / "tests" / "http"
if str(CURL_HTTP_DIR) not in sys.path:
    sys.path.insert(0, str(CURL_HTTP_DIR))

# 为 curl/tests/http/testenv 注入 out-of-source build 目录与二进制路径（若用户未显式设置）
os.environ.setdefault("CURL_BUILD_DIR", str(_DEFAULT_CURL_BUILD_DIR))
os.environ.setdefault("CURL", str(_DEFAULT_CURL_BUILD_DIR / "src" / "curl"))

TESTENV_IMPORT_ERROR = None
try:
    from testenv import Env, CurlClient  # noqa: E402
    from testenv.ports import alloc_ports_and_do  # noqa: E402
    from testenv.env import EnvConfig  # noqa: E402
    from testenv import Httpd, NghttpxQuic  # noqa: E402
    import testenv.env as testenv_env  # noqa: E402
    import testenv.nghttpx as testenv_nghttpx  # noqa: E402
except Exception as exc:  # pragma: no cover - defensive guard for missing config/binaries
    TESTENV_IMPORT_ERROR = exc
finally:
    # 将后续测试过程的 cwd 固定回 repo root，避免相对路径（QCURL_QTTEST 等）解析混乱。
    os.chdir(str(_REPO_ROOT))

log = logging.getLogger(__name__)

def _inject_upstream_curl_http_fixtures() -> None:
    """
    将 `curl/tests/http/conftest.py` 里的 fixtures/hook 注入到当前 conftest 中。

    说明：
    - pytest 8+ 不再支持在“非顶层 conftest”里声明 `pytest_plugins = [...]`。
    - 上游 conftest 里的 session autouse fixture（env）不应影响本仓库其它 pytest 用例，
      因此必须保持“目录级作用域”（仅对本目录 tests/libcurl_consistency 生效）。
    - 这里通过“导入上游 conftest 作为普通模块 + 复制 fixtures/hook 到当前模块”的方式，
      达到与旧 `pytest_plugins = ["conftest"]` 等价的行为，但不触发 pytest 的硬错误。
    """

    upstream_path = CURL_HTTP_DIR / "conftest.py"
    if not upstream_path.exists():
        raise RuntimeError(f"missing upstream conftest: {upstream_path}")

    spec = importlib.util.spec_from_file_location(
        "_qcurl_upstream_curl_http_conftest",
        upstream_path,
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load upstream conftest spec: {upstream_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)  # type: ignore[attr-defined]
    upstream = module
    assert isinstance(upstream, ModuleType)

    for name, obj in upstream.__dict__.items():
        # 仅注入非私有符号；保持本地覆盖优先（例如 env_config / worker_id / testrun_uid 等）
        if name.startswith("_") or name in globals():
            continue
        globals()[name] = obj

# 如果 testenv 无法导入（如缺少 config.ini 或 httpd/nghttpx），跳过本模块所有测试
if TESTENV_IMPORT_ERROR:
    pytest.skip(f"testenv unavailable: {TESTENV_IMPORT_ERROR}", allow_module_level=True)

_CURL_BUILD_DIR = Path(os.environ.get("CURL_BUILD_DIR", str(_DEFAULT_CURL_BUILD_DIR))).resolve()
_CURLINFO_BIN = _CURL_BUILD_DIR / "src" / "curlinfo"
_QCURL_BUILD_DIR = _CURL_BUILD_DIR.parent
_NGHTTPX_H3_BIN = _QCURL_BUILD_DIR / "libcurl_consistency" / "nghttpx-h3" / "bin" / "nghttpx"

def _ensure_qcurl_qttest_env() -> None:
    """
    为 QCURL_QTTEST 提供“可用即用”的默认值，并确保为绝对路径。

    背景：
    - pytest 的 Qt 执行器会在 artifacts 子目录下以 cwd=run_dir 启动子进程；
      若 QCURL_QTTEST 为相对路径，将导致 FileNotFoundError（相对路径不再指向 repo root）。
    - 允许用户显式覆盖 QCURL_QTTEST；但若用户提供相对路径，将按 repo root 解析并转为绝对路径。
    """
    raw = os.environ.get("QCURL_QTTEST", "").strip()
    if raw:
        p = Path(raw).expanduser()
        if not p.is_absolute():
            p = (_REPO_ROOT / p).resolve()
        else:
            p = p.resolve()
        os.environ["QCURL_QTTEST"] = str(p)
        return

    candidates = [
        _QCURL_BUILD_DIR / "tests" / "tst_LibcurlConsistency",
        _QCURL_BUILD_DIR / "tests" / "tst_LibcurlConsistency.exe",
    ]
    for c in candidates:
        if c.exists():
            os.environ["QCURL_QTTEST"] = str(c.resolve())
            return

    # 兜底：用户可能使用 build_xxx 作为构建目录（如 build_lc / build-debug），按 build*/tests 约定探测。
    for build_dir in sorted(_REPO_ROOT.glob("build*")):
        c = build_dir / "tests" / "tst_LibcurlConsistency"
        if c.exists():
            os.environ["QCURL_QTTEST"] = str(c.resolve())
            return
        c_exe = c.with_suffix(".exe")
        if c_exe.exists():
            os.environ["QCURL_QTTEST"] = str(c_exe.resolve())
            return


def _patch_testenv_curlinfo_path() -> None:
    """
    curl testenv 的 EnvConfig 不支持通过环境变量覆盖 curlinfo 路径，只能改模块常量。
    这里将其指向 out-of-source 的 <qcurl_build>/curl/src/curlinfo（默认 build/curl/src/curlinfo），避免 import 时 cwd 影响。
    """
    if _CURLINFO_BIN.exists():
        testenv_env.CURLINFO = str(_CURLINFO_BIN)

def _override_testenv_nghttpx_bin() -> None:
    """
    curl testenv 默认会优先读取 <qcurl_build>/curl/tests/http/config.ini（其中 nghttpx 通常指向 /usr/bin/nghttpx）。
    为了覆盖 HTTP/3 场景，这里在不改 curl/tests/... 的前提下，将 Env.CONFIG.nghttpx 指向本仓库构建的 h3-capable nghttpx。
    """
    if not _NGHTTPX_H3_BIN.exists():
        return
    Env.CONFIG.nghttpx = str(_NGHTTPX_H3_BIN)
    try:
        ver = testenv_env.NghttpxUtil.version(Env.CONFIG.nghttpx)
        Env.CONFIG._nghttpx_version = ver
        Env.CONFIG.nghttpx_with_h3 = testenv_env.NghttpxUtil.version_with_h3(ver)
    except Exception:
        # 不阻断：没有 nghttpx 时保持 have_h3() 为 False 即可
        Env.CONFIG.nghttpx = None
        Env.CONFIG._nghttpx_version = None
        Env.CONFIG.nghttpx_with_h3 = False


def _patch_httpd_access_log() -> None:
    """
    为 httpd 注入结构化 access_log（供“请求语义摘要”使用）。
    由于上游 server_reset 每个用例都会 reset_config()，这里通过 monkeypatch 保持默认配置恒有 access_log。
    """
    if getattr(Httpd, "_qcurl_lc_access_log_patched", False):
        return
    Httpd._qcurl_lc_access_log_patched = True  # type: ignore[attr-defined]

    original_reset_config = Httpd.reset_config
    original_clear_logs = Httpd.clear_logs

    def access_log_path(self: Httpd) -> str:
        return os.path.join(getattr(self, "_logs_dir"), "access_log")

    def reset_config_with_access_log(self: Httpd):  # type: ignore[no-untyped-def]
        original_reset_config(self)
        fmt_name = "qcurl_lc_access"
        self.set_extra_config(
            "base",
            [
                'LogFormat "%{%Y-%m-%dT%H:%M:%S%z}t|%H|%m|%U%q|%>s|%{Range}i|%{Content-Length}i" '
                + fmt_name,
                f'CustomLog "{access_log_path(self)}" {fmt_name}',
            ],
        )

    def clear_logs_with_access_log(self: Httpd):  # type: ignore[no-untyped-def]
        original_clear_logs(self)
        try:
            os.remove(access_log_path(self))
        except FileNotFoundError:
            pass

    Httpd.reset_config = reset_config_with_access_log  # type: ignore[assignment]
    Httpd.clear_logs = clear_logs_with_access_log  # type: ignore[assignment]


def _patch_nghttpx_access_log() -> None:
    """
    为 nghttpx-quic 注入结构化 access_log（供 h3 的“请求语义摘要/协议族”观测使用）。
    不修改 curl/tests/，通过 monkeypatch NghttpxQuic.start() 增加 --accesslog-* 参数。
    """
    if getattr(NghttpxQuic, "_qcurl_lc_access_log_patched", False):
        return
    NghttpxQuic._qcurl_lc_access_log_patched = True  # type: ignore[attr-defined]

    original_start = NghttpxQuic.start
    original_clear_logs = testenv_nghttpx.Nghttpx.clear_logs

    def access_log_path(self: NghttpxQuic) -> str:
        return os.path.join(getattr(self, "_run_dir"), "access_log")

    def clear_logs_with_access_log(self):  # type: ignore[no-untyped-def]
        original_clear_logs(self)
        try:
            os.remove(access_log_path(self))
        except FileNotFoundError:
            pass

    def start_with_access_log(self: NghttpxQuic, wait_live=True):  # type: ignore[no-untyped-def]
        # 复用上游实现，但在启动参数中追加 accesslog
        if not hasattr(self, "supports_h3"):
            return original_start(self, wait_live=wait_live)

        self._mkpath(getattr(self, "_tmp_dir"))
        if getattr(self, "_process", None):
            self.stop()

        creds = self.env.get_credentials(getattr(self, "_cred_name"))
        assert creds
        self._loaded_cred_name = getattr(self, "_cred_name")

        fmt = "$time_iso8601|$alpn|$method|$path|$status|$http_range|$http_content_length"
        args = [
            getattr(self, "_cmd"),
            f"--frontend=*,{getattr(self, '_port')};tls",
            f"--accesslog-file={access_log_path(self)}",
            f"--accesslog-format={fmt}",
        ]
        if self.supports_h3():
            args.extend([
                f"--frontend=*,{self.env.h3_port};quic",
                "--frontend-quic-early-data",
            ])
        args.extend([
            "--workers=1",
            f"--backend=127.0.0.1,{self.env.http_port}",
            "--log-level=ERROR",
            f"--pid-file={getattr(self, '_pid_file')}",
            f"--errorlog-file={getattr(self, '_error_log')}",
            f"--conf={getattr(self, '_conf_file')}",
            f"--cacert={self.env.ca.cert_file}",
            creds.pkey_file,
            creds.cert_file,
            "--frontend-http3-window-size=1M",
            "--frontend-http3-max-window-size=10M",
            "--frontend-http3-connection-window-size=10M",
            "--frontend-http3-max-connection-window-size=100M",
        ])
        ngerr = open(getattr(self, "_stderr"), "a")
        self._process = subprocess.Popen(args=args, stderr=ngerr)
        if self._process.returncode is not None:
            return False
        return (not wait_live) or self.wait_live(timeout=timedelta(seconds=Env.SERVER_TIMEOUT))

    NghttpxQuic.start = start_with_access_log  # type: ignore[assignment]
    testenv_nghttpx.Nghttpx.clear_logs = clear_logs_with_access_log  # type: ignore[assignment]


_patch_testenv_curlinfo_path()
_override_testenv_nghttpx_bin()
_patch_httpd_access_log()
_patch_nghttpx_access_log()
_ensure_qcurl_qttest_env()


@pytest.fixture(scope="session")
def worker_id() -> str:
    """兼容上游 pytest-xdist 的 worker_id fixture。"""
    return os.environ.get("PYTEST_XDIST_WORKER", "master")


@pytest.fixture(scope="session")
def testrun_uid() -> str:
    """为上游 testenv 提供本次测试运行的唯一标识。"""
    return os.environ.get("CURL_TESTRUN_UID", uuid.uuid4().hex[:8])


@pytest.fixture(scope="session")
def env_config(pytestconfig, testrun_uid, worker_id) -> EnvConfig:
    """
    覆盖上游 EnvConfig：将 build_dir 指向 out-of-source 的 `<qcurl_build>/curl`（默认 build/curl），
    使 LocalClient(name=...) 能正确定位 `<qcurl_build>/curl/tests/libtest/libtests`。
    """
    cfg = EnvConfig(pytestconfig=pytestconfig, testrun_uid=testrun_uid, worker_id=worker_id)
    cfg.build_dir = str(_CURL_BUILD_DIR)
    if _NGHTTPX_H3_BIN.exists():
        cfg.nghttpx = str(_NGHTTPX_H3_BIN)
        try:
            ver = testenv_env.NghttpxUtil.version(cfg.nghttpx)
            cfg._nghttpx_version = ver  # type: ignore[attr-defined]
            cfg.nghttpx_with_h3 = testenv_env.NghttpxUtil.version_with_h3(ver)
        except Exception:
            cfg.nghttpx = None
            cfg._nghttpx_version = None  # type: ignore[attr-defined]
            cfg.nghttpx_with_h3 = False
    return cfg


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

    proxy_dir = Path(indir) / "proxy"
    proxy_dir.mkdir(parents=True, exist_ok=True)
    (proxy_dir / "ok.txt").write_text("proxy-ok\n", encoding="utf-8")

    # ext：多资源 GET（test2402/test2502 风格）
    path_dir = Path(indir) / "path"
    path_dir.mkdir(parents=True, exist_ok=True)
    multi_body = "file contents should appear once for each file\n"
    for prefix in ("2402", "2502"):
        for i in range(1, 5):
            (path_dir / f"{prefix}{i:04d}").write_text(multi_body, encoding="utf-8")

    # ext：TLS policy + cache（LC-50）：用于注入 Strict-Transport-Security / Alt-Svc 响应头并验证落盘
    (Path(indir) / "lc_cache_headers").write_text("cache-headers-ok\n", encoding="utf-8")
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


def _check_tcp_alive(port: int, timeout: int) -> bool:
    end = datetime.now() + timedelta(seconds=timeout)
    while datetime.now() < end:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.05)
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
    cmd = _REPO_ROOT / "tests" / "libcurl_consistency" / "ws_scenario_server.py"
    run_dir.mkdir(parents=True, exist_ok=True)
    handshake_log_file = run_dir / "ws_handshake.jsonl"
    events_log_file = run_dir / "ws_events.jsonl"

    proc = None
    with open(run_dir / "stderr", "w") as cerr:
        def startup(ports: Dict[str, int]) -> bool:
            nonlocal proc
            args = [
                sys.executable,
                str(cmd),
                "--port",
                str(ports["ws"]),
                "--handshake-log",
                str(handshake_log_file),
                "--events-log",
                str(events_log_file),
            ]
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
            yield {
                "port": env.ws_port,
                "log_file": str(handshake_log_file),
                "events_log_file": str(events_log_file),
            }
        finally:
            if proc:
                proc.terminate()


@pytest.fixture(scope="function")
def lc_http_proxy(env: Env) -> Generator[Dict[str, object], None, None]:
    """
    启动本地 HTTP proxy（Basic auth + CONNECT），供 P1 proxy 一致性用例使用。
    - 每个测试函数单独启动，避免跨 case 的日志混淆
    - 输出 JSONL 日志用于“请求语义摘要”对齐
    """
    run_dir = Path(env.gen_dir) / f"lc_http_proxy_{uuid.uuid4().hex[:8]}"
    cmd = _REPO_ROOT / "tests" / "libcurl_consistency" / "http_proxy_server.py"
    run_dir.mkdir(parents=True, exist_ok=True)
    log_file = run_dir / "proxy_requests.jsonl"

    username = "lcuser"
    password = "lcpass"

    proc = None
    proxy_port = 0
    with open(run_dir / "stderr", "w") as cerr:
        def startup(ports: Dict[str, int]) -> bool:
            nonlocal proc, proxy_port
            log_file.write_text("", encoding="utf-8")
            proxy_port = int(ports["proxy"])
            args = [
                sys.executable,
                str(cmd),
                "--port",
                str(ports["proxy"]),
                "--log-file",
                str(log_file),
                "--username",
                username,
                "--password",
                password,
            ]
            log.info("start http proxy: %s", args)
            proc = subprocess.Popen(
                args=args,
                cwd=str(run_dir),
                stderr=cerr,
                stdout=cerr,
            )
            if _check_tcp_alive(ports["proxy"], Env.SERVER_TIMEOUT):
                return True
            log.error("http proxy failed to start")
            proc.terminate()
            proc = None
            return False

        ok = alloc_ports_and_do({"proxy": socket.SOCK_STREAM}, startup, env.gen_root, max_tries=3)
        if not ok or proc is None:
            pytest.skip("http proxy server did not start")
        try:
            yield {
                "port": proxy_port,
                "log_file": str(log_file),
                "username": username,
                "password": password,
            }
        finally:
            if proc:
                proc.terminate()

@pytest.fixture(scope="function")
def lc_httpd_cache_headers(env: Env, httpd) -> Generator[Dict[str, object], None, None]:
    """
    为 ext cache 用例注入响应头：
    - Strict-Transport-Security
    - Alt-Svc

    注意：
    - 仅对 https vhost 生效（通过 domain extra config 注入到 <VirtualHost *:https> 内）。
    - 用例结束必须清理，避免污染其他 case。
    """
    if not httpd:
        pytest.skip("httpd unavailable")
    domain = getattr(env, "domain1", None)
    if not domain:
        pytest.skip("curl testenv domain1 unavailable")

    alt_svc_port = int(getattr(env, "h3_port", 0) or getattr(env, "https_port", 0) or 0)
    if alt_svc_port <= 0:
        pytest.skip("ports unavailable for Alt-Svc header")

    lines = [
        '<Location "/lc_cache_headers">',
        '    Header always set Strict-Transport-Security "max-age=60"',
        f'    Header always set Alt-Svc \'h3=":{alt_svc_port}"; ma=60\'',
        "</Location>",
    ]
    httpd.set_extra_config(domain, lines)
    if not httpd.reload_if_config_changed():
        pytest.skip("httpd reload failed")
    try:
        yield {"alt_svc_port": alt_svc_port}
    finally:
        httpd.set_extra_config(domain, None)
        httpd.reload_if_config_changed()


@pytest.fixture(scope="function")
def lc_socks5_proxy(env: Env) -> Generator[Dict[str, object], None, None]:
    """
    启动最小 SOCKS5 stub proxy（固定返回失败），供 LC-39（SOCKS5 失败语义）使用。
    - 该 stub 不依赖外网，也不需要真实目标服务端
    - 通过 JSONL 日志可确认客户端确实走了 SOCKS5 握手路径
    """
    run_dir = Path(env.gen_dir) / f"lc_socks5_proxy_{uuid.uuid4().hex[:8]}"
    cmd = _REPO_ROOT / "tests" / "libcurl_consistency" / "socks5_proxy_server.py"
    run_dir.mkdir(parents=True, exist_ok=True)
    log_file = run_dir / "socks5_requests.jsonl"

    proc = None
    socks_port = 0
    with open(run_dir / "stderr", "w") as cerr:
        def startup(ports: Dict[str, int]) -> bool:
            nonlocal proc, socks_port
            log_file.write_text("", encoding="utf-8")
            socks_port = int(ports["socks"])
            args = [
                sys.executable,
                str(cmd),
                "--port",
                str(socks_port),
                "--log-file",
                str(log_file),
            ]
            log.info("start socks5 stub: %s", args)
            proc = subprocess.Popen(
                args=args,
                cwd=str(run_dir),
                stderr=cerr,
                stdout=cerr,
            )
            if _check_tcp_alive(socks_port, Env.SERVER_TIMEOUT):
                return True
            log.error("socks5 stub failed to start")
            proc.terminate()
            proc = None
            return False

        ok = alloc_ports_and_do({"socks": socket.SOCK_STREAM}, startup, env.gen_root, max_tries=3)
        if not ok or proc is None:
            pytest.skip("socks5 stub proxy did not start")
        try:
            yield {
                "port": socks_port,
                "log_file": str(log_file),
            }
        finally:
            if proc:
                proc.terminate()


@pytest.fixture(scope="function")
def lc_observe_http(env: Env) -> Generator[Dict[str, object], None, None]:
    """
    启动最小 HTTP/1.1 观测服务端（/cookie、/status/<code>）。
    - 每个测试函数单独启动，避免跨 case 的日志混淆
    """
    run_dir = Path(env.gen_dir) / f"lc_observe_http_{uuid.uuid4().hex[:8]}"
    cmd = _REPO_ROOT / "tests" / "libcurl_consistency" / "http_observe_server.py"
    run_dir.mkdir(parents=True, exist_ok=True)
    log_file = run_dir / "observe_http.jsonl"

    proc = None
    http_port = 0
    with open(run_dir / "stderr", "w") as cerr:
        def startup(ports: Dict[str, int]) -> bool:
            nonlocal proc, http_port
            http_port = int(ports["http"])
            log_file.write_text("", encoding="utf-8")
            args = [
                sys.executable,
                str(cmd),
                "--port",
                str(http_port),
                "--log-file",
                str(log_file),
            ]
            log.info("start observe http: %s", args)
            proc = subprocess.Popen(
                args=args,
                cwd=str(run_dir),
                stderr=cerr,
                stdout=cerr,
            )
            if _check_tcp_alive(http_port, Env.SERVER_TIMEOUT):
                return True
            log.error("observe http failed to start")
            proc.terminate()
            proc = None
            return False

        ok = alloc_ports_and_do({"http": socket.SOCK_STREAM}, startup, env.gen_root, max_tries=3)
        if not ok or proc is None:
            pytest.skip("observe http server did not start")
        try:
            yield {
                "port": http_port,
                "log_file": str(log_file),
            }
        finally:
            if proc:
                proc.terminate()


@pytest.fixture(scope="function")
def lc_observe_http_pair(env: Env) -> Generator[Dict[str, object], None, None]:
    """
    启动两套最小 HTTP/1.1 观测服务端（不同端口/不同 JSONL），用于“跨 host/port”可观测一致性场景。
    - 每个测试函数单独启动，避免跨 case 的日志混淆
    """
    run_dir = Path(env.gen_dir) / f"lc_observe_http_pair_{uuid.uuid4().hex[:8]}"
    cmd = _REPO_ROOT / "tests" / "libcurl_consistency" / "http_observe_server.py"
    run_dir.mkdir(parents=True, exist_ok=True)

    a_dir = run_dir / "a"
    b_dir = run_dir / "b"
    a_dir.mkdir(parents=True, exist_ok=True)
    b_dir.mkdir(parents=True, exist_ok=True)
    log_a = a_dir / "observe_http_a.jsonl"
    log_b = b_dir / "observe_http_b.jsonl"

    proc_a = None
    proc_b = None
    port_a = 0
    port_b = 0

    with open(a_dir / "stderr", "w") as cerr_a, open(b_dir / "stderr", "w") as cerr_b:
        def startup(ports: Dict[str, int]) -> bool:
            nonlocal proc_a, proc_b, port_a, port_b
            port_a = int(ports["a"])
            port_b = int(ports["b"])

            log_a.write_text("", encoding="utf-8")
            log_b.write_text("", encoding="utf-8")

            args_a = [
                sys.executable,
                str(cmd),
                "--port",
                str(port_a),
                "--log-file",
                str(log_a),
            ]
            args_b = [
                sys.executable,
                str(cmd),
                "--port",
                str(port_b),
                "--log-file",
                str(log_b),
            ]
            log.info("start observe http a: %s", args_a)
            log.info("start observe http b: %s", args_b)

            proc_a = subprocess.Popen(
                args=args_a,
                cwd=str(a_dir),
                stderr=cerr_a,
                stdout=cerr_a,
            )
            proc_b = subprocess.Popen(
                args=args_b,
                cwd=str(b_dir),
                stderr=cerr_b,
                stdout=cerr_b,
            )

            if _check_tcp_alive(port_a, Env.SERVER_TIMEOUT) and _check_tcp_alive(port_b, Env.SERVER_TIMEOUT):
                return True

            log.error("observe http pair failed to start")
            if proc_a:
                proc_a.terminate()
                proc_a = None
            if proc_b:
                proc_b.terminate()
                proc_b = None
            return False

        ok = alloc_ports_and_do({"a": socket.SOCK_STREAM, "b": socket.SOCK_STREAM}, startup, env.gen_root, max_tries=3)
        if not ok or proc_a is None or proc_b is None:
            pytest.skip("observe http pair server did not start")
        try:
            yield {
                "a": {"port": port_a, "log_file": str(log_a)},
                "b": {"port": port_b, "log_file": str(log_b)},
            }
        finally:
            if proc_a:
                proc_a.terminate()
            if proc_b:
                proc_b.terminate()


@pytest.fixture(scope="function")
def lc_observe_https(env: Env) -> Generator[Dict[str, object], None, None]:
    """
    启动最小 HTTPS 观测服务端（自签 CA + localhost 证书），用于 TLS 校验一致性。
    - 证书与 CA 复用 curl testenv 生成物（避免引入 openssl 生成过程）
    """
    ca_dir = _REPO_ROOT / "curl" / "tests" / "http" / "gen" / "ca"
    ca_cert = ca_dir / "ca.pem"
    cert = ca_dir / "one.http.curl.se.rsa2048.cert.pem"
    key = ca_dir / "one.http.curl.se.rsa2048.pkey.pem"
    if not (ca_cert.exists() and cert.exists() and key.exists()):
        pytest.skip("TLS 观测服务端证书/CA 不存在（需要先跑一次 curl testenv 生成 ca/）")

    run_dir = Path(env.gen_dir) / f"lc_observe_https_{uuid.uuid4().hex[:8]}"
    cmd = _REPO_ROOT / "tests" / "libcurl_consistency" / "http_observe_server.py"
    run_dir.mkdir(parents=True, exist_ok=True)
    log_file = run_dir / "observe_https.jsonl"

    proc = None
    https_port = 0
    with open(run_dir / "stderr", "w") as cerr:
        def startup(ports: Dict[str, int]) -> bool:
            nonlocal proc, https_port
            https_port = int(ports["https"])
            log_file.write_text("", encoding="utf-8")
            args = [
                sys.executable,
                str(cmd),
                "--port",
                str(https_port),
                "--log-file",
                str(log_file),
                "--tls-cert",
                str(cert),
                "--tls-key",
                str(key),
            ]
            log.info("start observe https: %s", args)
            proc = subprocess.Popen(
                args=args,
                cwd=str(run_dir),
                stderr=cerr,
                stdout=cerr,
            )
            if _check_tcp_alive(https_port, Env.SERVER_TIMEOUT):
                return True
            log.error("observe https failed to start")
            proc.terminate()
            proc = None
            return False

        ok = alloc_ports_and_do({"https": socket.SOCK_STREAM}, startup, env.gen_root, max_tries=3)
        if not ok or proc is None:
            pytest.skip("observe https server did not start")
        try:
            yield {
                "port": https_port,
                "log_file": str(log_file),
                "ca_cert": str(ca_cert),
                "cert": str(cert),
                "key": str(key),
            }
        finally:
            if proc:
                proc.terminate()

@pytest.fixture(scope="session")
def lc_logs(httpd, nghttpx):
    """
    提供服务端日志路径，便于失败时收集：
    - httpd：logs_dir/error_log
    - nghttpx：nghttpx.log/stderr
    """
    logs = {}
    if httpd:
        logs_dir = Path(getattr(httpd, "_logs_dir"))  # type: ignore[attr-defined]
        logs["httpd_logs_dir"] = logs_dir
        logs["httpd_error_log"] = Path(getattr(httpd, "_error_log"))  # type: ignore[attr-defined]
        logs["httpd_access_log"] = logs_dir / "access_log"
    if nghttpx:
        run_dir = Path(getattr(nghttpx, "_run_dir"))  # type: ignore[attr-defined]
        logs["nghttpx_log"] = Path(getattr(nghttpx, "_error_log"))  # type: ignore[attr-defined]
        logs["nghttpx_stderr"] = Path(getattr(nghttpx, "_stderr"))  # type: ignore[attr-defined]
        logs["nghttpx_access_log"] = run_dir / "access_log"
    return logs


@pytest.fixture(scope="session")
def lc_ws_logs(lc_ws_echo):
    """
    提供 WebSocket echo server 的日志路径（仅在用例显式依赖 ws 时启动服务端）。
    """
    logs = {}
    if lc_ws_echo and "log_file" in lc_ws_echo:
        logs["ws_handshake_log"] = Path(str(lc_ws_echo["log_file"]))
    if lc_ws_echo and "events_log_file" in lc_ws_echo:
        logs["ws_events_log"] = Path(str(lc_ws_echo["events_log_file"]))
    return logs


def collect_service_logs(logs: Dict[str, Path], dest: Path) -> Dict[str, str]:
    """
    将 httpd/nghttpx 等日志复制到目标目录，返回相对路径映射。
    仅在调用方需要调试时使用，默认不自动复制以避免噪声。
    """
    from tests.libcurl_consistency.pytest_support.service_logs import collect_service_logs as _collect
    return _collect(logs, dest)


# 注入上游 curl http testenv 的 fixtures/hook（保留目录级作用域）
_inject_upstream_curl_http_fixtures()
