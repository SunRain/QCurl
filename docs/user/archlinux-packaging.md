# Arch Linux 本地开发包

从**当前 QCurl 工作区**构建单个 `qcurl` 包，用 pacman 管理安装文件。包包含运行库和开发文件，不拆 runtime/devel，不从远程 tag 下载另一份源码，也不是 AUR 配方。

支持 Arch Linux `x86_64`。未提交修改以及构建引用的未跟踪文件都会参与构建；无需先提交或初始化 `curl/` 子模块。项目版本号不唯一标识这些本地修改，产包成功不等于正式发布验收通过。

## 1. 依赖与构建

需要 Arch `base-devel` 环境、CMake 3.19+、Ninja、pkgconf，以及 `qt6-base>=6.10.3`、`curl>=7.85.0`、`zlib`。包检查脚本使用 CMake 的 JSON 读取能力，因此此打包入口比库本身的 CMake 3.16 下限稍高。Python 只用于下文可选的维护者回归，不是打包依赖。

在 QCurl 仓库根执行：

```bash
makepkg -D packaging/arch
```

从其他目录执行（替换为实际绝对路径，保留引号）：

```bash
makepkg -D "/绝对路径/QCurl/packaging/arch"
```

依赖不全时会报错退出。允许 pacman 安装依赖后，可以由用户选择增加 `--syncdeps`；不要使用 `--nodeps` 绕过依赖检查。makepkg 应以普通用户运行，不要用 sudo 执行整个打包过程。

入口使用 Release、系统 Qt/libcurl/zlib 和 QCurl 共享库配置，关闭示例、库自身测试、benchmark 和 bundled curl 一致性构建。**关闭测试不会关闭 TestSupport 开发库。** 不需要 Docker、HTTPBin、Node 或公网测试服务。

配方显式关闭 LTO，确保 TestSupport 交付普通原生静态对象：GCC 瘦 LTO 对象经 makepkg strip 后可能只剩归档外壳，文件存在却无法链接。该选择也避免要求下游使用同一 LTO 工具链，不改变四组件的安装能力。

默认构建目录为 `packaging/arch/src/qcurl-build`，安装暂存目录为 `packaging/arch/pkg/qcurl`；不会借用仓库已有的 `build*` 或 release stage。makepkg 自身的 `BUILDDIR`、`PKGDEST` 配置可以改变工作和输出位置。

## 2. 产物与安装

查看预期输出位置和包内容：

```bash
makepkg -D packaging/arch --packagelist
pacman -Qip packaging/arch/qcurl-2.0.0-1-x86_64.pkg.tar.zst
pacman -Qlp packaging/arch/qcurl-2.0.0-1-x86_64.pkg.tar.zst
```

以上文件名对应当前 `2.0.0-1`；更改版本或 `PKGDEST` 后，以 `--packagelist` 输出为准。

| 组件 | 消费 target | 包内物理内容 |
| --- | --- | --- |
| Core | `QCurl::QCurl` | `libQCurl.so.2.0.0`、SONAME/开发链接及公共头 |
| BlockingExtras | `QCurl::BlockingExtras` | INTERFACE 导出及公共头；实现在 Core 库内，没有独立 Blocking 库 |
| OtherExtras | `QCurl::OtherExtras` | `libQCurlOtherExtras.so.2.0.0`、链接及公共头；WebSocket 按实际 libcurl 能力启用 |
| TestSupport | `QCurl::TestSupport` | **`libQCurlTestSupport.a`**、mock/capture 公共头及导出，仅供开发测试 |

公共头安装到 `/usr/include/qcurl/`，CMake 配置到 `/usr/lib/cmake/QCurl/`，两个 `.pc` 到 `/usr/lib/pkgconfig/`。`LICENSE` 与 `THIRD_PARTY_NOTICES.md` 安装到 `/usr/share/licenses/qcurl/`。不安装私有头、测试程序、源码、bundled curl 或额外 debug 包。

用户明确决定安装到当前系统后执行：

```bash
sudo pacman -U packaging/arch/qcurl-2.0.0-1-x86_64.pkg.tar.zst
pacman -Q qcurl
pacman -Ql qcurl
```

这会修改当前系统；只想检验产物时使用下一节解包消费，或在隔离 Arch 环境中安装。无需额外安装脚本，文件归属由 pacman 管理。

当前 Core 在 2.x 内保证源码兼容，但 **ABI 非稳定，每次更新都要重新编译和链接下游**；`.so.2` 不代表旧二进制可以直接复用。TestSupport 与 OtherExtras 也必须来自同一包，不能跨版本混装。

