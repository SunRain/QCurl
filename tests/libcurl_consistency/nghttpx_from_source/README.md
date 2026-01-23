# 从源码构建 h3-capable `nghttpx`（供 curl testenv / libcurl_consistency 使用）

> 目标：为 `curl/tests/http/testenv` 提供一个 **带 HTTP/3（QUIC）能力** 的 `nghttpx`，
> 并将其安装到本项目的 `build/` 目录下，避免依赖系统包的编译选项差异。

## 产物与路径

- 安装前缀（默认）：`build/libcurl_consistency/nghttpx-h3/`
- 可执行文件：`build/libcurl_consistency/nghttpx-h3/bin/nghttpx`

## 构建方式（由 CMake 驱动）

该目录通过 `ExternalProject` 从上游 `nghttp2` release tarball 构建 `nghttpx`。

- 单独构建：
  - `cmake --build build --target qcurl_nghttpx_h3`
- 构建一致性 Qt Test 时自动触发：
  - `cmake --build build --target tst_LibcurlConsistency`

### CI（PR gate）推荐用法

在 CI 中建议显式构建一次 `qcurl_nghttpx_h3` 并做版本校验，以便在依赖缺失时尽早失败、并输出更清晰的错误：

- 构建：
  - `cmake --build build --target qcurl_nghttpx_h3`
- 校验（curl testenv 用该字符串判定 `nghttpx_with_h3`）：
  - `build/libcurl_consistency/nghttpx-h3/bin/nghttpx --version`
  - 预期输出包含 `ngtcp2/x.y.z`

### 离线/内网构建

如需避免构建时下载源码，可在配置阶段提供本地 tarball 路径：

- `cmake -S . -B build -DQCURL_LC_NGHTTP2_ARCHIVE="/path/to/nghttp2-1.68.0.tar.gz"`

为保证输入可审计/可重复，离线 tarball **必须与锁文件一致**：

- 锁文件：`tests/libcurl_consistency/nghttpx_from_source/nghttp2.lock`
  - `url=...`：上游下载地址（在线模式使用）
  - `sha256=...`：期望的 tarball sha256（离线/在线都应一致）
- 校验命令（示例）：
  - `sha256sum /path/to/nghttp2-1.68.0.tar.gz`
  - 将输出与 `nghttp2.lock` 的 `sha256=` 对比；不一致应视为“输入漂移”，禁止继续构建（避免镜像漂移/篡改导致不可复现）。

## 前置依赖（Arch Linux）

需要系统已安装 `libngtcp2` 与 `libnghttp3`（HTTP/3/QUIC 依赖），以及常见编译工具链。

当构建完成后，执行：

- `build/libcurl_consistency/nghttpx-h3/bin/nghttpx --version`

预期输出包含 `ngtcp2/x.y.z`（curl testenv 用该字符串判定 `nghttpx_with_h3`）。
