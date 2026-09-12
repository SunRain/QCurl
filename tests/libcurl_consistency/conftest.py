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

# 将上游 http 测试目录放入 sys.path，直接复用 testenv 组件与 fixtures。
_REPO_ROOT = Path(__file__).resolve().parents[2]
# 本 conftest 的管辖边界：目录级 hook 收到的是整个 session 的 items，需据此过滤。
_LC_TEST_DIR = Path(__file__).resolve().parent
# 纯单元测试不依赖上游 curl testenv；其余 test_*.py 才需要一致性环境。
_UNIT_TEST_MODULES = frozenset(
    {
        "test_compare_unit.py",
        "test_run_gate_unit.py",
    }
)
_REQUIRED_ENVIRONMENT = (
    "CURL_BUILD_DIR",
    "CURL",
    "CURLINFO",
    "QCURL_QTTEST",
    "QCURL_BUILD_DIR",
    "QCURL_LC_CAPABILITY_MANIFEST",
)


def _consistency_environment_error() -> str | None:
    """校验 gate 注入的固定路径，拒绝默认目录和隐式二进制发现。"""

    for name in _REQUIRED_ENVIRONMENT:
        if not os.environ.get(name, "").strip():
            return f"缺少必需环境变量 `{name}`；请通过一致性 gate 注入显式路径"

    curl_build_dir = Path(os.environ["CURL_BUILD_DIR"]).expanduser().resolve()
    if not (curl_build_dir / "src").is_dir():
        return f"`CURL_BUILD_DIR` 必须包含 src/ 目录: {curl_build_dir}"

    qcurl_build_dir = Path(os.environ["QCURL_BUILD_DIR"]).expanduser().resolve()
    if not qcurl_build_dir.is_dir():
        return f"`QCURL_BUILD_DIR` 指向的目录不存在: {qcurl_build_dir}"

    for name in ("CURL", "CURLINFO", "QCURL_QTTEST"):
        path = Path(os.environ[name]).expanduser().resolve()
        if not path.is_file():
            return f"环境变量 `{name}` 指向的文件不存在: {path}"
    return None


_ENVIRONMENT_ERROR = _consistency_environment_error()


def _is_consistency_integration_file(path: Path) -> bool:
    """判断路径是否为本目录内需要 testenv 的测试模块。"""

    return (
        path.suffix == ".py"
        and (path.name.startswith("test_") or path.name.endswith("_test.py"))
        and _LC_TEST_DIR in path.parents
        and path.name not in _UNIT_TEST_MODULES
    )


def _collection_requests_consistency(config) -> bool:
    """判断 pytest 命令行是否明确包含一致性目录或其祖先。"""

    raw_args = [value for value in getattr(config, "args", ()) if not str(value).startswith("-")]
    if not raw_args:
        # 无路径参数时 pytest 默认从仓库根目录收集，包含一致性目录。
        return True

    for raw_arg in raw_args:
        value = str(raw_arg).split("::", 1)[0]
        if not value:
            continue
        candidate = Path(value)
        if not candidate.is_absolute():
            candidate = Path.cwd() / candidate
        candidate = candidate.resolve()
        if candidate.suffix == ".py":
            if _is_consistency_integration_file(candidate):
                return True
            continue
        if candidate == _LC_TEST_DIR or _LC_TEST_DIR in candidate.parents:
            return True
        if candidate in _LC_TEST_DIR.parents:
            return True
    return False


def _missing_env_integration_files(config) -> set[Path]:
    """返回当前 pytest 配置的缺失环境收集记录。"""

    files = getattr(config, "_qcurl_missing_env_integration_files", None)
    if files is None:
        files = set()
        setattr(config, "_qcurl_missing_env_integration_files", files)
    return files


def _gate_injected_path(name: str, placeholder: str) -> Path:
    """读取由一致性 gate 显式注入的路径；缺失时返回仓库内占位路径。

    占位路径的唯一用途是让不依赖 curl testenv 的纯单元测试仍能 import 本模块。
    真正使用这些路径的集成测试由 `pytest_collection_modifyitems` 依据
    `_ENVIRONMENT_ERROR` 拦截，因此占位路径不会进入任何断言。
    占位路径固定挂在 `_REPO_ROOT` 之下，保证后续路径运算不会逃逸出仓库。
    """

    value = os.environ.get(name, "").strip()
    if value:
        return Path(value).expanduser().resolve()
    return _REPO_ROOT / placeholder


