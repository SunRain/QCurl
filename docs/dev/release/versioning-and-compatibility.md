# 版本与兼容政策

从 **2.1.0** 起，QCurl 以 `major.minor` 为兼容版本线。后续 patch 必须向后兼容本线所有较早正式 patch 的公共源码、受支持 shared ABI、安装消费和公开行为。改变 major 或 minor 后不承诺兼容，升级前须查阅迁移说明并重建下游。

这是项目自定义版本政策，**不是 SemVer 的 minor 兼容规则**；不能从 major 相同推导兼容。历史 2.0 的合同与 tag 保留原义，不追溯适用本政策。当前发布输入和资格条件见唯一 [2.1 发布合同](2.1.0-release-contract.md)，执行命令见[发布操作](release-procedure.md)。

## 1. 版本数字与方向

| 变化 | 承诺 |
| --- | --- |
| `2.1.0 → 2.1.1` | 同线向后兼容 |
| `2.1.0 / 2.1.1 → 2.1.2` | 保留此前每个 patch 的公共合同，包括中间 patch 新增能力 |
| `2.0.x → 2.1.x`、`2.1.x → 2.2.x` | 新版本线；不保证源码、ABI、安装或行为兼容 |
| `2.x → 3.x` | 不保证兼容；major 也可表达较大的产品或架构变化 |
| `2.1.2 → 2.1.0` | 不承诺降级兼容，新接口不能假定旧库提供 |

patch 以兼容修复为主，新增能力优先进入下一 minor。兼容新增若进入 patch，也不能引入旧合法调用的重载歧义、布局变化或行为破坏。新版本线无需人为制造不兼容，但须切换加载器和包的版本线边界。

## 2. 公共范围

| 组件 | 源码、安装和行为 | shared ABI 的适用方式 |
| --- | --- | --- |
| Core / `QCurl::QCurl` | 全部已发布 Public 安装面 | 旧 consumer 使用本线后续 patch 的 Core |
| Blocking Extras / `QCurl::BlockingExtras` | 同上；仍须显式 opt-in | 实现在 Core 中，共用 Core ABI；没有独立 Blocking 运行库 |
| Other Extras / `QCurl::OtherExtras` | 同上，包含 Diagnostics / WebSocket 的 Preview API | 对应 shared 公共 ABI；按完整包升级 |
| Test Support / `QCurl::TestSupport` | 同上，范围是公开测试支持接口 | 静态库不提供运行时替换；旧程序已链接的 TestSupport 代码与新 Core 的桥接仍须可运行 |

`Preview` 表示成熟度和功能完整度，**不豁免 patch 兼容**。破坏已发布 Preview 公共接口也须进入新版本线。`Internal`、未安装私有头和实现类没有独立公共承诺，但内部修改不得间接破坏公共 consumer。

精确组件、可见性、成熟度及安装头由 [surface manifest](../../../tests/public_api/surface_manifest.json) 枚举，检查方法见[公共头边界](../architecture/public-header-boundary.md)。默认 Core 和显式 opt-in 是消费边界，不是兼容保障的强弱划分。

## 3. patch 必须保留什么

| 维度 | 必须保持的合同 |
| --- | --- |
| 公共源码 | 头路径、类型、接口签名、枚举含义、公开编译宏与合法调用方式 |
| shared ABI | 旧导出、调用约定、类型布局和对齐、虚表及公共类型关系；符号名字相同不足以证明兼容 |
| 构建和安装 | target、组件名、include 方式、公开定义和链接要求；旧 consumer 仍能正常配置和重建 |
| 最低依赖 | 本线承诺的 Qt、libcurl、编译器与构建工具下限和受支持能力；抬高下限进入下一 minor |
| 公开行为 | 返回值、错误语义、默认策略、ownership、线程约束、取消、重试、回调次数与生命周期 |

修复违反既有文档、协议或安全合同的缺陷不要求保留错误行为；发布说明须交代依据并有边界验证。若修复需要改变已承诺的合法行为，应进入新版本线，不能静默宣布 patch 例外。同线优先改私有实现，不为未来可能扩展而全库改成 PIMPL。

## 4. 二进制支持环境

首个正式 shared 二进制支持配置为 **Linux x86_64、GCC/libstdc++、C++17、`_GLIBCXX_USE_CXX11_ABI=1`、Release、未插桩、完整组件与默认功能**。最低依赖为 Qt **6.10.3**、libcurl **8.14.1**；源码所需 CMake 3.16、GCC 11 下限同样不得在本线 patch 抬高。

