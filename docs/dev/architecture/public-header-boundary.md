# 公共头、安装与导出边界

本文面向维护者，说明 public install surface 的来源、禁止泄漏项、隔离 consumer 与合同检查。具体 API 行为由公开头注释和用户正文定义，通用布局由[PIMPL 规范](../pimpl-and-shared-data-style.md)定义；不再维护逐 API 的布局或合同镜像。

## 1. 唯一清单与组件归属

- `src/CMakeLists.txt` 的 QCURL_INSTALL_HEADERS、QCURL_INSTALL_HEADERS_BLOCKING_EXTRAS、QCURL_INSTALL_HEADERS_OTHER_EXTRAS、QCURL_INSTALL_HEADERS_TEST_SUPPORT 分别定义四组件安装头，另有生成头 QCurlConfig.h。
- [surface_manifest.json](../../../tests/public_api/surface_manifest.json)记录组件、Stable/Preview 成熟度、Public/Internal 可见性及安装面。检查必须与 CMake 生成的四组件清单双向对齐，拒绝无归属、重复、漏装、越界或陈旧条目。
- Core 默认消费；Blocking Extras 是显式 INTERFACE target，实现在 Core 库内；Other Extras 独立编译；Test Support 是开发静态库。Preview 与 Internal 都不是额外组件。
- QCURL_INSTALL_HEADERS_EXTRAS 仅为非默认头的聚合集合，不定义所有权；WebSocket 按 QCURL_WEBSOCKET_SUPPORT 条件安装，不进入 Core。

[正式合同](../release/2.0.0-hard-break-release-contract.md)规定 2.x Core 源码兼容，ABI 非稳定，每次更新都须下游重编译和链接。机器清单负责枚举，不在正文复制完整头列表。

## 2. 公开头约束

- 安装头只暴露 QCurl 自有 API、Qt 公开依赖及必要标准类型；禁止 libcurl 头/CURL 类型/curl API、Qt private、任何 `_p.h`、tuple 和自定义 PIMPL 宏。
- libcurl 转换放到 cpp/private；能前置声明就不强制传递 include。共享轻量类型使用独立 type header，避免大头文件成为转运站。
- 值类型、QObject/运行时类及不完整类型 special members 遵循 PIMPL 规范，不在这里逐类重写布局表。
- Core cookie 模型不得泄漏 QtNetwork cookie；CachePolicy 类型不混入 concrete cache 读取接口，HttpVersion 头不公开 curl 常量映射。
- RequestConfig 只承载现行配置族；DNS/DoH/connect-to/resolve override/Happy Eyeballs/本地端口绑定不进入默认 Core。运行时 registry/lease、transfer record、handle accessor、日志脱敏 helper 留在非安装面；QCurlRuntimeState 这个公开枚举仍随 QCurlRuntime 交付，不能和私有 registry 混淆。
- scheduler 实现和 admission core 不安装、不导出，用户配置、命令和通知只走 manager；行为细节链接[lane 正文](../../user/lane-scheduler.md)。
- Body/Multipart helper 不承担 manager 发送；transfer job 不旁路 manager 管线。Middleware base 不泄漏具体策略状态，mock/capture/test hook 不回流 Core。
- 磁盘缓存只接受有界 fixed-header v4，v3/未知格式按 miss 清理；QSaveFile 禁用 direct fallback，单文件原子提交。不得恢复无界 QDataStream 容器解析，缓存容器、锁与文件系统细节留在实现中。
- QCNetworkLoggerHandle 的共享控制块与最终删除留在库内，公开面不得恢复 owning smart pointer 或裸 owning logger 注入；reply 保留已捕获的 logger。使用方法见[配置](../../user/configuration.md)。

### QObject 借用的特殊边界

QCNetworkReply 不重复声明 QObject::deleteLater，不能新增派生 wrapper 或导出符号。

WebSocket pool 的 acquire/preWarm 使用 QFuture，结果只携带纯值 LeaseId，不携带 QObject 指针。owner thread 解析临时 non-owning 借用并归还同一 lease：

```cpp
[[nodiscard]] LeaseResult resolveLease(LeaseId leaseId, QCWebSocket **socket) const;
[[nodiscard]] LeaseResult release(LeaseId leaseId);
[[nodiscard]] bool clearPool(const QUrl &url = QUrl(), QString *error = nullptr);
[[nodiscard]] bool setConfig(const QCWebSocketPoolConfig &config, QString *error = nullptr);
```

错误线程、销毁中 pool、非法/未知/失效 lease 或非法输出参数必须拒绝且不改变池状态；有效借用不越过 lease 生命周期。同步 mutator 失败保持旧状态，成功清空可选错误输出。内部记录、映射、统计和 timer 隐藏，不能通过 mutex、局部事件循环或 queued mutator 改变公开 owner-thread 合同。

## 3. 安装与导出

### 安装集合

- Core-only stage 的 include/qcurl 集合必须恰好等于 Core 清单与 QCurlConfig.h；完整安装按四组件或 package metadata 逐文件归属。不允许漏装 public 或多装 private/QCPimpl/test hooks。
- Core 使用 Runtime/Development。Blocking 只额外需要 BlockingExtrasDevelopment；Other Extras 使用 OtherExtrasRuntime/OtherExtrasDevelopment；Test Support 只有 TestSupportDevelopment。
- 默认 Core 负向 consumer 与组件 opt-in 正向 consumer 必须一起保留。无过滤安装部署所有已构建组件，不代表无组件 find_package 自动加载它们。

### 导出目标与依赖

