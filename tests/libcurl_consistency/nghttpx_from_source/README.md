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

### 离线/内网构建

如需避免构建时下载源码，可在配置阶段提供本地 tarball 路径：

- `cmake -S . -B build -DQCURL_LC_NGHTTP2_ARCHIVE="/path/to/nghttp2-1.68.0.tar.gz"`

## 前置依赖（Arch Linux）

需要系统已安装 `libngtcp2` 与 `libnghttp3`（HTTP/3/QUIC 依赖），以及常见编译工具链。

当构建完成后，执行：

- `build/libcurl_consistency/nghttpx-h3/bin/nghttpx --version`

预期输出包含 `ngtcp2/x.y.z`（curl testenv 用该字符串判定 `nghttpx_with_h3`）。