_CURL_BUILD_DIR = _gate_injected_path("CURL_BUILD_DIR", "__missing_curl_build__")
_CURLINFO_BIN = _gate_injected_path("CURLINFO", "__missing_curlinfo__")
_QCURL_QTTEST_BIN = _gate_injected_path("QCURL_QTTEST", "__missing_qttest__")
# 由 gate_runtime.gate_environment() 显式注入，不再从二进制路径反推构建树位置。
_QCURL_BUILD_DIR = _gate_injected_path("QCURL_BUILD_DIR", "__missing_qcurl_build__")
_NGHTTPX_H3_BIN = _QCURL_BUILD_DIR / "libcurl_consistency" / "nghttpx-h3" / "bin" / "nghttpx"

CURL_HTTP_DIR = _REPO_ROOT / "curl" / "tests" / "http"
TESTENV_IMPORT_ERROR = None
if _ENVIRONMENT_ERROR:
    TESTENV_IMPORT_ERROR = RuntimeError(_ENVIRONMENT_ERROR)
else:
    # curl testenv 在 import 阶段实例化 Env.CONFIG，因此必须先切换到显式 build/src。
    os.chdir(str(_CURL_BUILD_DIR / "src"))
    if str(CURL_HTTP_DIR) not in sys.path:
        sys.path.insert(0, str(CURL_HTTP_DIR))
    try:
        from testenv import Env, CurlClient  # noqa: E402
        from testenv.ports import alloc_ports_and_do  # noqa: E402
        from testenv.env import EnvConfig  # noqa: E402
        from testenv import Httpd, NghttpxQuic  # noqa: E402
        import testenv.env as testenv_env  # noqa: E402
        import testenv.nghttpx as testenv_nghttpx  # noqa: E402
    except ImportError as exc:  # pragma: no cover - environment-specific import guard
        TESTENV_IMPORT_ERROR = RuntimeError(f"无法导入 curl testenv: {exc}")
    except RuntimeError as exc:  # EnvConfig 在 import 阶段报告的明确配置/二进制错误
        TESTENV_IMPORT_ERROR = exc
    finally:
        # 后续测试使用仓库根目录解析 run-scoped 路径。
        os.chdir(str(_REPO_ROOT))

log = logging.getLogger(__name__)


def pytest_configure(config):
    """注册 testenv marker，用于区分需要 curl testenv 的集成测试与纯单元测试。"""
    config.addinivalue_line(
        "markers",
        "needs_testenv: 测试需要 curl testenv 环境（httpd/nghttpx 服务器）",
    )


def pytest_ignore_collect(collection_path: Path, config) -> bool | None:
    """环境缺失时在导入测试模块前阻止 testenv 依赖的文件被加载。

    pytest 的目录级 ``pytest_collection_modifyitems`` 发生在模块导入之后；若只在
    那里退出，集成测试会先因 ``from testenv`` 产生大量导入错误。这里提前忽略这些
    模块，并由 ``pytest_collection_finish`` 统一给出单一、可读的环境错误。
    """

    if not _collection_requests_consistency(config):
        return None
    path = collection_path.resolve()
    if _ENVIRONMENT_ERROR and _is_consistency_integration_file(path):
        _missing_env_integration_files(config).add(path)
        return True
    return None


def pytest_collection_finish(session) -> None:
    """在收集完成后以单一错误报告缺失的一致性环境。"""

    files = getattr(session.config, "_qcurl_missing_env_integration_files", set())
    if files and _ENVIRONMENT_ERROR:
        pytest.exit(
            f"libcurl consistency 环境无效: {_ENVIRONMENT_ERROR}",
            returncode=2,
        )


def pytest_collection_modifyitems(config, items):
    """为本目录中需要 testenv 的测试项添加 marker 和 fixture 依赖，并检查环境。

    pytest 会把**整个 session** 的 items 传给目录级 conftest 的该 hook，因此必须先
    限定到本目录：否则仓库其它目录的测试会被误判为需要 curl testenv，一旦环境缺失就被
    `pytest.exit` 连带终止（混合运行 `pytest tests/` 时整个 session 失败）。

    本目录内的纯单元测试（test_compare_unit.py、test_run_gate_unit.py）同样不标记，
    因此不受环境检查与 autouse fixture 影响。
    """
    del config

    needs_testenv_items = []
    for item in items:
        item_path = Path(item.fspath).resolve()
        # 管辖边界：只处理本目录（含子目录）的测试项
        if _LC_TEST_DIR not in item_path.parents:
            continue
        if item_path.name in _UNIT_TEST_MODULES:
            continue
        item.add_marker(pytest.mark.needs_testenv)
        # 为集成测试显式添加 autouse fixture 依赖
        item.add_marker(pytest.mark.usefixtures("lc_seed_http_docs"))
        needs_testenv_items.append(item)

    # 仅当存在需要 testenv 的测试时才检查环境
    if needs_testenv_items:
        if _ENVIRONMENT_ERROR:
            pytest.exit(
                f"libcurl consistency 环境无效: {_ENVIRONMENT_ERROR}",
                returncode=2,
            )
        if TESTENV_IMPORT_ERROR:
            pytest.exit(
                f"libcurl consistency testenv 初始化失败: {TESTENV_IMPORT_ERROR}",
                returncode=2,
            )


