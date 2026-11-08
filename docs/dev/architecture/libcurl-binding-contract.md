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

HTTP admission 显式接收 observer、manager、现有 transfer state 和 easy handle 的所有权，返回 accepted/rejected/quarantined 结果、opaque `TransferToken`、warnings 及可安全归还的 handle。multi manager 不读写 `QCNetworkReplyPrivate`，不从 observer 的 parent 推导 manager，也不复制 state；reply 与 record 共享同一个 `QCNetworkReplyTransferState`，record 从中读取 reply setup 写入的 callback 配置标志。

`curl_multi_add_handle()` 成功后，multi transfer record 独占 easy handle 和 callback userdata，并持有共享 backing state。reply 只保存 token；`activeEasyHandle` 是 add commit 后才发布的非 owning 观察值，仅能在 owner thread／libcurl callback 合同允许的时段使用。cancel、abort、析构仅请求 `removeTransfer(token)`，跨线程或 callback 内延迟 detach，不提前清零 token 或释放 callback target。

安全 detach 按 `curl_multi_remove_handle()`、清除 active handle 观察值、解除 share 与 record callbacks/userdata、移交 handle、释放 record 的顺序执行。detach 同时关闭 progress/verbose，避免 cleanup 调用旧回调或退回默认输出；后续 admission 重新启用 progress，verbose 由每次 reply setup 按 trace 配置启用。reply-side completion adapter 经同线程 manager 作为投递上下文，以 `Qt::AutoConnection` 回到 owner thread；只在目标线程检查 `QPointer`，先回收 detached handle、清零 token，再处理终态或 retry。reply 已销毁时只释放安全 detached handle，不再访问 QObject。ownership 无法证明时撤销 active handle 观察并 quarantine/poison，不归还或 cleanup 原 handle，也不清零活动 token。

admission warnings 由 reply 在 owner thread 消费；安全失败固定先恢复 handle、再写 warnings、最后 queued `abortWithError()`。连接池统计由 reply-side 在首次 admission 成功后开始，retry 不重复开始，终态或析构继续通过 `finishPoolRequest()` 完成一次逻辑请求统计。

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

Core 请求入口在创建 easy handle 前只接受 `http` / `https`。异步与 Blocking Extras 均显式设置初始请求白名单为 `http,https`，开启重定向时才应用重定向白名单；调用方只能收窄，不能恢复 libcurl 的全部协议默认值。

## 共同请求策略

隐藏的 `compileRequestPolicy` 从准备后的请求快照编译纯值结果，集中协议、重定向、raw header 覆盖、认证、proxy、range、限速、endpoint verify/CA、connect/total/receive-idle timeout 和 HTTP version intent。它不保存 request/body、CurlPlan、执行对象或 CURLOPT 指令序列，不读取环境/probe、不调用 native/QObject、不发布诊断。只有全部校验通过才给出可应用结果。

先确定字段是否生效，再按 URL/协议 → 生效代理 → connect timeout → total timeout → receive-idle timeout → 其余生效共同数值选择首错。连接/总时限的零值不覆盖原生默认；receive-idle 的零值禁用，正值上限为 2,147,483,647ms。负值和超出适用 `long`/`curl_off_t` 或 receive-idle 范围的数值拒绝，不截断。raw header 含空值仍优先；None、关闭重定向及被覆盖的配置不新增拒绝。HTTP 版本由 `decideHttpVersionPolicy` 接收 adapter 在原检查点采样的窄能力事实，隐式版本不设置选项，Http3/Http3Only 保持既有降级与错误分类。

两个 adapter 只把共同决策绑定到各自原生插点。所有生效的必需共同选项失败即终止，默认直连、显式代理 `NOPROXY=""`、协议/验证及原 optional 的 redirect、Referer、Accept-Encoding、限速、UNRESTRICTED_AUTH 均遵循该规则。无代理/None 使用 `PROXY=""` 禁用环境代理；显式 HTTP/HTTPS 代理凭据使用原生 ANY 认证协商，HTTPS 类型直接使用最低依赖支持的 enum，不进行宏探测或明文回退。

