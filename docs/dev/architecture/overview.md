# QCurl 架构概览

面向维护者说明模块边界与请求路径。用户用法从[文档目录](../../README.md)进入；组件、成熟度及 2.x 源码兼容/下游重编译合同由[正式发布合同](../release/2.0.0-hard-break-release-contract.md)定义，这里不复制发布矩阵。

## 模块边界

- Core 承载异步请求及公共配置、调度、缓存、传输任务；Blocking Extras 提供显式 opt-in 的同步值结果，其实现在 Core 物理库内。
- Other Extras 独立编译，承载 Diagnostics、Middleware Extras 和有能力条件的 WebSocket；Test Support 是开发静态库。包内 private bridge 只用于同一 source identity 的锁步链接，不是 public API。
- 公开头与安装面在 `src/`；非安装 `_p.h` / `src/private/` 隐藏 libcurl、运行时句柄及内部协作。精确范围见[公共头边界](public-header-boundary.md)。

## 异步请求路径

```text
QCNetworkRequest + method/body
  -> QCNetworkAccessManagerSend 的请求入口与执行上下文
  -> QCNetworkReply 的 NormalizedRequest / CurlPlan 快照
  -> 可选的 manager-owned scheduler（私有 admission core）
  -> owner-thread QCCurlMultiManager 的 socket/timer 驱动
  -> reply 的状态、响应头、数据、进度和终态通知
```

[请求管线](request-normalization-pipeline.md)固化 method/body 计划；具体 curl options 与 callback 安装仍在执行器内部，不能把计划 digest 当作全部传输语义的证明。scheduler 配置/通知/命令只通过 manager，对调用方的排序和生命周期语义见[用户合同](../../user/lane-scheduler.md)。

成功注册到 multi 后，transfer record 持有 easy handle、callback target 与 backing storage，reply 是 observer。reply 析构不能提前释放仍注册的传输；owner thread 完成 detach 后再回收。线程、libcurl 创建和 package bridge 的细则见[binding 合同](libcurl-binding-contract.md)。

## 流控与同步路径

传输 pause/resume 保留同一次传输；scheduler defer 只管理未启动请求，不是恢复已运行传输的入口。下载 backpressure 与上传 source pause 各有原因位，不冒充用户 Paused 状态；[传输实现](transport-pause-resume.md)负责 callback 安全与恢复推进。

Blocking Extras 以配置快照和值结果工作，不借用 live manager 的跨线程状态；cookie 通过快照/delta 交付。不要增加透明 blocking getter、嵌套事件循环或第二网络执行路线。

QCurlRuntime 只协调进程期手动 libcurl cleanup；外部 libcurl 使用者先停止、排空并 join，owner loops 运行时排空内部 lease。它不提供 QCurl 动态卸载。

## 验证与维护位置

| 位置 | 职责与入口 |
| --- | --- |
| `tests/public_api/` | 安装/导出、独立 consumer、公共合同与元类型 |
| `tests/qcurl/` | QtTest 生命周期、错误、线程和协议行为 |
| `tests/libcurl_consistency/` | 相对 libcurl 的外部可观察一致性 |
| `examples/` / `benchmarks/` | 使用示例与性能趋势，不定义产品合同 |
| [构建与测试](../build-and-test.md) | 日常检查与失败解释 |
| [发布操作](../release/release-procedure.md) / [UCE](../uce/README.md) | 候选验收和可归档证据 |

公开行为变化同步用户正文与头注释；内部边界变化更新对应架构说明；install/export 变化验证适用的 shared 测试树与 static/OFF package consumer。历史记录只从[历史索引](../archive/README.md)查阅，不用于当前 readiness。
