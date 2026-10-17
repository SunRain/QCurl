# 从 2.0 迁移到 2.1

2.1.0 是新的兼容版本线，**不保证与 2.0.x 的源码、ABI、安装消费或行为兼容**。先按下文修改源码并重新编译、链接全部下游，再整体部署 2.1 包。不能让旧 `.so.2` 程序通过手工链接加载 `.so.2.1`。

本页按 `v2.0.0` 公共头、安装面和实现与当前候选的差异整理；它是迁移说明，不表示候选已经通过 release gate 或已经发布。从 1.0 升级时还需阅读[迁移到 2.0](migration-2.0.md)。

## 1. 版本、依赖与包消费

最低 libcurl 从 `v2.0.0` 的 **7.85.0 提高到 8.14.1**，构建头、链接库和运行库均须更新；仅替换开发头不够。Qt 下限仍为 **6.10.3**。libcurl 的 WebSocket 手动 Pong 与交错控制帧修复属于该下限的依据；HTTP/3 仍要求实际 QUIC backend，不能由版本号推断。

CMake consumer 显式请求新线：

```cmake
find_package(QCurl 2.1.0 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE QCurl::QCurl)
```

扩展仍用 `COMPONENTS BlockingExtras OtherExtras TestSupport` 按需选择，不新增库或组件。包规则从 `SameMajorVersion` 改为 `SameMinorVersion`；2.0 请求不会由 2.1 静默满足。不要以 `EXACT` 代替最低 patch，也不要删除版本请求来绕过迁移边界。

pkg-config consumer 同时约束下限与下一 minor：

```bash
pkg-config --exists 'qcurl >= 2.1.0' 'qcurl < 2.2.0'
```

Other Extras 对应 `qcurl-other-extras`。其包内 Core 依赖保持精确同版，完整包内的 Core、Other Extras、TestSupport 不应任意混装。

Linux 两个 shared 库的 SONAME 从 `.so.2` 变为 `.so.2.1`，实体文件为 `libQCurl.so.2.1.0` 和 `libQCurlOtherExtras.so.2.1.0`；不提供旧 SONAME 兼容链接。跨线重建与本线后续 patch 的旧二进制保护是两个不同要求。

## 2. 真正需要调整的源码与行为

### 移除 share-DNS 配置

`QCNetworkAccessManager::ShareHandleConfig::shareDnsCache()` 和 `setShareDnsCache(bool)` 已删除。移除对应调用；ShareHandleConfig 仅控制本 manager 的显式 Cookie 与 SSL session store，不控制 DNS 或连接。

DNS/连接缓存现在由传输 CURLM 域拥有。需要覆盖解析时在请求上设置 `setResolveOverride()`，再按实际共享需求选择 `QCNetworkResolveIsolation`；这不是旧 share-DNS 开关的一对一替换。

```cpp
QCurl::QCNetworkRequest request(QUrl{QStringLiteral("https://service.invalid/")});
const auto result = request.setResolveOverride(
    {QStringLiteral("service.invalid:443:127.0.0.1")});
if (result != QCurl::QCNetworkConfigUpdateResult::Applied) {
    // 拒绝发送；旧请求配置保持不变。
    return;
}
```

该片段放在调用方可提前返回的函数中；完整请求生命周期见[快速开始](quickstart.md)。解析覆盖不会关闭 TLS 主机名或证书检查。

### 解析覆盖公开，设置结果须检查

`setResolveOverride()`、`resolveOverride()` 从旧的高级网络路径条件编译区提升为普通 Core 公共 API，无需导出 `QCURL_ENABLE_ADVANCED_REQUEST_NETWORK_PATH_API`。其余高级路径 API 仍是内部测试能力，不应为使用解析覆盖而向安装 target 添加整组宏。

曾使用该内部开关的调用方还须调整返回类型：`setResolveOverride()` 现在返回 `QCNetworkConfigUpdateResult`，不再返回 `QCNetworkRequest &`。将链式调用拆开并检查 `Applied` / `InvalidArgument`。输入仅接受规范化的 `host:port:IPv4`，端口为 1–65535；Unicode 主机按 `QUrl::toAce()` 规范化。空列表清除，任一非法项使整个更新拒绝并保留旧值；不接受 libcurl 的全部扩展语法。

