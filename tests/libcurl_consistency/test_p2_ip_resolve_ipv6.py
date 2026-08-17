"""P2：`setIpResolve(Ipv6)` 成对证据。"""

from tests.libcurl_consistency.pytest_support.ip_resolve_case import run_ip_resolve_case


def test_p2_ip_resolve_ipv6(env, lc_observe_http_ipv6):
    run_ip_resolve_case(env, lc_observe_http_ipv6, "v6")
