# libcurl binding contract

本文定义 `src/` 中生产 libcurl binding 的创建、驱动、所有权和诊断边界，适用于 Core async、Blocking Extras、WebSocket、cookie bridge、share handle 与 diagnostics。`tests/libcurl_consistency/` 的 baseline client 是上游行为参照，允许直接使用 libcurl API，不代表生产 binding 的实现方式；本地方案也不替代本合同。

## easy handle 创建合同

`QCCurlHandleManager` 是生产代码唯一的 `CURL *` easy handle 创建入口。顺序固定为：

1. 取得 QCurl runtime lease，确认进程级 libcurl runtime 处于 `Running`。
2. 调用 `curl_easy_init()`。
3. 应用 common easy defaults，目前包含 `CURLOPT_NOSIGNAL=1L`。

Core async、Blocking Extras、WebSocket 和 cookie helper 统一继承这些默认项，不再新增 factory、直接初始化路径或路径级 `CURLOPT_NOSIGNAL` 设置。

## owner thread 与网络驱动

`QCCurlMultiManager` 是 thread-local multi owner。异步 easy handle、reply、scheduler 和 upload device 只能在所属 owner thread 驱动；Blocking Extras 的局部 easy handle 按同步执行器合同在调用线程使用，不借用 live manager。

明确允许跨线程的异步入口通过 Qt queued 调用回到 owner thread；owner-thread-only 命令按公开合同拒绝错误线程，不透明投递或阻塞等待。Core async 与 WebSocket 仅使用 Qt event loop 和 libcurl multi socket/timer 推进，Blocking Extras 保留原生同步执行器；不新增 Qt Network 第二执行路线，也不用 busy wait、sleep、手写 poll 或 signal handler 修补 resolver timeout、SIGPIPE 或网络时序。

### multi transfer ownership

`curl_multi_add_handle()` 成功后，multi transfer record 独占 easy handle、callback userdata 和 transfer backing storage，直到 `curl_multi_remove_handle()` 完成。`QCNetworkReply` 仅观察传输；析构时清空 observer 并请求 owner thread 延迟 detach，不提前销毁仍注册的 handle 或 callback target。

WebSocket 握手复用同一 multi driver，不回退到 owner-thread `curl_easy_perform()` 或新增 worker transport。CONNECT_ONLY easy handle 在 `curl_easy_send()` / `curl_easy_recv()` 使用期间持续留在 multi 中。

### package-internal component bridge

独立编译组件只通过以下非安装 Core bridge 链接：

| 组件 | bridge | 跨库数据 |
| --- | --- | --- |
| Other Extras | `registerPersistentTransfer`、`removePersistentTransfer` | opaque transfer token |
| Test Support | `installNetworkMockProvider` | 进程期 callback table |

bridge 声明留在 `_p.h`，不进入 public install manifest。transfer record 类型不得出现在跨库导出签名或 Core 动态符号中；组件与 Core 必须来自同一 package source identity，这种锁步内部合同不是 public source API 或稳定 ABI。Blocking Extras 与 Core 同库编译，handle/protocol helper 保持隐藏内部符号。

动态符号检查只接受 `scripts/qcurl_abi_symbols.py` 逐项列出的 owner，禁止用通配 owner 掩盖新增导出。`QCCurlMultiTransferRecord`、`QCCurlHandleManager`、reply-private 和 jitter helper 等实现符号不能因存在 bridge 而被放行。

### Core HTTP protocol boundary

Core 请求入口在创建 easy handle 前只接受 `http` / `https`。异步与 Blocking Extras 均显式设置初始请求及重定向协议白名单为 `http,https`；调用方只能收窄，不能恢复 libcurl 的全部协议默认值。

## share handle 合同

只有配置完整的 lock / unlock callback 及 userdata 后，libcurl share interface 才能共享数据。可共享范围由当前 `ShareHandleConfig` 明确启用，例如 DNS、Cookie、SSL session；新增类型前须证明锁与回调生命周期覆盖完整 transfer，并同步测试及本合同。

## borrowed lifetime 合同

借给 libcurl 的 `curl_slist`、header/body buffer、upload device、callback userdata 和 TLS/proxy/URL 字符串缓冲必须覆盖相关 transfer 生命周期。

`QCCurlHandleManager` 负责 easy handle 和内部 header list 的释放。其他外部借用对象由对应 owner 持有，不能依赖临时对象、悬挂 lambda 引用或跨线程裸指针。进入 multi 后的释放时机遵循上面的 transfer ownership。

## diagnostics 与 public accessor 边界

public accessor 保留调用方主动读取 raw header、raw body 摘要和 error string 的能力，不因脱敏而删除或弱化。自动 diagnostics、logger、持久化证据和下游 facade 则统一使用脱敏逻辑，仅输出 redacted summary，不写出 Authorization、Cookie、token、signed ticket、signed URL query value 或 verbose trace 中的原始敏感数据。

URL 脱敏分两种情况：

- 命中 AWS S3 / CloudFront、Google Cloud Storage、Azure SAS 或通用 signature marker：脱敏全部 query value，保留 scheme、host、path 和 query key，不将整个 URL 隐藏到不可诊断。
- 未命中签名 marker：只按 sensitive-key 规则脱敏 token、cookie、secret、password、api key 等参数，保留非敏感 query value 的诊断价值。

## 发布边界与验证入口

2.x Core public source API 的破坏需要新的 major release；ABI 变化仍须保持下游每次更新重编译的合同，并同步相关发布说明和测试。2.0 的 `abiMode=none`、非稳定 ABI 及未来 baseline/diff/promotion 的适用范围统一见[正式发布合同](../release/2.0.0-hard-break-release-contract.md)，本页不另维护发布矩阵。历史对比只从[归档索引](../archive/README.md)追溯。

评审按上面的职责检查受影响路径，再选择能验证本次变更的入口，不重复抄写规则清单：

| 入口 | 验证用途 |
| --- | --- |
| `qcurl_libcurl_binding_contract_guard` | 静态拒绝新增 direct `curl_easy_init()` 和路径级 `CURLOPT_NOSIGNAL`；不证明运行期所有权 |
| 相关 QtTest，包括 logger redaction 用例 | 验证借用寿命、owner thread、协议、detach 和脱敏等实际行为 |
| public-api / package consumer 与动态符号检查 | 验证安装面、组件 bridge 及意外实现导出；通过不代表二进制兼容 |

命令与失败解释见[构建与测试](../build-and-test.md)，候选资格仍按[发布操作](../release/release-procedure.md)执行。
