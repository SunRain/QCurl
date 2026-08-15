# libcurl binding contract

本文档定义 QCurl 生产代码接入 libcurl 的长期约束。后续新增或修改 libcurl 路径时，必须按本合同审查；`.helloagents` 方案包只保存本轮执行证据，不替代本文件。

## 适用范围

本合同适用于 `src/` 下的 QCurl 生产代码，包括 Core async、Blocking Extras、WebSocket、cookie bridge、share handle 和 diagnostics 路径。

`tests/libcurl_consistency/` 中的 baseline client 是上游 libcurl 行为参照，允许直接使用 libcurl API；它不代表 QCurl 生产 binding 规则。

## easy handle 创建合同

`QCCurlHandleManager` 是 QCurl 生产代码唯一的 `CURL *` easy handle 创建入口。

创建顺序固定为：

1. 通过 QCurl runtime lease 确认进程级 libcurl 运行时处于 `Running`
2. `curl_easy_init()`
3. common easy defaults

当前 common easy defaults 固定包含 `CURLOPT_NOSIGNAL=1L`。Core async、Blocking Extras、WebSocket、cookie helper 均继承该默认项，不在各自路径重复维护 `CURLOPT_NOSIGNAL`。

`qcurl_libcurl_binding_contract_guard` 负责静态扫描 `src/`，阻止新增 direct `curl_easy_init()` 和路径级 `CURLOPT_NOSIGNAL` 设置。

## owner thread 合同

`QCCurlMultiManager` 是 thread-local multi owner。每个 easy handle 只在所属 `QCCurlMultiManager` 的 owner thread 中驱动。

跨线程入口必须通过 Qt queued 调用回到 owner thread。非 owner thread 不得直接驱动 reply、multi、scheduler、easy handle 或 upload device。

网络推进只允许 Qt event loop 与 libcurl multi socket/timer 集成。禁止通过 busy wait、sleep、手写 poll 或 signal handler 修补网络时序。

### multi transfer ownership

成功调用 `curl_multi_add_handle()` 后，`QCCurlMultiManager` 的 transfer record 独占 easy handle、callback userdata
和 transfer backing storage，直到 `curl_multi_remove_handle()` 完成。`QCNetworkReply` 只保留观察关系；reply 析构只
清空 observer 并请求 owner thread 延迟 detach，不得提前销毁仍注册在 multi 中的 handle 或 callback target。

WebSocket 握手复用同一 multi driver，不得回退到 owner-thread `curl_easy_perform()` 或新增 worker transport。
Core 与 Other Extras 之间只通过 opaque transfer token 传递 persistent transfer 身份；非安装的 transfer record 类型不得进入跨库导出签名或 Core 动态符号。CONNECT_ONLY easy handle 在 `curl_easy_send()` / `curl_easy_recv()` 使用期间持续留在 multi 中。

### Core HTTP protocol boundary

Core request entry points 在创建 easy handle 前只接受 `http` / `https`。异步和 Blocking Extras 都必须显式设置初始
请求与重定向协议白名单为 `http,https`；调用方配置只能收窄该集合，不能恢复 libcurl 的全部协议默认值。

## share handle 合同

libcurl share interface 只有在 lock / unlock callback 和 userdata 已配置时才能共享数据。

可共享范围固定为 DNS、Cookie、SSL session 等由当前 `ShareHandleConfig` 明确启用的数据。新增共享类型时必须先证明 lock / unlock 生命周期覆盖完整 transfer，再更新测试和本文件。

## borrowed lifetime 合同

传给 libcurl 的借用数据必须活到相关 transfer 结束。

必须覆盖的对象包括：

- `curl_slist`
- header buffer
- body buffer
- upload device
- callback userdata
- TLS / proxy / URL 字符串缓冲

`QCCurlHandleManager` 负责 easy handle 与内部 header list 的释放。其他外部借用对象必须由对应 owner 持有，不能依赖临时对象、lambda 捕获悬挂引用或跨线程裸指针。

## diagnostics 与 public accessor 边界

public accessor 保留调用方主动读取 raw header、raw body 摘要和 error string 的能力。

自动 diagnostics、logger、持久化证据和下游 facade 只能输出 redacted summary。它们不得写出 Authorization、Cookie、token、signed ticket、signed URL query value 或 libcurl verbose trace 中的 raw sensitive data。