同版本源码变化后，可增加 PKGBUILD 的 `pkgrel` 区分本地重包；项目版本变化时同步 `pkgver`。不一致会在 prepare 阶段报错。若确实要覆盖同名旧包，用户可显式加 `--force` 后重新 `pacman -U`；这会覆盖旧归档，应先保留所需旧包。不建议用 `--repackage` 验证源码变化，因为它跳过重新构建。

不要对主工作区执行清理式重置。需要全新验证时使用新的工作副本或 makepkg 工作目录，保留原来的源码修改与构建证据；本入口不自动清理其他目录。

## 3. 下游消费

推荐使用 CMake；独立程序与事件循环用法见[快速开始](quickstart.md)。默认 Core：

```cmake
find_package(QCurl CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE QCurl::QCurl)
```

完整包虽然安装了扩展文件，默认 `find_package` 只导入 Core，consumer 不需要主动查找 QtNetwork。按需请求扩展，例如：

```cmake
find_package(QCurl CONFIG REQUIRED COMPONENTS BlockingExtras OtherExtras TestSupport)
target_link_libraries(my_cli PRIVATE QCurl::BlockingExtras)
target_link_libraries(my_diagnostics PRIVATE QCurl::OtherExtras)
target_link_libraries(my_tests PRIVATE QCurl::TestSupport)
```

这些 target 示例需要放在相应 `add_executable` 之后。生产程序不应因为包里含有 TestSupport 就默认链接它。各组件的成熟度和边界不因打包改变，见[公共头与安装边界](../dev/architecture/public-header-boundary.md)。

现有 pkg-config 入口为 `qcurl` 和 `qcurl-other-extras`，没有独立 Blocking/TestSupport `.pc`。手工编译使用 C++17 和位置无关代码，避免新 Qt/GCC 工具链对导出符号产生 copy relocation；CMake 的 Qt imported target 会传递相关编译要求：

```bash
pkg-config --modversion qcurl qcurl-other-extras
c++ -std=c++17 -fPIC -DQT_NO_KEYWORDS main.cpp -o my_app \
  $(pkg-config --cflags --libs qcurl)
```

使用 OtherExtras 时将最后的包名改为 `qcurl-other-extras`。Core 的共享链接接口不向下游暴露 CURL/ZLIB 构建 target；系统包依赖仍必须安装。

## 4. 维护者验证

基础入口和失败路径检查（需要 Python/pytest，仅开发验证使用）：

```bash
bash -n packaging/arch/PKGBUILD
makepkg -D packaging/arch --printsrcinfo
python3 -m pytest -q tests/test_arch_packaging.py
```

`package()` 会检查完整安装面、共享库 SONAME/链接、静态 TestSupport、许可证、禁止文件和生产者路径泄漏。makepkg 随后仍会 strip 并生成包，因此验收还须针对**最终归档**重做检查并实际消费，不能只看暂存目录。

以下命令从仓库根运行，使用新的临时目录，不安装到主机：

```bash
work="$(mktemp -d -t qcurl-arch-check.XXXXXX)"
mkdir "$work/unpacked"
bsdtar -xf packaging/arch/qcurl-2.0.0-1-x86_64.pkg.tar.zst -C "$work/unpacked"
cmake "-DPACKAGE_ROOT=$work/unpacked" -DPACKAGE_VERSION=2.0.0 \
  "-DSOURCE_ROOT=$PWD" "-DBUILD_ROOT=$PWD/packaging/arch/src/qcurl-build" \
  -P packaging/arch/verify_package.cmake
python3 tests/arch/verify_install.py \
  --prefix "$work/unpacked/usr" --work-dir "$work/consumers"
```

内容检查使用当前工作区的 `surface_manifest.json` 和许可文件，所以必须针对同一份源码产物执行；自定义 `BUILDDIR` 时也同步检查参数。consumer 脚本复用现有四组件 smoke，额外验证默认/完整/未知组件边界、私有头不可见和两个 pkg-config 程序，并检查运行时加载的是待测包中的 QCurl 库。

在隔离 Arch 环境完成 `pacman -U` 后，还应运行同一 consumer 脚本，以 `--prefix /usr` 和新的 `--work-dir` 验证实际安装结果，并用 `pacman -Qo /usr/lib/libQCurlTestSupport.a` 检查归属。隔离环境本身不属于普通打包依赖。

脚本遇错返回非零状态，保留工作目录和完整失败诊断，不覆盖已有验证目录。分别记录语法/元数据、真实产包、最终包内容、解包消费、pacman 安装消费的结果；缺失任一层验证不能写成该层已通过。本地包验收不替代正式 release gate、协议一致性或 HTTP/3 支持验证。