Core 纯值及配置拒绝为 InvalidRequest；Blocking 在共同值校验后拒绝生效正 receive-idle，保留协议能力/HTTP3 预检查的 UnsupportedCapability，其余配置错误仍为 InvalidRequest。新失败只描述类别、选项和原生静态错误，不输出输入值。敏感集合只控制 libcurl UNRESTRICTED_AUTH，不提供通用自定义敏感头过滤器，具体用户义务见[配置合同](../../user/configuration.md)。

## share handle 合同

只有配置完整的 lock / unlock callback 及 userdata 后，libcurl share interface 才能共享数据。
`ShareHandleConfig` 仅共享 manager 的 Cookie/SSL session store，不共享 DNS 或连接；
后两者由每个传输域的 CURLM 持有。Isolated 按完整有效 RESOLVE 规则分域，NativeShared
固定于同线程共享域，Blocking 每次独立 easy。各 CURLM 分别应用原生连接限额。

share 全关后新传输立即不绑定，非空在途变更沿用 applied 并诊断；安全空闲时只应用最新
pending，改回 applied 撤回 pending。active users 和临时 Cookie 操作都必须退出后才可配置
或清理 CURLSH。detach 失败保留原生 ownership，不把未解绑的 easy 交给普通析构路径。

`QCCurlShareCookieStore` 是完整的 share/Cookie 行为边界：持有逻辑 scope、惰性创建的
context、HTTP bindings、临时 operation、applied/pending、初始化失败及 store poison。
multi 只保留一个 shared holder，负责 owner-thread 投递、graph mutex、CURLM、token 和
结果发布；module 不反向调用 multi，也不发布日志。manager 地址仅用作 live identity；
scope 关闭先移出 live 索引，旧 binding 继续保留原 context，地址复用得到独立 store。

HTTP 原生 SHARE 解绑成功才提交 binding/users 退出；之后 pending 的安全初始化失败只
产生诊断，不撤销已经安全的 detach。临时 Cookie 命令的退出固定为 SHARE 解绑成功、
easy cleanup、operation 退出、pending/retired 推进。事务成功与退出 ownership 结果
分别返回；退出失败不得发布成功。解绑或 cleanup 无法证明安全时，module 停止所有
native 调用，保留 easy/context/userdata，由 multi 的现有 quarantine 整图持有同一实例。

runtime shutdown 先关闭事件入口，再使 module 进入 Draining；在途 easy 全部安全脱离和
销毁后统一关闭 share scopes，最后清理 domains。Draining 的最后用户退出不应用 pending。
任一层 ownership 失败立即停止后续 native cleanup，module 析构不代替显式 shutdown。

## borrowed lifetime 合同

借给 libcurl 的 `curl_slist`、header/body buffer、upload device、callback userdata 和 TLS/proxy/URL 字符串缓冲必须覆盖相关 transfer 生命周期。

`QCCurlHandleManager` 负责 easy handle 和内部 header list 的释放；HTTP handle 还保留同一 transfer state，使 POSTFIELDS/RESOLVE 等借用数据在 detach 后随 handle 移动，覆盖 completion 排队、事件丢弃和 reply 先销毁的路径。释放顺序必须是 easy cleanup、header list、共享 state，不能因 record 已销毁就提前释放 backing storage。其他外部借用对象由对应 owner 持有，不能依赖临时对象、悬挂 lambda 引用或跨线程裸指针。进入 multi 后的释放时机遵循上面的 transfer ownership。

Blocking 的共同策略结果和字符串/list/callback backing 保存在调用线程的局部 context；context 先于 easy 守卫构造，后于 easy cleanup 析构，配置失败路径也遵循这一顺序。没有新增共享 holder 或第二执行器。

## Reply 生命周期与响应

`QCReplyLifecycle` 与 `QCReplyResponse` 按值保存在同一个共享 transfer state 中。
生命周期集中 phase、错误、实际原生诊断、公开进度、warnings 和首次 Running 起算的
请求时钟；显式 retry 只清 attempt 观察，终态后的迟到 completion、回放及普通转换无操作。
响应集中最终头块、权威 status、消费数据、已交付事实与有界缓存候选。消费不会截断候选，
候选超限立即释放；completion 的 status=0 不被代理 CONNECT 头覆盖。

