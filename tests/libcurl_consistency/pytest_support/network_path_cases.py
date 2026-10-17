"""RESOLVE/CONNECT_TO 冷请求配对配置；暖域与生命周期由 Qt 窄用例覆盖。"""

NETWORK_PATH_CASES = {
    "resolve": {
        "url": "http://origin.invalid:{a}/status/200",
        "resolve": ["origin.invalid:{a}:127.0.0.1"],
        "target": "a",
    },
    "connect": {
        "url": "http://origin.invalid:{a}/status/200",
        "connect_to": ["origin.invalid:{a}:127.0.0.1:{b}"],
        "target": "b",
    },
    "neither": {
        "url": "http://127.0.0.1:{a}/status/200",
        "target": "a",
    },
    "both": {
        "url": "http://origin.invalid:{a}/status/200",
        "resolve": ["node.invalid:{b}:127.0.0.1"],
        "connect_to": ["origin.invalid:{a}:node.invalid:{b}"],
        "target": "b",
    },
    "both_connect_first": {
        "url": "http://origin.invalid:{a}/status/200",
        "resolve": ["node.invalid:{b}:127.0.0.1"],
        "connect_to": ["origin.invalid:{a}:node.invalid:{b}"],
        "connect_first": True,
        "target": "b",
    },
    "resolve_last_wins": {
        "url": "http://origin.invalid:{a}/status/200",
        "resolve": ["origin.invalid:{a}:127.0.0.2", "origin.invalid:{a}:127.0.0.1"],
        "target": "a",
    },
    "connect_first_match": {
        "url": "http://origin.invalid:{a}/status/200",
        "connect_to": [
            "origin.invalid:{a}:127.0.0.1:{b}",
            "origin.invalid:{a}:127.0.0.1:{a}",
        ],
        "target": "b",
    },
    "connect_source_empty": {
        "url": "http://origin.invalid:{a}/status/200",
        "resolve": ["node.invalid:{b}:127.0.0.1"],
        "connect_to": ["::node.invalid:{b}"],
        "target": "b",
    },
    "connect_target_host_empty": {
        "url": "http://origin.invalid:{a}/status/200",
        "resolve": ["origin.invalid:{b}:127.0.0.1"],
        "connect_to": ["origin.invalid:{a}::{b}"],
        "target": "b",
    },
    "connect_target_port_empty": {
        "url": "http://origin.invalid:{a}/status/200",
        "resolve": ["node.invalid:{a}:127.0.0.1"],
        "connect_to": ["origin.invalid:{a}:node.invalid:"],
        "target": "a",
    },
    "connect_noop": {
        "url": "http://origin.invalid:{a}/status/200",
        "resolve": ["origin.invalid:{a}:127.0.0.1"],
        "connect_to": ["origin.invalid:{a}:origin.invalid:{a}"],
        "target": "a",
    },
    "connect_unmatched": {
        "url": "http://origin.invalid:{a}/status/200",
        "resolve": ["origin.invalid:{a}:127.0.0.1"],
        "connect_to": ["other.invalid:{a}:127.0.0.1:{b}"],
        "target": "a",
    },
    "resolve_origin_mismatch": {
        "url": "http://origin.invalid:{a}/status/200",
        "resolve": ["origin.invalid:{a}:127.0.0.1"],
        "connect_to": ["origin.invalid:{a}:node.invalid:{b}"],
        "curlcode": 6,
    },
    "resolve_port_mismatch": {
        "url": "http://origin.invalid:{a}/status/200",
        "resolve": ["node.invalid:{a}:127.0.0.1"],
        "connect_to": ["origin.invalid:{a}:node.invalid:{b}"],
        "curlcode": 6,
    },
}