@pytest.fixture(autouse=True)
def _record_gate_nodeid(request):
    """把当前 pytest nodeid 写入 gate JUnit，支持精确执行集合核对。"""
    if os.environ.get("QCURL_LC_RUN_ID", "").strip():
        request.node.user_properties.append(("nodeid", request.node.nodeid))

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

def _curl_supports_http3() -> bool:
    """Return whether the bundled curl build can actually execute HTTP/3 cases."""
    return bool(Env.have_h3_curl())


def _disable_testenv_nghttpx() -> None:
    """Hide nghttpx from upstream curl fixtures when HTTP/3 cannot be exercised."""
    Env.CONFIG.nghttpx = None
    Env.CONFIG._nghttpx_version = None
    Env.CONFIG.nghttpx_with_h3 = False

def _patch_testenv_curlinfo_path() -> None:
    """
    curl testenv 的 EnvConfig 不支持通过环境变量覆盖 curlinfo 路径，只能改模块常量。
    这里将其指向 gate 显式注入的 CURLINFO 路径，避免 import 时 cwd 影响。
    """
    if _CURLINFO_BIN.exists():
        testenv_env.CURLINFO = str(_CURLINFO_BIN)

def _override_testenv_nghttpx_bin() -> None:
    """
    curl testenv 默认会优先读取 <qcurl_build>/curl/tests/http/config.ini（其中 nghttpx 通常指向 /usr/bin/nghttpx）。
    为了覆盖 HTTP/3 场景，这里在不改 curl/tests/... 的前提下，将 Env.CONFIG.nghttpx 指向本仓库构建的 h3-capable nghttpx。
    """
    if not _curl_supports_http3():
        _disable_testenv_nghttpx()
        return
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
    注意：CustomLog 必须落在实际处理请求的 VirtualHost 上，放在 base server 只会记启动探针。
    """
    if getattr(Httpd, "_qcurl_lc_access_log_patched", False):
        return
    Httpd._qcurl_lc_access_log_patched = True  # type: ignore[attr-defined]

    original_reset_config = Httpd.reset_config
    original_clear_logs = Httpd.clear_logs
    original_curltest_conf = Httpd._curltest_conf

    def access_log_path(self: Httpd) -> str:
        return os.path.join(getattr(self, "_logs_dir"), "access_log")

    def curltest_conf_with_access_log(self: Httpd, servername) -> list[str]:  # type: ignore[no-untyped-def]
        lines = original_curltest_conf(self, servername)
        return [
            f'    CustomLog "{access_log_path(self)}" qcurl_lc_access',
            *lines,
        ]

    def reset_config_with_access_log(self: Httpd):  # type: ignore[no-untyped-def]
        original_reset_config(self)
        self.set_extra_config(
            "base",
            [
                'LogFormat "%{%Y-%m-%dT%H:%M:%S%z}t|%H|%m|%U%q|%>s|%{Range}i|%{Content-Length}i" '
                + "qcurl_lc_access",
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
    Httpd._curltest_conf = curltest_conf_with_access_log  # type: ignore[assignment]


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


if TESTENV_IMPORT_ERROR is None:
    _patch_testenv_curlinfo_path()
    _override_testenv_nghttpx_bin()
    _patch_httpd_access_log()
    _patch_nghttpx_access_log()


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
    if not _curl_supports_http3():
        cfg.nghttpx = None
        cfg._nghttpx_version = None  # type: ignore[attr-defined]
        cfg.nghttpx_with_h3 = False
        return cfg
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


@pytest.fixture(scope="session")
def lc_seed_http_docs(env, httpd):
    """
    为一致性用例准备最小的静态资源文件（对齐上游 test_02_download.py 的 class-scope fixture）。

    注意：不再是 autouse，通过 pytest_collection_modifyitems 为需要 testenv 的测试显式添加。
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
def lc_https_proxy(env: Env) -> Generator[Dict[str, object], None, None]:
    """启动启用 TLS 和 Basic 认证的本地 HTTPS proxy。"""

    ca_dir = _REPO_ROOT / "curl" / "tests" / "http" / "gen" / "ca"
    ca_cert = ca_dir / "ca.pem"
    cert = ca_dir / "proxy.http.curl.se.rsa2048.cert.pem"
    key = ca_dir / "proxy.http.curl.se.rsa2048.pkey.pem"
    if not (ca_cert.exists() and cert.exists() and key.exists()):
        pytest.skip("HTTPS proxy 证书/CA 不存在")

    run_dir = Path(env.gen_dir) / f"lc_https_proxy_{uuid.uuid4().hex[:8]}"
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
            proxy_port = int(ports["proxy"])
            log_file.write_text("", encoding="utf-8")
            args = [
                sys.executable,
                str(cmd),
                "--port",
                str(proxy_port),
                "--log-file",
                str(log_file),
                "--username",
                username,
                "--password",
                password,
                "--tls-cert",
                str(cert),
                "--tls-key",
                str(key),
                "--tls-min",
                "tls1.2",
                "--tls-max",
                "tls1.2",
            ]
            proc = subprocess.Popen(args=args, cwd=str(run_dir), stderr=cerr, stdout=cerr)
            if _check_tcp_alive(proxy_port, Env.SERVER_TIMEOUT):
                return True
            proc.terminate()
            proc = None
            return False

        ok = alloc_ports_and_do({"proxy": socket.SOCK_STREAM}, startup, env.gen_root, max_tries=3)
        if not ok or proc is None:
            pytest.skip("HTTPS proxy 未能启动")
        try:
            yield {
                "port": proxy_port,
                "log_file": str(log_file),
                "username": username,
                "password": password,
                "ca_cert": str(ca_cert),
                "cert": str(cert),
            }
        finally:
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
def lc_socks5_success_proxy(env: Env) -> Generator[Dict[str, object], None, None]:
    """
    启动可转发的 SOCKS5 proxy。
    默认 failure stub 保持不变，成功路径必须显式使用本 fixture。
    """
    run_dir = Path(env.gen_dir) / f"lc_socks5_success_proxy_{uuid.uuid4().hex[:8]}"
    cmd = _REPO_ROOT / "tests" / "libcurl_consistency" / "socks5_proxy_server.py"
    run_dir.mkdir(parents=True, exist_ok=True)
    log_file = run_dir / "socks5_success_requests.jsonl"

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
                "--mode",
                "success",
            ]
            log.info("start socks5 success proxy: %s", args)
            proc = subprocess.Popen(
                args=args,
                cwd=str(run_dir),
                stderr=cerr,
                stdout=cerr,
            )
            if _check_tcp_alive(socks_port, Env.SERVER_TIMEOUT):
                return True
            log.error("socks5 success proxy failed to start")
            proc.terminate()
            proc = None
            return False

        ok = alloc_ports_and_do({"socks": socket.SOCK_STREAM}, startup, env.gen_root, max_tries=3)
        if not ok or proc is None:
            pytest.skip("socks5 success proxy did not start")
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
def lc_observe_http_ipv6(env: Env) -> Generator[Dict[str, object], None, None]:
    """在 IPv6 loopback 上启动 HTTP 观测服务；不支持时由 capability planner 排除。"""

    run_dir = Path(env.gen_dir) / f"lc_observe_http_ipv6_{uuid.uuid4().hex[:8]}"
    cmd = _REPO_ROOT / "tests" / "libcurl_consistency" / "http_observe_server.py"
    run_dir.mkdir(parents=True, exist_ok=True)
    log_file = run_dir / "observe_http_ipv6.jsonl"

    picker = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    picker.bind(("::1", 0))
    port = int(picker.getsockname()[1])
    picker.close()

    with open(run_dir / "stderr", "w") as cerr:
        args = [
            sys.executable,
            str(cmd),
            "--port",
            str(port),
            "--log-file",
            str(log_file),
            "--bind-host",
            "::1",
        ]
        proc = subprocess.Popen(args=args, cwd=str(run_dir), stderr=cerr, stdout=cerr)
        deadline = time.time() + Env.SERVER_TIMEOUT
        alive = False
        while time.time() < deadline:
            try:
                with socket.create_connection(("::1", port), timeout=0.2):
                    alive = True
                    break
            except OSError:
                time.sleep(0.05)
        if not alive:
            proc.terminate()
            pytest.fail("capability planner 启用了 IPv6 case，但本机 ::1 服务无法启动")
        try:
            yield {"port": port, "log_file": str(log_file)}
        finally:
            proc.terminate()


