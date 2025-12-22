"""
P0/P1 用例清单与参数模板。
- 供 baseline/QCurl 共同引用，避免分散硬编码。
- URL/端口需在运行时由 env 填充。
"""

# P0：下载
# 参考 curl/tests/http/test_02_download.py test_02_21/test_02_22
DOWNLOAD_DEFAULTS = {
    "count": 2,
    "pause_offset": 100 * 1023,
    "max_parallel": 5,
    "proto": "h2",
    "doc_serial": "data-1m",
    "doc_parallel": "data-10m",
}

# P0：上传
# 参考 curl/tests/http/test_07_upload.py test_07_15/test_07_17
UPLOAD_DEFAULTS = {
    "count": 2,
    "upload_size": 128 * 1024,
    "proto": "h2",
}

# P0：WebSocket
# 参考 curl/tests/http/test_20_websockets.py test_20_02/test_20_04
WS_DEFAULTS = {
    "payload_pingpong": 125 * "x",
    "model": 2,
}

P0_CASES = {
    # 下载：串行 + resume
    "download_serial_resume": {
        "suite": "p0_download",
        "case": "test_02_21_lib_serial",
        "client": "cli_hx_download",
        "args_template": [
            "-n",
            "{count}",
            "-P",
            "{pause_offset}",
            "-V",
            "{proto}",
            "{url}",
        ],
        "defaults": {
            "count": DOWNLOAD_DEFAULTS["count"],
            "pause_offset": DOWNLOAD_DEFAULTS["pause_offset"],
            "proto": DOWNLOAD_DEFAULTS["proto"],
            "docname": DOWNLOAD_DEFAULTS["doc_serial"],
            "url": "https://localhost:{https_port}/" + DOWNLOAD_DEFAULTS["doc_serial"],
        },
        "baseline_download_count": DOWNLOAD_DEFAULTS["count"],
        "qcurl_download_count": DOWNLOAD_DEFAULTS["count"],
    },
    # 下载：并行 + resume
    "download_parallel_resume": {
        "suite": "p0_download",
        "case": "test_02_22_lib_parallel_resume",
        "client": "cli_hx_download",
        "args_template": [
            "-n",
            "{count}",
            "-m",
            "{max_parallel}",
            "-P",
            "{pause_offset}",
            "-V",
            "{proto}",
            "{url}",
        ],
        "defaults": {
            "count": DOWNLOAD_DEFAULTS["count"],
            "max_parallel": DOWNLOAD_DEFAULTS["max_parallel"],
            "pause_offset": DOWNLOAD_DEFAULTS["pause_offset"],
            "proto": DOWNLOAD_DEFAULTS["proto"],
            "docname": DOWNLOAD_DEFAULTS["doc_parallel"],
            "url": "https://localhost:{https_port}/" + DOWNLOAD_DEFAULTS["doc_parallel"],
        },
        "baseline_download_count": DOWNLOAD_DEFAULTS["count"],
        "qcurl_download_count": DOWNLOAD_DEFAULTS["count"],
    },
    # 上传：PUT
    "upload_put": {
        "suite": "p0_upload",
        "case": "test_07_15_hx_put",
        "client": "cli_hx_upload",
        "args_template": [
            "-n",
            "{count}",
            "-S",
            "{upload_size}",
            "-l",
            "-V",
            "{proto}",
            "{url}",
        ],
        "defaults": {
            "count": UPLOAD_DEFAULTS["count"],
            "upload_size": UPLOAD_DEFAULTS["upload_size"],
            "proto": UPLOAD_DEFAULTS["proto"],
            "url": "https://localhost:{https_port}/curltest/put",
        },
        "baseline_download_count": UPLOAD_DEFAULTS["count"],
        "qcurl_download_count": UPLOAD_DEFAULTS["count"],
    },
    # 上传：POST + 复用
    "upload_post_reuse": {
        "suite": "p0_upload",
        "case": "test_07_17_hx_post_reuse",
        "client": "cli_hx_upload",
        "args_template": [
            "-n",
            "{count}",
            "-M",
            "POST",
            "-S",
            "{upload_size}",
            "-l",
            "-R",
            "-V",
            "{proto}",
            "{url}",
        ],
        "defaults": {
            "count": UPLOAD_DEFAULTS["count"],
            "upload_size": UPLOAD_DEFAULTS["upload_size"],
            "proto": UPLOAD_DEFAULTS["proto"],
            "url": "https://localhost:{https_port}/curltest/echo",
        },
        "baseline_download_count": UPLOAD_DEFAULTS["count"],
        "qcurl_download_count": UPLOAD_DEFAULTS["count"],
    },
    # WebSocket：ping/pong
    "ws_pingpong_small": {
        "suite": "p0_ws",
        "case": "test_20_02_pingpong_small",
        "client": "cli_ws_pingpong",
        "args_template": [
            "{url}",
            "{payload}",
        ],
        "defaults": {
            "payload": WS_DEFAULTS["payload_pingpong"],
            "url": "ws://localhost:{ws_port}/",
        },
        "qcurl_download_count": 1,
    },
    # WebSocket：data frames 小数据
    "ws_data_small": {
        "suite": "p0_ws",
        "case": "test_20_04_data_small",
        "client": "cli_ws_data",
        "args_template": [
            "-{model}",
            "-m",
            "1",
            "-M",
            "10",
            "{url}",
        ],
        "defaults": {
            "model": WS_DEFAULTS["model"],
            "url": "ws://localhost:{ws_port}/",
        },
        "qcurl_download_count": 1,
    },

    # 下载：中断 + Range 续传（P0）
    "download_range_resume": {
        "suite": "p0_download",
        "case": "lc_range_resume",
        "client": "cli_hx_range_resume",
        "args_template": [
            "-A",
            "{abort_offset}",
            "-S",
            "{file_size}",
            "-V",
            "{proto}",
            "{url}",
        ],
        "defaults": {
            "proto": DOWNLOAD_DEFAULTS["proto"],
            "docname": DOWNLOAD_DEFAULTS["doc_parallel"],
            "abort_offset": 256 * 1024,
            "file_size": 10 * 1024 * 1024,
            "url": "https://localhost:{https_port}/" + DOWNLOAD_DEFAULTS["doc_parallel"],
        },
        "baseline_download_count": 1,
        "qcurl_download_count": 1,
    },
}

