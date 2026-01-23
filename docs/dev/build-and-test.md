# 构建与测试

本页面面向贡献者，给出在本地 **可复现** 的最小构建/测试路径，并说明哪些用例需要额外环境（如本地 httpbin、libcurl testenv）。

## 1. 构建

默认构建会启用 tests/examples/benchmarks（见 `CMakeLists.txt` 中的 `BUILD_TESTING/BUILD_EXAMPLES/BUILD_BENCHMARKS` 选项）。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

## 2. 运行 Qt Test（ctest）

```bash
ctest --test-dir build --output-on-failure
```

说明：
- 大部分测试可离线运行。
- 部分测试会探测本地 httpbin（`localhost:8935`），服务不可用时会 `QSKIP`（详见 `tests/README.md`）。

### 2.1 （可选）启动本地 httpbin（用于部分集成用例）

```bash
docker run -d -p 8935:80 --name qcurl-httpbin kennethreitz/httpbin
curl http://localhost:8935/get
```

停止并清理：

```bash
docker stop qcurl-httpbin && docker rm qcurl-httpbin
```

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
python3 tests/libcurl_consistency/run_gate.py --suite all --build
```

更多约束与说明请参考：`tests/libcurl_consistency/README.md`。

## 4. 性能基准与回归检测（可选）

- 性能基准：`benchmarks/`（运行方式与 CI 口径：`docs/reference/performance.md`）
- CI workflow：`.github/workflows/benchmark.yml`