def _observe_https_server(
    env: Env,
    *,
    client_auth: bool = False,
    tls_min: str = "",
    tls_max: str = "",
) -> Generator[Dict[str, object], None, None]:
    """启动带指定 TLS 合同的本地观测服务，并在退出时释放进程。"""
    ca_dir = _REPO_ROOT / "curl" / "tests" / "http" / "gen" / "ca"
    ca_cert = ca_dir / "ca.pem"
    cert = ca_dir / "one.http.curl.se.rsa2048.cert.pem"
    key = ca_dir / "one.http.curl.se.rsa2048.pkey.pem"
    if not (ca_cert.exists() and cert.exists() and key.exists()):
        pytest.skip("TLS 观测服务端证书/CA 不存在（需要先跑一次 curl testenv 生成 ca/）")

    client_ca = None
    client_cert_source = None
    client_intermediate = None
    client_key = None
    client_key_encrypted = None
    client_key_password = uuid.uuid4().hex
    if client_auth:
        client_ca = ca_cert
        client_cert_source = ca_dir / "clientsX" / "user1.rsa2048.cert.pem"
        client_intermediate = ca_dir / "clientsX.rsa2048.cert.pem"
        client_key = ca_dir / "clientsX" / "user1.rsa2048.pkey.pem"
        if not all(
            path.exists()
            for path in (client_ca, client_cert_source, client_intermediate, client_key)
        ):
            pytest.skip("mTLS 客户端证书未生成（需要先跑一次 curl testenv 生成 clientsX）")

    run_dir = Path(env.gen_dir) / f"lc_observe_https_{uuid.uuid4().hex[:8]}"
    cmd = _REPO_ROOT / "tests" / "libcurl_consistency" / "http_observe_server.py"
    run_dir.mkdir(parents=True, exist_ok=True)
    log_file = run_dir / "observe_https.jsonl"
    client_cert = None
    if client_cert_source is not None and client_intermediate is not None:
        client_cert = run_dir / "client-chain.pem"
        client_cert.write_bytes(client_cert_source.read_bytes() + client_intermediate.read_bytes())
    if client_key is not None:
        client_key_encrypted = run_dir / "client-encrypted-key.pem"
        try:
            subprocess.run(
                [
                    "openssl",
                    "rsa",
                    "-in",
                    str(client_key),
                    "-aes256",
                    "-passout",
                    f"pass:{client_key_password}",
                    "-out",
                    str(client_key_encrypted),
                ],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
        except (OSError, subprocess.CalledProcessError) as exc:
            pytest.fail(f"生成加密 mTLS 客户端私钥失败: {exc}")

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
            if client_ca is not None:
                args.extend(["--tls-client-ca", str(client_ca), "--tls-require-client-cert"])
            if tls_min:
                args.extend(["--tls-min", tls_min])
            if tls_max:
                args.extend(["--tls-max", tls_max])
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
                "client_ca": str(client_ca) if client_ca is not None else "",
                "client_cert": str(client_cert) if client_cert is not None else "",
                "client_key": str(client_key) if client_key is not None else "",
                "client_key_encrypted": str(client_key_encrypted) if client_key_encrypted is not None else "",
                "client_key_password": client_key_password if client_key_encrypted is not None else "",
                "tls_min": tls_min,
                "tls_max": tls_max,
            }
        finally:
            if proc:
                proc.terminate()


@pytest.fixture(scope="function")
def lc_observe_https(env: Env) -> Generator[Dict[str, object], None, None]:
    """启动默认 HTTPS 观测服务端。"""

    yield from _observe_https_server(env)


@pytest.fixture(scope="function")
def lc_observe_https_mtls(env: Env) -> Generator[Dict[str, object], None, None]:
    """启动要求客户端证书的 HTTPS 观测服务端。"""

    yield from _observe_https_server(env, client_auth=True)


@pytest.fixture(scope="function")
def lc_observe_https_tls12(env: Env) -> Generator[Dict[str, object], None, None]:
    """启动仅允许 TLS 1.2 的 HTTPS 观测服务端。"""

    yield from _observe_https_server(env, tls_min="tls1.2", tls_max="tls1.2")


@pytest.fixture(scope="function")
def lc_observe_https_tls13(env: Env) -> Generator[Dict[str, object], None, None]:
    """启动最低 TLS 1.3 的 HTTPS 观测服务端。"""

    yield from _observe_https_server(env, tls_min="tls1.3")

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


# 注入上游 curl http testenv 的 fixtures/hook（保留目录级作用域）。
if TESTENV_IMPORT_ERROR is None:
    _inject_upstream_curl_http_fixtures()