- 无组件 `find_package(QCurl CONFIG REQUIRED)` 只加载 QCurl::QCurl。其余三个目标通过 COMPONENTS 显式加载；BlockingExtras 只转发 Core，不生成独立 libQCurlBlockingExtras。
- QCurlTargets 不得定义或引用 QCurl::libcurl_shared。shared Core interface 不泄漏 CURL::libcurl 或 ZLIB::ZLIB；static Core 只传播必需的 CURL::libcurl，并由 QCurlConfig.cmake 的 find_dependency 补齐，不传播 ZLIB。
- bundled libcurl_shared 只属于 staging/runtime 细节，不进入 public package/export 合同。
- 跨库私有 bridge 仅限 Other Extras 的 persistent-transfer register/remove，以及 Test Support 的 installNetworkMockProvider；声明不安装，具体 owner 由 `scripts/qcurl_abi_symbols.py` 限定。组件必须与 Core 来自同一 package source identity，不把锁步内部链接当作 public API/稳定 ABI。Blocking helper 同库解析且隐藏。

## 4. 隔离 consumer 的独有验收

所有 consumer 在独立工程中仅使用 staging prefix，不能回退源码 include。安装集合、编译可见性和运行行为各自验证，不能只用头能编译代替生命周期证明。

| 验收领域 | 必须保留的观察 |
| --- | --- |
| Core cookie | QCCookie 与两类异步结果的 accessor、success/failure、错误/policy/cookies 读取；默认 consumer 不需要 QtNetwork |
| Request/scheduler | Request 的 redirect/transfer 配置族；typed lane、policy/lane accessor、manager setSchedulerPolicy/schedulerPolicy/schedulerStatistics；不恢复 scheduler getter 工作流 |
| Logger | NetworkLogEntry accessor、QCNetworkLogResult、handle create/createWithBorrow、manager logger/debug trace；默认 logger 的 console/min-level/entries 与结构化 log 结果 |
| Cache | CachePolicy 设置/读取；结构化 request key、lookup(FreshOnly)、metadata/lookup result 与 clear result 的状态、计数、残留字节和稳定错误 |
| Body/Multipart/jobs | JSON/form body、fromFormData、contentType/data/size/toByteArray/fieldCount、job 类型可见性；job 构造不启动，connect 后显式 start |
| Cancel/middleware/pool | attach/attachMultiple/autoTimeout/cancel/isCancelled；middleware 派生及 manager 注册/移除；connection-pool config/manager/statistics accessor |
| Reply 生命周期 | 继承的 deleteLater 可调用，meta-object method index 早于 QCNetworkReply::methodOffset；动态符号拒绝派生 wrapper |
| Blocking Extras | opt-in；受限内存 maxInMemoryBodyBytes/BodyTooLarge、method-specific 与 custom request、downloadToDevice、rawHeaders、bytesReceived 和 diagnosticCurlCode；默认 Core 不能 include Blocking 头 |
| Other Extras | diagnostics/middleware 最小 value/API、能力启用时 WebSocket；默认 Core 不能 include Extras 头，Preview 不写成 Core |
| Test Support | captured request accessor、recordRequest/takeCapturedRequests/mockResponse 与 TestSupport manager 绑定；默认 Core 不能 include mock/test-support |
| 私有反向编译 | include QCNetworkReply_p.h 或 QCNetworkConnectionPoolManager_p.h 必须失败 |
| static 元类型 | 仅使用枚举的 consumer 显式 initialize，不依赖 scheduler 符号；安装包证据来自 static/OFF producer |

<a id="contract-checks"></a>
## 5. 四类公共合同检查

[public_contract_inventory.json](../../../tests/public_api/public_contract_inventory.json)是唯一公共合同清单，不因归并说明页另建手工镜像。范围覆盖四组件的每个非 Internal 安装头，四个审查轴各恰好分类一次：

1. 可失败 API：返回的接受/失败结果、拒绝无副作用与调用方检查义务。
2. 错误生命周期：结果何时确定、可读期限、成功/失败状态转换。
3. QObject 裸指针借用：owner thread、非 owning 生命周期、销毁/重入边界。
4. owning 智能指针 ABI：跨库控制块、allocator/deleter 与所有权表达。

分类为 contracts 的条目必须引用完整详细合同，包括当前/目标签名、状态/生命周期、线程约束、唯一处置和执行阶段。hardBreakAllowlist 仍按 JSON 校验范围，不能因合并文档放宽到未列出的 API；不在正文重复旧 T6/T7/T8 进度或逐 API 清单。

```bash
python3 tests/public_api/run_public_api_checks.py public-contract-inventory \
  --inventory tests/public_api/public_contract_inventory.json \
  --surface-manifest tests/public_api/surface_manifest.json \
  --source-root src
```

合法完整覆盖应通过；缺头、重复分类、失效合同引用、allowlist 不一致或回流旧签名应失败。具体状态语义以 JSON 与公开头为准，调度用法仍只链接用户正文。

## 6. 运行验证

在 shared/ON 测试树运行带锚点的 public-api/public-api-slow 标签，命令与环境见[构建与测试](../build-and-test.md#public-api)。surface manifest 检查保持 maturity/visibility 正交；规则扫描失败需报告文件、行号与规则名。

static 必须 BUILD_TESTING=OFF，使用[package evidence](../release/release-procedure.md#package-evidence)验证安装/导出/consumer/lifecycle，不在 OFF 树运行 CTest。发布时共享/静态完整包、默认 Core-only 负向面、动态符号与 sanitizer 均须来自对应 producer；单项 PASS 不代表完整发布。
