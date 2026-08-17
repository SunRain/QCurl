"""P2：`setIpResolve(Ipv4)` 成对证据。"""

from tests.libcurl_consistency.pytest_support.ip_resolve_case import run_ip_resolve_case


def test_p2_ip_resolve_ipv4(env, lc_observe_http):
    run_ip_resolve_case(env, lc_observe_http, "v4")
