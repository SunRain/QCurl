# 快速开始（10 分钟跑通）

本指南目标：在 Linux + Qt6 + libcurl 环境下，完成 **构建 QCurl** 并跑通一个最小 HTTP 请求示例。

## 1. 依赖

- CMake 3.16+
- Qt6（QtCore / QtNetwork）
- libcurl 8.0+
- 编译器：GCC 11+ / Clang 14+（C++17）

## 2. 构建（默认启用 examples/tests）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

运行单元测试：

测试运行与门禁（offline/env/全量回归/libcurl_consistency）请参考：[`docs/dev/build-and-test.md`](../dev/build-and-test.md)。

## 3. 安装（可选）

如需 `find_package(QCurl)` / `pkg-config qcurl`，可执行安装：

```bash
sudo cmake --install build
```

## 4. 在你的项目中使用

### CMake（推荐）

```cmake
find_package(QCurl REQUIRED)
target_link_libraries(your_app PRIVATE QCurl::QCurl)
```

### pkg-config

```bash
g++ your_app.cpp $(pkg-config --cflags --libs qcurl) -o your_app
```

## 5. 最小请求示例

示例代码可参考根目录 `README.md` 中的 “代码示例-简单 GET 请求”。