P1_CASES = {
    # P1：CURLOPT_POSTFIELDS 二进制（含 \0），参考 test1531 的语义
    "postfields_binary_1531": {
        "suite": "p1_postfields",
        "case": "lc_postfields_binary_1531",
        "client": "cli_postfields_binary",
        "args_template": [
            "-V",
            "{proto}",
            "{url}",
        ],
        "defaults": {
            "proto": DOWNLOAD_DEFAULTS["proto"],
            "url": "https://localhost:{https_port}/curltest/echo",
        },
        "baseline_download_count": 1,
        "qcurl_download_count": 1,
    },
}

P1_PROXY_CASES = {
    # P1：HTTP proxy（Basic auth），观测 proxy 收到的绝对形式请求行 + Proxy-* 头
    "proxy_http_basic_auth": {
        "suite": "p1_proxy",
        "case": "lc_proxy_http_basic_auth",
        "client": "cli_lc_http",
        "args_template": [
            "-V",
            "http/1.1",
            "--proxy",
            "{proxy_url}",
            "--proxy-user",
            "{proxy_user}",
            "--proxy-pass",
            "{proxy_pass}",
            "{url}",
        ],
        "defaults": {
            "url": "http://localhost:{http_port}/proxy/ok.txt",
        },
        "baseline_download_count": 1,
        "qcurl_download_count": 1,
    },
    # P1：HTTPS over proxy（CONNECT + Basic auth），观测 CONNECT 目标与 Proxy-* 头
    "proxy_https_connect_basic_auth": {
        "suite": "p1_proxy",
        "case": "lc_proxy_https_connect_basic_auth",
        "client": "cli_lc_http",
        "args_template": [
            "-V",
            "h2",
            "--proxy",
            "{proxy_url}",
            "--proxy-user",
            "{proxy_user}",
            "--proxy-pass",
            "{proxy_pass}",
            "{url}",
        ],
        "defaults": {
            "url": "https://localhost:{https_port}/proxy/ok.txt",
        },
        "baseline_download_count": 1,
        "qcurl_download_count": 1,
    },
}

