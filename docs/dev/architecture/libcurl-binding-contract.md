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

bridge 声明留在 `_p.h`，不进入 public install manifest。transfer record 类型不得出现在跨库导出签名或 Core 动态符号中；完整包组件与 Core 来自同一 source identity，bridge 不是 public source API。但旧 consumer 已静态链接的 TestSupport 代码依赖新 Core 时，仍须满足同线 patch 运行合同。Blocking Extras 与 Core 同库编译，handle/protocol helper 保持隐藏内部符号。

动态符号检查只接受 `scripts/qcurl_abi_symbols.py` 逐项列出的 owner，禁止用通配 owner 掩盖新增导出。`QCCurlMultiTransferRecord`、`QCCurlHandleManager`、reply-private 和 jitter helper 等实现符号不能因存在 bridge 而被放行。

### Core HTTP protocol boundary

Core 请求入口在创建 easy handle 前只接受 `http` / `https`。异步与 Blocking Extras 均显式设置初始请求及重定向协议白名单为 `http,https`；调用方只能收窄，不能恢复 libcurl 的全部协议默认值。

## share handle 合同

只有配置完整的 lock / unlock callback 及 userdata 后，libcurl share interface 才能共享数据。
`ShareHandleConfig` 仅共享 manager 的 Cookie/SSL session store，不共享 DNS 或连接；
后两者由每个传输域的 CURLM 持有。Isolated 按完整有效 RESOLVE 规则分域，NativeShared
固定于同线程共享域，Blocking 每次独立 easy。各 CURLM 分别应用原生连接限额。

share 全关后新传输立即不绑定，非空在途变更沿用 applied 并诊断；安全空闲时只应用最新
pending，改回 applied 撤回 pending。active users 和临时 Cookie 操作都必须退出后才可配置
或清理 CURLSH。detach 失败保留原生 ownership，不把未解绑的 easy 交给普通析构路径。

## borrowed lifetime 合同

借给 libcurl 的 `curl_slist`、header/body buffer、upload device、callback userdata 和 TLS/proxy/URL 字符串缓冲必须覆盖相关 transfer 生命周期。

`QCCurlHandleManager` 负责 easy handle 和内部 header list 的释放。其他外部借用对象由对应 owner 持有，不能依赖临时对象、悬挂 lambda 引用或跨线程裸指针。进入 multi 后的释放时机遵循上面的 transfer ownership。

## diagnostics 与 public accessor 边界

public accessor 保留调用方主动读取 raw header、raw body 摘要和 error string 的能力，不因脱敏而删除或弱化。自动 diagnostics、logger 和内建 middleware 使用同一脱敏边界；默认不写出 query、Authorization、Cookie、signed ticket 或 verbose trace 中的原始敏感数据。宿主持久化证据与自定义 facade 应遵循各自的敏感数据治理，不属于内建日志自动保证。

请求级 `QCNetworkDiagnosticQueryPolicy` 仅提供 `Redacted`（默认）与 `Full`。Redacted
隐藏整个 query（包括无等号的不透明授权、普通参数名和值），显示为 `?[REDACTED]`，不
按参数名黑名单猜测安全性。显式 Full 仅允许调用方初始 URL 的 query 在 request、
middleware、开始日志与每个 QCurl 网络 attempt 的首个可识别 HEADER_OUT 目标记录可见。
成功/错误 middleware 的初始 URL 字段跟随策略；错误自由文本不随 Full 放开。

两档都移除 URL userinfo/fragment，隐藏敏感头、TEXT/HEADER_IN、Location、redirect
完成日志及后续目标 query。fragment-only 的默认日志移除 fragment，是对原输出的安全
收敛。trace 按逻辑行跨 callback 处理，不把分块边界当行边界；每行最多缓存 4,096
原始字节（含 CR，不含结束 LF），全部 caller/internal 通道合计最多 12,288 原始字节。
超限或无法可靠判界时丢弃受影响的原始行/续段，结束时不输出未完成尾段。CONNECT/407
不消费初始目标机会，origin 认证重发和重定向不重新开放，只有新的 QCurl attempt 复位。
最终 message 总长不超过 4,096 个 QString UTF-16 单元，每行类型、ID 与安全标记全部
计入；先脱敏再限长，不能截出半个 ID 或固定安全标记。

两条生产 debug callback 根据现有 owner 的主 easy handle 区分 caller 与 internal，
不覆盖 `CURLOPT_PRIVATE`。callback 起始的单层 `[transfer-connection] ` 装饰仅接受非负
`curl_off_t` 十进制值或小写 `x`，并与实际 callback handle 的 `CURLINFO_XFER_ID`、
`CURLINFO_CONN_ID` 成功结果逐字段核对，`x` 仅对应 `-1`。合法 ID 在正文分类前分离，
安全行、脱敏头、凭据摘要与固定标记保留各自可靠 ID；一次 callback 多行时逐行标注。
跨 callback 的行只使用首片段可靠 ID，后续片段冲突则丢弃该关联及正文。无装饰不补造
ID；未知来源、非法/嵌套装饰或 getter 失败/不匹配只输出安全标记，不显示未确认 ID。
transfer ID 仅在相同 connection cache/multi 内用于关联，connection ID 可复用，不是
全局请求主键。DATA/SSL_DATA 仅输出去装饰后的 callback payload 长度，不回显正文。

