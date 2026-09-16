# 传输 pause/resume 实现边界

本文面向维护者，保留 multi 驱动、callback 安全与恢复推进的实现约束。调用方状态机、线程入口和用法统一见[用户流控](../../user/flow-control.md)，不在这里复制教程。

## 1. 职责划分

- QCNetworkReply 维护用户可见状态；QCCurlMultiManager 在所属线程驱动 libcurl socket/timer。
- 传输 pause/resume 保留同一次传输。私有 scheduler 只负责 admission、Pending/Deferred/Running 槽位；defer/undefer 不保留或恢复传输现场。
- manager 的正式调度控制见[lane 合同](../../user/lane-scheduler.md)，不得让调用方依赖私有 scheduler。释放已运行槽位须显式取消，而不是把取消伪装为暂停。

## 2. Callback 与 owner-thread 安全

所有 libcurl pause 操作在 reply/multi owner thread 执行；跨线程的公开流控入口按其合同排队回 owner thread。callback 栈内暂停使用安全返回/原因位路径，不在不安全的 callback 深度直接重入 libcurl。

multi transfer record 在 detach 前持有 handle、callback userdata 和 backing storage。暂停、用户析构或排队恢复不能提前释放这些对象，细则见[binding 合同](libcurl-binding-contract.md)。

## 3. 恢复与原因组合

恢复成功后必须显式推进 multi（`QCCurlMultiManager::wakeup()`），因为解除暂停未必立即带来新的 socket/timer 事件。恢复可能同步触发缓存数据交付，必须在回调重入后复核对象存活与当前暂停原因。

下载背压、上传 source-not-ready 与用户暂停是独立原因。清除一个原因不能清掉其他原因，也不能把内部背压伪装为用户 ReplyState::Paused。水位、边沿通知和 source readyRead 的对外合同见用户流控页。

## 4. 有证伪能力的验证

相关位置：[ReplyFlowControl](../../../src/private/QCNetworkReplyFlowControl.cpp)、[ReplyCallbacks](../../../src/private/QCNetworkReplyCallbacks.cpp)、[multi manager](../../../src/QCCurlMultiManager.cpp)。修改时不仅检查最终完成，还要观察：

1. 暂停发生在允许的 callback/owner-thread 边界。
2. 静默窗口内没有违约数据交付，恢复后能继续推进。
3. 各原因组合、重复操作、取消或析构时不会丢失原因或访问悬空对象。
4. scheduler defer 与 transport pause 没有混为同一状态。

使用 Reply/Scheduler/Backpressure/StreamUpload QtTest 与 libcurl consistency 的强判据专题；命令和证据选择见[构建与测试](../build-and-test.md)。

## 5. 维护规则

变更 pause mode、恢复推进、callback 安全、下载背压或上传 source pause 时，同步本页、用户流控与受影响测试；调度行为只同步 lane 正文。历史任务通过[唯一历史索引](../archive/README.md)追溯，不继续维护旧状态页。
