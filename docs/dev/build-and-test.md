# 构建与测试

本页面面向贡献者，给出在本地 **可复现** 的最小构建/测试路径，并说明哪些用例需要额外环境（如本地 httpbin、libcurl testenv）。

> 约定：`tests/README.md` 会链接到本文作为“回归命令单一入口”；如需调整全量回归/pytest 回归命令，请仅修改本文，避免口径分裂。

## 1. 构建

默认构建会启用 tests/examples/benchmarks（见 `CMakeLists.txt` 中的 `BUILD_TESTING/BUILD_EXAMPLES/BUILD_BENCHMARKS` 选项）。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

## 2. 运行 Qt Test（ctest）

```bash
# 门禁推荐：ctest 严格模式（默认仅跑 LABELS=offline，skip=fail）
python3 scripts/ctest_strict.py --build-dir build

# 运行门禁证据集合（LABELS=env；需要先准备依赖）
python3 scripts/ctest_strict.py --build-dir build --label-regex env

# 非门禁：直接运行 ctest（注意 QtTest 的 QSKIP 在 ctest 下会计为 Passed）
ctest --test-dir build --output-on-failure
```

说明：
- 默认门禁集合为 `offline`（不应触发 `QSKIP`，不依赖外部网络/服务）。
- `env` 组为**门禁证据集合**：仅包含依赖本地环境的用例（本地 httpbin、端口绑定、python 等），在门禁 runner 上应可稳定执行且不触发 `QSKIP`。
- `external_network` 为非门禁集合：包含依赖公网或外部服务的用例（部分 WebSocket/诊断等），可按需单独执行与评估，不建议与门禁证据混跑。
- `capability` 为非门禁集合：包含依赖 libcurl 构建能力差异的用例（network_path/proxy/http2 等）。缺失能力应通过 Fail/Warn 合同给出可证伪信号（不应再以 `QSKIP` 掩盖缺失），但仍不建议与门禁证据混跑。

### 2.1 HTTP/2（本地可控 + 强断言）

`tst_QCNetworkHttp2` 默认启动仓库内置的本地 node server（`tests/http2-test-server.js`），不依赖公网，并通过响应 JSON 提供可观察证据（`httpVersion/sessionId/streamId`）。

```bash
ctest --test-dir build -R tst_QCNetworkHttp2 --output-on-failure
```

可通过环境变量覆盖 base URL（例如对接 curl testenv）：
- `QCURL_HTTP2_TEST_BASE_URL`
- `QCURL_HTTP2_TEST_HTTP1_BASE_URL`

### 2.2 （可选）启动本地 httpbin（用于部分集成用例）

```bash
docker run -d -p 8935:80 --name qcurl-httpbin kennethreitz/httpbin
curl http://localhost:8935/get
```

停止并清理：

```bash
docker stop qcurl-httpbin && docker rm qcurl-httpbin
```

### 2.3 全量回归（本地自检；非门禁）

> 说明：该“全量回归”会跑到 `env/websocket/external_network/external_heavy` 等用例集合，其中外网依赖类用例 **不建议作为门禁**（仅用于开发者自检与问题定位）。

**前置自检（建议先做）**

```bash
python3 --version
node --version

# 本机端口绑定能力（受限环境可能 EPERM）
python3 -c "import socket; s=socket.socket(); s.bind(('127.0.0.1', 0)); print('ok', s.getsockname()); s.close()"

# 若要跑 env/httpbin：需本地 httpbin:8935
curl -fsS http://localhost:8935/get >/dev/null
```

**执行命令**

```bash
ctest --test-dir build --output-on-failure
```

常见权限失败信号：
- Docker 权限不足：`permission denied while trying to connect to the docker API at unix:///var/run/docker.sock`
- 本机 socket 被禁止：`curl: (7) failed to open socket: 不允许的操作`（或同类 `EPERM`）

## 3. libcurl_consistency Gate（可选）

一致性 gate 依赖上游 `curl/tests/http/testenv`，会启动本地 `httpd/nghttpx` 等服务。
在受限环境（禁止本机端口绑定）下可能无法运行，应在具备权限的 runner 上执行。

### 3.1 构建 gate 依赖（bundled curl）

```bash
git submodule update --init --recursive

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF -DBUILD_TESTING=ON \
  -DQCURL_BUILD_LIBCURL_CONSISTENCY=ON

cmake --build build --parallel
```

### 3.2 运行 gate

```bash
# 最小回归（建议先跑；默认套件 p0）
python3 tests/libcurl_consistency/run_gate.py --suite p0 --build

# 全量回归（PR gate：包含 ext；不允许 skipped）
QCURL_LC_EXT=1 python3 tests/libcurl_consistency/run_gate.py --suite all --with-ext --build
```

### 3.3 全量回归（pytest 直跑；可选）

> 注意：该套件会启动 curl testenv（本地 httpd/nghttpx/ws 等）并依赖本机端口绑定能力；受限环境可能直接失败。

```bash
# 默认集合
env -u QCURL_QTTEST python3 -m pytest tests/libcurl_consistency

# 启用 ext（PR gate）
env -u QCURL_QTTEST QCURL_LC_EXT=1 python3 -m pytest tests/libcurl_consistency
```

更多约束与说明请参考：`tests/libcurl_consistency/README.md`。

## 4. 性能基准与回归检测（可选）

- 性能基准：`benchmarks/`（运行方式与 CI 口径：`docs/reference/performance.md`）
- CI workflow：`.github/workflows/benchmark.yml`
