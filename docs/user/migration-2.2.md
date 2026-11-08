# 从 2.1 迁移到 2.2

2.2.0 是新兼容线首版，收拢 `v2.1.1` tag 之后的开发成果。**跨 minor 不保证源码、ABI、安装消费或公开行为兼容**；升级须核对下列行为并重新编译、链接全部下游。2.2 后续 patch 才承担本线已发布 Public 组件（含 Preview、TestSupport）的兼容承诺，见[版本政策](../dev/release/versioning-and-compatibility.md)。

本文描述源码与包的迁移，不表示 2.2.0 已发布或取得 full/final 资格。从 2.0 或更早版本升级，先完成[2.0 → 2.1 迁移](migration-2.1.md)。

## 1. 更新消费版本并重建

```cmake
find_package(QCurl 2.2.0 CONFIG REQUIRED)
target_link_libraries(app PRIVATE QCurl::QCurl)
```

需要扩展时继续显式添加 `COMPONENTS BlockingExtras OtherExtras TestSupport` 并链接相应 target；默认 Core 不自动导入这些组件。组件边界、Qt 6.10.3 / libcurl 8.14.1 下限没有变化，完整示例见 [Quickstart](quickstart.md)。

CMake 保持 `SameMinorVersion`：2.2.0 包接受最低 2.2.0，拒绝 2.1.x 和更高版本请求。pkg-config 必须显式限定上下界：

```bash
pkg-config --exists 'qcurl >= 2.2.0' 'qcurl < 2.3.0'
```

Linux 两运行库 SONAME 从 `.so.2.1` 改为 `.so.2.2`，首版实体为 `libQCurl.so.2.2.0`、`libQCurlOtherExtras.so.2.2.0`。不提供旧 SONAME 兼容链接；不能让 2.1 旧程序通过手工链接加载 2.2 库。Core、Other Extras 和 TestSupport 按同一候选整体重建和部署；Blocking Extras 实现仍随 Core 交付。

## 2. 核对请求策略的行为变化

Core async 与 Blocking Extras 现在共用请求策略。以下调整会影响已有调用，即使调用方源码仍能编译：

| 场景 | 2.2 行为 | 调用方调整 |
| --- | --- | --- |
| 显式代理遇到 `no_proxy` / `NO_PROXY` | 显式代理优先，包括绕过列表为 `*`；非法主机或端口直接拒绝 | 要直连时显式使用 None，不能靠环境变量绕过已配置代理 |
| 带凭据 HTTP/HTTPS 代理 | 使用 libcurl 原生挑战协商，不再固定预发送 Basic | 代理须正确处理挑战；可能重发的上传 body 必须可回放 |
| 必需共同选项无法应用 | 重定向、Referer、解压、限速、敏感头授权等生效配置失败会终止请求 | 检查错误结果和诊断；不能依赖告警后忽略配置继续发送 |
| 未显式设置 HTTP 版本 | 两个执行器均沿用 libcurl 默认协商 | 必须表达 HTTP/1.1 意图时调用 `setHttpVersion(Http1_1)` |
| 空 CA 路径 | 使用 libcurl/TLS backend 默认信任源，不扫描硬编码系统 CA 文件 | 使用私有 CA 或固定信任集合时显式设置 CA；不要关闭证书验证 |
| raw `Authorization` / `Referer` / `Accept-Encoding` | raw 值（含空值）优先；空 Accept-Encoding 抑制头并关闭自动解压 | 不要同时依赖被覆盖配置；空头与未设置含义不同 |
| 数值与 middleware 配置 | 生效值按原生范围检查，非法值不截断；cache/mock 不能掩盖非法请求 | 在发送前纠正负数/超界配置，并处理 `InvalidRequest` |

未配置代理或 None 继续禁用环境代理。连接/总超时的零值继续沿用 libcurl 默认；只有明确的 HTTP/3 能力降级允许告警继续。跨 origin 敏感认证须显式授权，libcurl 不承诺过滤任意自定义敏感头。完整优先级与错误边界见[常见配置](configuration.md)。

已知 libcurl 8.14.1/OpenSSL 在 HTTPS 代理返回 `407` 并关闭连接后，原生认证重连可能返回 `CURLE_SEND_ERROR (55)`；QCurl 保留该原生错误，不切换到明文代理或另一认证执行器。

## 3. 按需启用接收空闲超时

新增 `QCNetworkTimeoutConfig::receiveIdleTimeout()` / `setReceiveIdleTimeout()`。默认、`std::nullopt`、`0ms` 均禁用，已有调用不会自动启用。正值范围为 `1..2,147,483,647ms`，负数或超界值以 `InvalidRequest` 拒绝。

```cpp
QCurl::QCNetworkTimeoutConfig timeout;
timeout.setConnectTimeout(std::chrono::seconds(10));
timeout.setReceiveIdleTimeout(std::chrono::seconds(10));
request.setTimeoutConfig(timeout);
```

连接完成后等待首个响应字节，以及响应头/正文两次接收活动之间，各有独立预算。连接、排队和退避不消耗预算；只有实际接收续期。用户接收暂停和背压停止计时，恢复后给予完整预算；上传活动、source-not-ready 与消费缓冲数据不会续期。它不替代 total timeout 或 low-speed 限制。

到期仍以 `NetworkError::ConnectionTimeout` 结束；新增 `QCNetworkReply::isReceiveIdleTimeout()` 标记最终错误来源。`diagnosticCurlCode()` 只记录实际原生结果，主动 detach/mock 没有适用结果时为 0，不把高层超时伪造为 curl 28。两个值在首次终态通知前确定，须在 reply 所属线程读取。

Blocking Extras 对生效正值返回 `UnsupportedCapability` 且不发送；空值和零保持既有同步行为。TestSupport capture 新增 `receiveIdleTimeoutMs()` / `setReceiveIdleTimeoutMs(std::optional<qint64>)`，保留未设置、零与正值的区别；升级测试辅助库时同步重建 consumer。详细时序见[接收空闲配置](configuration.md#接收空闲超时)。

## 4. 迁移验证与发布证据

下游应运行真实安装包 consumer，并复核代理/认证、HTTP 协商、CA 来源、配置失败和超时处理。已编译的 2.1 程序或历史参考不能证明 2.2 的运行行为。

2.2.0 发布走新线 `snapshot` 与独立 round-trip，不要求与 2.1 ABI 兼容。成功的 `REFERENCE_VALIDATED` / `patchCompatibility=false` 只证明首版参考可供后续比较；正式资格还须满足[2.2.0 发布合同](../dev/release/2.2.0-release-contract.md)的 clean C1、fresh 五树 full/final、独立校验和真实资产回验。