Reply 编排终态前的缓存恢复、提交、trace/domain/pool 收尾、flow 退出及安全信号发布。
304 恢复可同步触发 readyRead；发布后必须重查终态，不能覆盖槽中已完成的取消。网络、
cache、mock/chaos、retry 与 public accessor 使用同一领域接口；module 不反向访问 Private
或共享 holder，也不发布 Qt 信号。原生 callback 和跨信号检查仍由现有 adapter 承接。

## 接收空闲计时

`QCNetworkReceiveIdleTimeout` 只接受单调时间和连接、字节到达、接收暂停等事实，集中
期限、generation 与到期判定。`QCNetworkReceiveIdleExecution` 在同一 transfer state 中
连接 Qt timer 与原生阶段采样；timer parent 为 reply，record 仅借用同一 backing。
PREREQ 确认当前连接/TLS 已完成，SOCKOPT 标记重连，header/body 续期；progress 与 timer
只采样本请求，socket action 不为此能力遍历全体 active transfers。禁用时不建立该 timer
或周期唤醒。跨 callback 的完成仍经 queued owner-thread adapter 与安全 detach，重查
reply、token 和 generation，旧 attempt 不得终结新尝试。

只有真实 `CURLMSG_DONE` 可记录原生完成码；主动 detach 的合成返回值只作内部控制。
高层 timeout 分类、最终 receive-idle 来源和实际原生码分别保存。mock 没有原生完成码；
已有原生失败不得被 receive-idle 替换为 28。retry 清除 attempt 来源，成功 cache 回退清除
失败来源，两项公开诊断在终态信号前冻结。

## Reply 请求体与流控

`QCReplyBodySource` 借用调用方设备，集中长度、EOF/Wait、读错、定位与重放规则；
设备探测前由 adapter 验证 owner/device thread，readyRead 连接以 Reply 为 context。
`QCReplyFlowControl` 独占 user/internal/applied pause 原因、背压阈值/峰值与上传暂停事实，
每次只借用有效 easy，原生成功才提交，失败回滚，不把用户原因与内部原因互相覆盖。

write callback 在 readyRead 前更新峰值，发布后重读消费量决定背压；readAll 后恢复排到
下一轮，以免回调内解除接收暂停。curl_easy_pause 可同步重入 callback，adapter 跨原生
调用保留同一 holder，flow 返回后保留重入提交的最新原因，随后按存活/终态重查发布与
multi wake。终态只清内部原因，不调用已 detach 的 easy；mock/chaos 仅使用逻辑回放操作。

## Reply 请求级重试

`QCReplyRetry` 独占资格、次数与累计等待预算，复用公共 policy 和 jitter；它只接收方法、
幂等 key、来源可重放、输出交付/恢复能力、elapsed 与 Retry-After 等窄事实，不执行
输出 I/O、Qt 信号或 timer。预算评估不提交状态，使用减法边界拒绝溢出，累计等待饱和。

adapter 在原检查点解析 429 的最终头并采样查询时钟，允许提案才调用独占 writer 恢复；
恢复失败保留实际原生诊断并提交 OutputDeviceError，不计数、不等待、不发下一请求。
恢复 callback 和 retryAttempt 后重查存活与终态，成功恢复后才提交次数及等待。
timer 到期才 reset/execute；跨 attempt 的时钟、warnings 和累计预算保留，上传来源
回卷仍在下一网络 prepare 发生。已向通用消费者交付响应且无恢复合同则禁止重试。

## Reply 请求级缓存

`QCReplyCache` 持有 key、stale entry、重验证与一次回退资格；manager 构造前调用
prepareRequest，整体移交有效请求及上下文，不重新解释 key/validator。module 复用
cache integration 合同，集中五类执行策略、认证分区、no-store、候选容量、partial
response 禁止回退、304 raw headers 合并与 HEAD/GET 存储规则。

module 不保存 manager/cache 指针，每个原执行和终态检查点借用当前服务，保留替换、
注销和 QPointer 销毁语义。响应应用、readyRead→Finished、OnlyCache miss 排队以及
304 恢复后的终态重查由 adapter 负责；缓存命中包括空 body，Age 和敏感头过滤不变。
response 只接受回放数据与上限，不判断缓存资格；流式消费与完整候选依然独立。

