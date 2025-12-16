from __future__ import annotations

import pytest


def test_env_smoke(env, httpd, nghttpx, lc_ws_echo):
    assert env.http_port > 0
    assert env.https_port > 0
    assert httpd is not None

    # nghttpx fixture 可能返回 False（未安装/不可用）
    if env.have_h3():
        assert nghttpx is not False

    assert lc_ws_echo["port"] == env.ws_port