每次正式参考必须记录实际 OS、架构、compiler、标准库/GLIBC、Qt/libcurl 的编译和运行版本、配置、开关及 flags。上述信息组成支持 tuple；更换架构、标准库 ABI、Qt ABI、Debug/Release 或功能配置后，不能把不匹配报告当作同一 tuple 的兼容证明。精确输入以该发布参考和 final 证据为准，不能用宿主当前高版本证明最低依赖可用。

Clang、ASan/UBSan/LSan 和 static 继续承担产品验证职责，不据此承诺其二进制与 GCC Release 任意互换。static consumer 验证旧源码重建，shared consumer 另验旧二进制不重编译运行。其他平台或交付配置进入二进制支持范围前，须建立相应参考和验证。

升级应整体替换来自同一候选的 Core 与 Other Extras；不承诺旧 Other Extras 与新 Core 等任意混装。TestSupport 的 private bridge 不成为公共源码 API，但不能导致已链接旧 TestSupport 的程序失效。

## 5. Linux SONAME 与消费规则

两个 shared target 均由 `PROJECT_VERSION` 派生：`VERSION=major.minor.patch`、`SOVERSION=major.minor`。

| 发布 | Core SONAME | Other Extras SONAME |
| --- | --- | --- |
| 历史 2.0.0 | `libQCurl.so.2` | `libQCurlOtherExtras.so.2` |
| 2.1.x | `libQCurl.so.2.1` | `libQCurlOtherExtras.so.2.1` |
| 2.2.x | `libQCurl.so.2.2` | `libQCurlOtherExtras.so.2.2` |

例如 2.1.2 的实体文件是 `libQCurl.so.2.1.2`，SONAME 仍为 `.so.2.1`。2.1 不提供 `.so.2` 指向 `.so.2.1` 的兼容链接；不能用重命名或链接让旧线程序静默加载新线。未来 patch 若无法保持 ABI，须进入新 minor，不能只换 SONAME 后继续发布 patch。

CMake 使用 `SameMinorVersion`：候选 major/minor 与请求一致，且不得低于请求的最低 patch。下游写明所需最低 patch：

```cmake
find_package(QCurl 2.1.0 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE QCurl::QCurl)
```

若使用后续 patch 新增接口，应把 `2.1.0` 提高到首次提供该接口的 patch。`EXACT` 会禁止兼容更新，不是通常的消费方式；不推荐用跨 minor 的 CMake 版本区间替代版本线规则。

假定安装候选为 2.1.2，请求 2.1.0、2.1.1、2.1.2 应接受，2.0.0、2.2.0、2.1.3 应拒绝；这说明匹配规则，不表示这些 patch 都已发布。无版本请求只选择安装包及默认组件，不能表达所需版本线。

pkg-config 自身没有 CMake 的版本线规则，调用方须明确两个约束：

```bash
pkg-config --exists 'qcurl >= 2.1.0' 'qcurl < 2.2.0'
```

Other Extras 使用同样的 `qcurl-other-extras` 区间；包内 `.pc` 对 Core 保持精确同版依赖，避免混装。组件 opt-in、静态传递依赖与初始化见[快速开始](../../user/quickstart.md)。

## 6. 参考与发布证据

每个正式 patch 保留真实交付构建的两库 ABI snapshot、安装头、支持 tuple、四组件 consumer 源码及只构建一次的可执行文件，作为普通 compatibility-reference 发布资产。参考保留对应调试信息、来源 commit/submodule、profile 和原始运行日志；完整性复用正式 manifest 和 checksum，不另建 promotion 或人工 PASS 流程。

新线首版 `M.m.0` 使用 `snapshot`：捕获参考，再由独立必需 gate 做自身 round-trip，证明参考可用；结果分类为 `REFERENCE_VALIDATED`、`patchCompatibility=false`。它没有同线前序版本，**不能称为一次实际 patch 兼容 PASS**，也不要求与上一 minor ABI 兼容。

后续 patch 使用 `current`，对照本线首个正式 `vM.m.0` 与候选之前最近的正式 patch 参考。两者相同则核对身份后去重；缺失、损坏、错线、错 tuple 或来源不明的参考均阻断 final。两个烟测输入不替代累计公共合同与行为回归。

发布证据须分别证明旧源码重建、两库 ABI、旧二进制不重编译运行和公开行为。两库 ABI 明确使用参考/候选的真实 ELF 做完整及残余比较，两侧 snapshot 仍为必需归档和语义校验输入，不能只比较 XML 就放行。动态符号 allowlist 只检查意外导出，不能替代 ABI 分析；ABI 报告也不能替代真实 consumer。操作、采用真实 ELF 的原因和错误判据见[ABI 参考操作](stable-abi-contract-and-baseline.md)。

本政策不自动承诺所有旧版本线长期维护，也不规定多开发包在同一前缀共存。安全维护范围见 [SECURITY](../../../SECURITY.md)。