URL redaction 固定为两层规则：

1. 命中 signed URL marker 后，保留 scheme、host、path 和 query key，脱敏全部 query value。
2. 未命中 signed URL marker 时，只按 sensitive-key 规则脱敏 token、cookie、secret、password、api key 等参数。

signed URL marker 覆盖 AWS S3 / CloudFront、Google Cloud Storage、Azure SAS 和通用 signature 标记。

## 禁止事项

- 不新增 easy handle factory。
- 不在 `QCWebSocket`、Blocking Extras、Core async 或 cookie helper 中直接调用 `curl_easy_init()`。
- 不在路径级重复设置 `CURLOPT_NOSIGNAL`。
- 不新增 Qt Network 第二执行路线。
- 不用 busy wait、sleep、手写 poll 或 signal handler 修补 resolver timeout / SIGPIPE 行为。
- 不删除或弱化 public raw accessor。
- 不把 signed URL 整段隐藏到不可诊断；必须保留 scheme、host、path 和 query key。
- 默认不在本合同范围内改变 public API / ABI。若 release 前确需改变公开面，必须同步更新当前 fresh baseline、release contract、用户文档和测试证据。

## ABI 证据合同

`QCurl 2.0.0` 是相对于已发布 v1.0.0 的 hard-break 候选。Reviewer 必须确认：

1. candidate 阶段使用 `abi/baseline/qcurl-core-v1.abi.xml` 生成 v1-to-v2 hard-break report。
2. 当前 v2 baseline 由当前 `libQCurl.so.2.0.0` 通过受控 promotion 生成，不能复用旧草稿产物。
3. final 阶段只对 `abi/baseline/qcurl-core-v2.abi.xml` 执行 clean diff。
4. `qcurl_abi_gate.py` 在生成诊断 snapshot/candidate 和正式 diff 前检查动态符号，拒绝包含
   `QCCurlMultiTransferRecord`、`QCCurlHandleManager`、reply-private 或 jitter helper 的产物；
   诊断命令不得写入 `abi/baseline/`，只能由经过 machine manifest 身份重放的显式 promotion
   更新 baseline，不得先刷新 baseline 再掩盖私有类型泄漏。

历史 libcurl binding ABI 对比材料归档在 `docs/internal/archived-release/libcurl-binding-abi-comparison-evidence.md`。

## Review checklist

新增或修改 libcurl binding 路径时，reviewer 必须逐项确认：

- easy handle 来自 `QCCurlHandleManager`。
- 成功进入 multi 后，transfer record 持有 easy handle 和 callback userdata 直到 detach 完成；reply 只作为 observer。
- Core / Other Extras 桥接只传 opaque token，Core 动态符号不包含 transfer record 类型。
- CONNECT_ONLY easy handle 在 WebSocket 收发期间保持注册在 multi 中。
- Core 和 Blocking 请求入口只允许 HTTP/HTTPS，并同时设置初始与重定向协议白名单。
- easy handle 创建前已经取得有效的 QCurl runtime lease。
- easy handle 创建后统一继承 `CURLOPT_NOSIGNAL=1L`。
- 代码没有新增 direct `curl_easy_init()`。
- 代码没有新增路径级 `CURLOPT_NOSIGNAL` 设置。
- reply、scheduler、multi、easy handle 和 upload device 仍在 owner thread 驱动。
- 跨线程入口通过 queued 调用回到 owner thread。
- share handle 只在 lock / unlock callback 已配置时共享数据。
- `curl_slist`、buffer、device 和 callback userdata 生命周期覆盖 transfer。
- diagnostics、logger、持久化证据和下游 facade 使用统一 redaction helper。
- signed URL marker 命中后全部 query value 被脱敏。
- 普通非签名 URL 的非敏感 query value 保持可诊断。
- 修改未引入 busy wait、sleep、手写 poll 或 signal handler。
- 修改未引入 public API / ABI 变化；若 release 前确需改变公开面，相关 baseline、合同、用户文档和测试证据已同步更新。
- `qcurl_libcurl_binding_contract_guard`、logger redaction 用例和相关 targeted Qt Test 通过。
