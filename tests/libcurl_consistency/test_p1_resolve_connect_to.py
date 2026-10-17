"""P1：双模式 RESOLVE/CONNECT_TO 的冷请求选项语义与服务端观察。"""

from __future__ import annotations

import pytest

from tests.libcurl_consistency.pytest_support.case_defs import P1_NETWORK_PATH_CASES
from tests.libcurl_consistency.pytest_support.network_path_case import (
    run_network_path_case,
)

_ISOLATIONS = ["isolated", "native_shared"]
_COMBINATIONS = [
    name for name in P1_NETWORK_PATH_CASES if name not in {"resolve", "connect"}
]


@pytest.fixture(autouse=True)
def _disable_environment_proxy(monkeypatch):
    for name in (
        "http_proxy",
        "https_proxy",
        "all_proxy",
        "HTTP_PROXY",
        "HTTPS_PROXY",
        "ALL_PROXY",
    ):
        monkeypatch.delenv(name, raising=False)
    monkeypatch.setenv("no_proxy", "*")
    monkeypatch.setenv("NO_PROXY", "*")


@pytest.mark.parametrize("isolation", _ISOLATIONS)
def test_p1_resolve_override_http_1_1(env, lc_logs, lc_observe_http_pair, isolation):
    run_network_path_case(env, lc_logs, lc_observe_http_pair, "resolve", isolation)


@pytest.mark.parametrize("isolation", _ISOLATIONS)
def test_p1_connect_to_http_1_1(env, lc_logs, lc_observe_http_pair, isolation):
    run_network_path_case(env, lc_logs, lc_observe_http_pair, "connect", isolation)


@pytest.mark.parametrize(
    "scenario,isolation",
    [(scenario, isolation) for scenario in _COMBINATIONS for isolation in _ISOLATIONS],
    ids=[
        f"{scenario}-{isolation}"
        for scenario in _COMBINATIONS
        for isolation in _ISOLATIONS
    ],
)
def test_p1_resolve_connect_to_combinations_http_1_1(
    env, lc_logs, lc_observe_http_pair, scenario, isolation
):
    run_network_path_case(env, lc_logs, lc_observe_http_pair, scenario, isolation)