caller/internal 分别保存固定的三通道状态，原始缓存预算共同计算。只有确认的 caller
初始目标可获得 Full；internal 目标始终 Redacted，不能消费 caller 资格或解除其凭据
保护。internal handle 切换后，该内部槽在本 attempt 持续失步，仅输出安全标记，可附
本 callback 的可靠 ID；LF、空行、非 TEXT 或切回旧 handle 均不解除。未知事件不能作为
恢复边界；未知 HEADER_OUT 无法排除初始目标已丢失时关闭 Full 资格。正常 caller 与
未失步内部记录仍输出安全诊断。疑似上游截断（包括结尾 `...\n`）只保留此前完整安全行，
末行及不确定续段受保护，补出的 LF 不能解除；TEXT 只在同来源可信协议边界后恢复。

TEXT 中的 URL 按完整记录处理，不能把 URL 内的合法单引号当成结束边界。libcurl
重定向记录先提取完整 URL，再统一隐藏 query 并移除 userinfo/fragment。未知包装、畸形
URL 或额外尾段无法可靠判界时整条显示固定安全标记，不拼回原文；普通非 URL 文本中的
`#` 保持可见，不因此关闭全部 TEXT。

Cookie 添加/替换与 Server/Proxy auth 的 TEXT 不是 HTTP 头，仍必须隐藏 Cookie 凭据
和认证用户名；只保留安全操作/认证方式摘要。疑似凭据记录解析失败时只输出固定标记，
不能把原片段作为普通 TEXT 回退。用户名可含 LF 和引号，凭据记录后的无缩进尾行也
必须保护；不能凭行尾或引号猜测记录结束。识别出的凭据或超限 TEXT 启用凭据续段保护，
直到真实非 TEXT 协议事件且原始尾段已完整消费才解除；跨类型的未完成尾段不丢失状态。
这段区间内正常 TEXT 也可能统一显示固定安全标记，仍可输出统一分类器生成的安全凭据
摘要；边界后普通无凭据 TEXT 恢复可见。
这是有限的诊断可见性取舍，不关闭 trace、Cookie engine 或认证，也不进行任意业务文本
的通用秘密扫描。敏感头名已出现却缺合法分隔符的记录同样 fail-closed。

trace 初始目标以 libcurl 默认路径规范化后的诊断基准匹配，覆盖 `/./`、`/../` 和代理
absolute-form 默认端口。匹配仍严格核对编码 query（包括 `%2B`）；不规范化后续目标来
忽略差异。基准构造失败时显式标记并保持 Redacted，不能给后来的目标重新发放资格。
这不设置 `CURLOPT_PATH_AS_IS`，普通 URL 字段仍保持原 request 的编码路径，trace 则显示
实际发送目标。旧状态清理、mock/chaos reset、终态与解绑只停止 trace，只有真实网络执行
入口开启新 attempt；策略快照与流状态由内部行为类封装，不另建 callback owner。

诊断 URL 使用 `FullyEncoded`；实际请求、cache key、request equality、CurlPlan digest
和 raw accessor 不变。trace 开关、logger 目标与 request query 策略各自独立。不同协议的
目标记录必须有真实 callback 证据，不能用 formatter 单元测试宣称 HTTP/3 已通过。宿主／
自定义 middleware 主动打印原始数据不属于内建日志保证。

## 发布边界与验证入口

从 2.1 起，全部已发布 Public 组件（含 Preview）在同 `major.minor` 后续 patch 内保护源码、受支持 shared ABI、安装消费与公开行为。确需破坏时进入新版本线，并同步迁移说明和测试；跨 minor 不保证兼容。规范范围见[版本与兼容政策](../release/versioning-and-compatibility.md)，必需参考/报告见[正式发布合同](../release/2.1.1-release-contract.md)，本页不另维护发布矩阵。历史对比只从[归档索引](../archive/README.md)追溯。

评审按上面的职责检查受影响路径，再选择能验证本次变更的入口，不重复抄写规则清单：

| 入口 | 验证用途 |
| --- | --- |
| `qcurl_libcurl_binding_contract_guard` | 静态拒绝新增 direct `curl_easy_init()` 和路径级 `CURLOPT_NOSIGNAL`；不证明运行期所有权 |
| 相关 QtTest，包括 logger redaction 用例 | 验证借用寿命、owner thread、协议、detach 和脱敏等实际行为 |
| public-api / package consumer 与动态符号检查 | 验证安装面、组件 bridge 及意外实现导出；通过不代表二进制兼容 |

命令与失败解释见[构建与测试](../build-and-test.md)，候选资格仍按[发布操作](../release/release-procedure.md)执行。