新隔离策略默认为 **Isolated**：完整有效覆盖等价的异步请求可共享域内资源，不同规则以及无覆盖请求不跨域复用。显式 **NativeShared** 才使用同线程固定 CURLM 的原生 DNS/连接共享语义，不同 manager 也可能观察到暖缓存。清除覆盖不等于 flush，不重置隔离策略；Cookie/TLS 的显式 store 仍按 manager 隔离。Blocking 每次独立 easy，不提供跨调用共享池。

### 连接限额按每个 CURLM 应用

`multiMaxTotalConnections`、`multiMaxHostConnections`、`multiMaxConcurrentStreams`、`multiMaxConnects` 的配置作用域是**每个传输 CURLM**。同线程存在多个隔离域后，合计连接可超过单域上限；不能继续把这些值当作线程或进程的总配额。若业务需要全局请求 admission，使用已有 manager/lane 调度合同；连接等待与请求排队仍是两层约束。

## 3. 新增能力

- RetryPolicy 增加 `setMaxCumulativeDelay()` 与 `setMaxElapsedTime()`：可选非负毫秒预算，默认不限制，非法值保留旧值。预算在决定下一次等待时检查，不中断初次或正在传输的请求，也不替代传输超时；恰好达到上限允许，等待中取消不冲销已接受的计划等待。
- Reply 增加 `retryAttemptCount()`、`cumulativeRetryDelay()` 与独立 `retryAfterDelay()` 查询。次数不含首次传输，累计值不是实际睡眠时长；Retry-After 读取不要求开启重试，合法 0 保留为 0 ms，非法/负值/溢出返回空值，HTTP-date 按查询时刻计算。
- 增加从当前工作区构建的 [Arch 完整开发包](archlinux-packaging.md)。它包含四个逻辑组件、三个物理库，产包成功不等于正式资格通过。

这些能力不自动接管应用上层重试；避免让库和上层同时无边界重试。更完整的参数边界见[常见配置](configuration.md)。

## 4. 缺陷修复与调用方核对

| 修复 | 调用方应核对 |
| --- | --- |
| 原生终态 HTTP 状态取 `CURLINFO_RESPONSE_CODE`；缓存命中恢复缓存元数据状态 | 代理 CONNECT 被拒绝、未收到目标响应时状态为 0，不能把代理 403 当成目标 HTTP 403；继续以 error 为主判断并保留 raw headers 诊断 |
| 中间件注销立即结束当前注册，进行中的请求跳过尚未开始和后续阶段回调；重注册只参与新请求 | 不再等待已注销中间件的后续 response/error 回调；不要跨回调保存借用 reply；同地址重注册不会恢复旧请求注册 |
| 中间件回调中销毁 manager、reply、job，以及下载设备写入期间取消/销毁的生命周期处理 | 错误和取消结果必须按 job/reply 的公开终态读取，不能依赖对象在回调后仍存活；不得自行延长裸指针借用 |
| WebSocket pool 在完成 future 前提交连接、容量与 lease 状态；移除旧连接后才通知 | owner-thread 同步 continuation 可查询、clear、再次 acquire 或销毁 pool；clear 只处理当次选中的旧连接，迟到通知不能重新激活旧 lease |
| 子项目构建的生成文件与安装路径独立于顶层工程 | `add_subdirectory` 使用者从实际安装前缀消费生成头，不再依赖顶层源码/构建目录泄漏 |

这些项目修复既有生命周期或观测错误，不新增 shim、第二传输路线或公共 ownership 模型。WebSocket 仍是 Preview 且 owner-thread-only；Preview 不使同线后续 patch 可以破坏公共用法。

## 5. 完成迁移后如何更新 patch

从 2.1.0 起，同 `major.minor` 后续 patch 保证全部已发布 Public 组件的源码、受支持 shared ABI、安装消费与公开行为向后兼容，包含 Preview。使用 2.1.0 接口的下游可继续请求最低 2.1.0；若用后续 patch 新增接口，应提高最低 patch。

在正式支持的 Linux x86_64 GCC/libstdc++ C++17、Release shared、未插桩、相同 Qt/标准库 ABI 与功能配置中，旧 shared consumer 使用后续兼容 patch 无须重编译。static 仍需重建才能获得更新；不同 tuple、降级、任意组件混装不在此承诺内。跨 2.2 等新线须重新核对迁移并重建。

这是自定义版本线政策，不承诺整个 2.x 兼容，也不追溯改变 2.0 历史合同。2.1.0 首版捕获参考并自测可用，不能由此声称一次真实 patch 升级已经验证；资格和已验证边界以[当前发布合同](../dev/release/2.1.0-release-contract.md)绑定的真实机器证据为准。