# 可选扩展：默认不跑（由 pytest driver 控制），避免引入与数据无关的波动点（LC-11）
EXT_CASES = {
    # 并发下载压力（仅用于 h2/h3 的 multiplexing/调度路径观测）
    "ext_download_parallel_stress": {
        "suite": "ext_download",
        "case": "lc_ext_download_parallel_stress",
        "client": "cli_hx_download",
        "args_template": [
            "-n",
            "{count}",
            "-m",
            "{max_parallel}",
            "-V",
            "{proto}",
            "{url}",
        ],
        "defaults": {
            "count": 10,
            "max_parallel": 10,
            "proto": "h2",
            "docname": DOWNLOAD_DEFAULTS["doc_serial"],
            "url": "https://localhost:{https_port}/" + DOWNLOAD_DEFAULTS["doc_serial"],
        },
        "baseline_download_count": 10,
        "qcurl_download_count": 10,
    },

    # 多资源 GET（test2402 风格：HTTP/2 multi）
    "ext_multi_get4_h2": {
        "suite": "ext_multi",
        "case": "lc_ext_multi_get4_2402",
        "client": "cli_hx_multi_get4",
        "args_template": [
            "-n",
            "{count}",
            "-I",
            "{req_id}",
            "-V",
            "{proto}",
            "{url_prefix}",
        ],
        "defaults": {
            "count": 4,
            "proto": "h2",
            "docname": "path/2402",
            "url_prefix": "https://localhost:{https_port}/path/2402",
        },
        "protos": ["h2"],
        "baseline_download_count": 4,
        "qcurl_download_count": 4,
        "expected_requests": 4,
    },

    # 多资源 GET（test2502 风格：HTTP/3 multi）
    "ext_multi_get4_h3": {
        "suite": "ext_multi",
        "case": "lc_ext_multi_get4_2502",
        "client": "cli_hx_multi_get4",
        "args_template": [
            "-n",
            "{count}",
            "-I",
            "{req_id}",
            "-V",
            "{proto}",
            "{url_prefix}",
        ],
        "defaults": {
            "count": 4,
            "proto": "h3",
            "docname": "path/2502",
            "url_prefix": "https://localhost:{https_port}/path/2502",
        },
        "protos": ["h3"],
        "baseline_download_count": 4,
        "qcurl_download_count": 4,
        "expected_requests": 4,
    },
}

# 可选扩展：WebSocket 低层帧语义（LC-19/LC-20）
# - 默认不跑（由 QCURL_LC_EXT=1 控制）
# - 基线客户端与服务端场景由 libcurl_consistency 自建，不依赖 curl/tests/http/testenv 的 ws_echo_server
EXT_WS_CASES = {
    # LC-19：服务端 Ping → 客户端手动 Pong（对齐 test2301 的 ping→pong 语义）
    "ext_ws_ping_2301": {
        "suite": "ext_ws",
        "case": "lc_ext_ws_ping_2301",
        "scenario": "lc_ping",
        "url": "ws://localhost:{ws_port}/?scenario=lc_ping",
        "qcurl_download_count": 1,
    },

    # LC-34：握手扩展协商（permessage-deflate），仅对齐可观测握手头（不依赖数据帧压缩）
    "ext_ws_deflate_ping": {
        "suite": "ext_ws",
        "case": "lc_ext_ws_deflate_ping",
        "scenario": "lc_ping_deflate",
        "url": "ws://localhost:{ws_port}/?scenario=lc_ping",
        "qcurl_download_count": 1,
    },

    # LC-20：服务端发送 text/binary/ping/pong/close（对齐 test2700 的帧类型覆盖）
    "ext_ws_frame_types_2700": {
        "suite": "ext_ws",
        "case": "lc_ext_ws_frame_types_2700",
        "scenario": "lc_frame_types",
        "url": "ws://localhost:{ws_port}/?scenario=lc_frame_types",
        "qcurl_download_count": 1,
    },
}

# 后续可在此扩展更高阶清单
