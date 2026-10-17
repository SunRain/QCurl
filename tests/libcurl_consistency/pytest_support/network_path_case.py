"""复用现有 HTTP baseline 与 Qt 执行器完成网络路径配对观察。"""

from __future__ import annotations

import re
import uuid
from pathlib import Path
from urllib.parse import urlsplit

from .artifacts import apply_error_namespaces, build_request_semantic, write_json
from .baseline import run_libtest_case
from .case_defs import P1_NETWORK_PATH_CASES
from .compare import assert_artifacts_match
from .observed import observe_http_observed_list_for_id, parse_observe_http_log
from .qcurl_runner import require_qcurl_qttest, run_qt_test
from .service_logs import collect_service_logs_for_case, should_collect_service_logs

_SUITE = "p1_network_path"
_PROTO = "http/1.1"
_CURLINFO_RE = re.compile(r"curlcode=(\d+)\s+http_code=(\d+)")


def _format_case(scenario: str, servers: dict) -> dict:
    spec = P1_NETWORK_PATH_CASES[scenario]
    ports = {name: int(server["port"]) for name, server in servers.items()}
    return {
        **spec,
        "url": spec["url"].format(**ports),
        "resolve": [entry.format(**ports) for entry in spec.get("resolve", [])],
        "connect_to": [entry.format(**ports) for entry in spec.get("connect_to", [])],
    }


def _baseline_args(spec: dict, url: str) -> list[str]:
    args = ["-V", _PROTO]
    for option, entries in (
        ("--resolve", spec["resolve"]),
        ("--connect-to", spec["connect_to"]),
    ):
        for entry in entries:
            args.extend([option, entry])
    return [*args, url]


def _run_side(
    env, spec: dict, case: str, isolation: str, req_id: str, side: str
) -> dict:
    url = f"{spec['url']}?id={req_id}"
    curlcode = spec.get("curlcode", 0)
    common = {
        "env": env,
        "suite": _SUITE,
        "case": case,
        "request_meta": {
            "method": "GET",
            "url": spec["url"],
            "headers": {},
            "body": b"",
        },
        "response_meta": {
            "status": 0 if curlcode else 200,
            "http_version": _PROTO,
            "headers": {},
            "body": None,
        },
        "download_count": 1,
    }
    if side == "baseline":
        return run_libtest_case(
            **common,
            client_name="cli_lc_http",
            args=_baseline_args(spec, url),
            allowed_exit_codes={7} if curlcode else {0},
        )
    return run_qt_test(
        **common,
        qt_executable=require_qcurl_qttest(),
        case_env={
            "QCURL_LC_CASE_ID": "p1_network_path",
            "QCURL_LC_TARGET_URL": url,
            "QCURL_LC_RESOLVE_ISOLATION": isolation,
            "QCURL_LC_RESOLVE_ENTRIES": "\n".join(spec["resolve"]),
            "QCURL_LC_CONNECT_TO_ENTRIES": "\n".join(spec["connect_to"]),
            "QCURL_LC_CONNECT_FIRST": "1" if spec.get("connect_first") else "0",
            "QCURL_LC_EXPECT_CURL_CODE": str(curlcode),
        },
    )


def _assert_curl_result(payload: dict, spec: dict) -> tuple[int, int]:
    lines = [*payload.get("stdout", []), *payload.get("stderr", [])]
    matches = [match for line in lines if (match := _CURLINFO_RE.search(line))]
    assert len(matches) == 1, f"缺少或重复传输结果：{lines}"
    result = (int(matches[0][1]), int(matches[0][2]))
    curlcode = spec.get("curlcode", 0)
    assert result == (curlcode, 0 if curlcode else 200), result
    return result


def _record_observation(artifact: dict, spec: dict, servers: dict, req_id: str) -> None:
    payload = artifact["payload"]
    curlcode, http_code = _assert_curl_result(payload, spec)
    target = spec.get("target")
    logs = {name: Path(str(server["log_file"])) for name, server in servers.items()}
    if target is not None:
        observed = observe_http_observed_list_for_id(
            logs[target], req_id, expected_count=1
        )[0]
        assert observed.method == "GET" and observed.url == "/status/200", observed
        assert (
            observed.headers.get("host") == urlsplit(spec["url"]).netloc
        ), observed.headers
        assert observed.status == http_code
        payload["request"] = build_request_semantic(
            observed.method, observed.url, observed.headers, b""
        )
        payload["response"]["status"] = observed.status
        payload["response"]["headers"] = observed.response_headers
    else:
        assert payload["response"]["body_len"] == 0
        apply_error_namespaces(
            payload, kind="dns", http_status=0, curlcode=curlcode, http_code=http_code
        )
    for name, log in logs.items():
        if name != target:
            assert not [
                row for row in parse_observe_http_log(log) if row.get("id") == req_id
            ], f"请求不应命中服务端 {name}: {req_id}"
    write_json(artifact["path"], payload)


def _run_pair(env, spec: dict, servers: dict, isolation: str, case: str) -> None:
    trace = f"lc_{uuid.uuid4().hex[:8]}_network_path"
    artifacts = []
    for side in ("baseline", "qcurl"):
        req_id = f"{trace}__{side}"
        artifact = _run_side(env, spec, case, isolation, req_id, side)
        _record_observation(artifact, spec, servers, req_id)
        artifacts.append(artifact)
    assert_artifacts_match(artifacts[0]["path"], artifacts[1]["path"])


def run_network_path_case(
    env, logs, servers: dict, scenario: str, isolation: str
) -> None:
    """比较一个冷请求配置；DNS 负例先用相同双选项目标完成健康配对。"""

    case = f"{scenario}_{isolation}_http_1.1"
    spec = _format_case(scenario, servers)
    try:
        if spec.get("curlcode"):
            # 两侧各次执行均使用新进程；健康对照不能向 DNS 负例预填暖缓存。
            _run_pair(
                env,
                _format_case("both", servers),
                servers,
                isolation,
                f"{case}_control",
            )
        _run_pair(env, spec, servers, isolation, case)
    except Exception:
        if should_collect_service_logs():
            collect_service_logs_for_case(
                env,
                suite=_SUITE,
                case=case,
                logs=logs,
                meta={
                    "scenario": scenario,
                    "isolation": isolation,
                    "ports": {
                        name: int(server["port"]) for name, server in servers.items()
                    },
                },
            )
        raise