## Reply 非 TLS 配置与请求编排

`QCReplyCurlConfiguration` 按值保存在同一 transfer state，拥有不可变 CurlPlan、共同策略结果与非 TLS
字节、resolve/connect-to 列表及 Cookie/HSTS/Alt-Svc 配置。capture、digest 和 admission
读取同一只读计划；没有可写 backing getter，也不接收 Private、holder、设备或服务对象。
列表先构建 candidate，required setopt 成功后才接管；easy cleanup 早于配置存储释放。
HTTP header list 继续由 `QCCurlHandleManager` 唯一持有，不新增第二份 body/method 存储。

共同纯值校验在共同 setopt 之前执行；配置按实际插点分段：URL → PRIVATE → network → 设备准备 → method → 设备 READ → body
size/headers/limits → auth/proxy → proxy TLS → HTTP version → endpoint TLS → timeouts →
response/seek/progress callbacks。debug 仍在网络 prepare 注册；callback、Qt 信号、timer、
multi 以及跨 module 的存活/终态检查由 adapter 负责，TLS/share store 不退回 Reply 内部。

每段返回保序的普通日志与 capability warnings，再返回首次 fatal；publisher 不分组、
去重或重解释策略，普通日志不进入公开 warnings。mock 调用同一 encoding 冲突规则，
保留 setup 与 dispatch 各一次的发布。协议白名单始终 required，失败不启动网络请求。

manager 的 Cookie 配置仅在没有已继承文件时按原检查点传值；HSTS/Alt-Svc 在每次网络
prepare 借用当前配置，module 不保留 manager。CookieJAR FLUSH 在成功终态的 stateChanged
之后、cache store/finished 之前执行，失败仍只产生 warning；share adapter 只查询是否
同时启用文件，事务、ownership 与 store poison 仍归 `QCCurlShareCookieStore`。

`QCNetworkReplyPrivate` 仅持有请求快照、同一 holder、easy/token/removal、一次逻辑 pool
统计、请求级 retry/cache 与独占 writer 恢复 callback，协调以上跨 module 顺序。
生命周期、响应、设备、pause、资格/预算、缓存策略及非 TLS 选项规则不再摊平为 aliases。

## TLS 配置与诊断

`QCTlsConfiguration` 封装 endpoint 与 HTTPS proxy 的 TLS 配置解释、选项顺序、Fail/Warn
策略和全部 TLS 字节存储。它按值保存在既有 `QCNetworkReplyTransferState` 中，不依赖
Reply、整个 Request 或反向回调；规则测试通过同一行为接口使用真实 easy handle。
生产调用方传入共同 endpoint verify/CA 决策及高级 endpoint config，或 requested/effective proxy type 与可选 proxy TLS config。空 CA 不探测硬编码路径，也不覆盖 backend 默认。Blocking 仅消费共同 endpoint 子集，未补齐高级 endpoint/proxy TLS 能力。

proxy TLS 在代理 endpoint/credentials 之后、HTTP version 之前应用；endpoint TLS 保持原
配置位置。endpoint verify、CA 和客户端凭据是 required；proxy TLS 及 endpoint 的 pin、
minimum version、cipher 保持各自的 security policy。只有 `CURLE_UNKNOWN_OPTION` 和
`CURLE_NOT_BUILT_IN` 可在 Warn 下继续，其它失败始终终止。HTTPS proxy 使用受支持
libcurl 的原生类型，不回落明文代理。

module 只返回有序 warnings 与首次 fatal；Reply 的 `publishReplyTlsConfigurationResult` 在原
调用点先发布 warnings，再设置一次错误，不重解释策略、不清空既有诊断。诊断只包含
选项名、错误类别与原生静态描述，不回显路径、密码、pin 或 cipher 输入。TLS 对象不
复制或移动；active record、detached handle 与 queued/dropped completion 沿用同一
backing 的持有链，easy cleanup 早于 TLS 存储释放。配置仅发生在原有安全 setup 时点。

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
