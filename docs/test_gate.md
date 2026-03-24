# 测试门禁可靠性说明

本文总结默认 gate 的可证明边界、非证明边界和最可靠的证据入口。

## 1. 当前最强的证据来源

- `tests/libcurl_consistency/run_gate.py`
  - skip=fail
  - schema 校验
  - 脱敏扫描
- 强判据专题
  - `pause_resume_strict`
  - `resp_headers_raw`
  - 其他明确比较原始字节或结构化事件边界的用例
- `tests/qcurl/CMakeLists.txt`
  - 通过 `FAIL_REGULAR_EXPRESSION` 把 `QSKIP` 视为无证据失败

## 2. 当前门禁不能证明的内容

门禁全绿，不等于以下问题一定不存在：

- 资源释放与生命周期问题
- 全量异步竞态
- 所有连接复用/池化边界
- 所有 TLS 细节与平台差异
- 所有头部、压缩和时序语义

默认 gate 比较的是“被定义为可观测 contract 的字段”，不会把所有实现细节都拉进来比较。

## 3. 应如何解读 P0

P0 的作用是提供最小、可重复、可机器判定的字节级或摘要级证据。

不要从 P0 推断：

- 所有并发语义都已经被覆盖
- 所有复用语义都已经被覆盖
- 所有 pause/resume 细节都已经被覆盖

这些能力是否被纳入默认 gate，必须以对应专题测试是否存在、README 是否明确声明为准。

## 4. 可靠性使用原则

1. 先看 suite 定义，再看报告结果。
2. 先看专题 contract，再看“是否全绿”。
3. 如果一个差异只能靠长篇解释成立，而没有专题测试兜底，就不应把它当作稳定证据。

## 5. 何时需要补强门禁

出现以下情况时，应该新增或强化专题用例，而不是继续堆说明文字：

1. 某类失败反复出现但 reports 无法归因
2. 某个 public API 的外部行为变成产品契约
3. README 需要靠大量“例外说明”才能解释为什么全绿
4. 某个结论只能依赖单次日志或人工判断

## 6. 相关入口

- `tests/README.md`
- `tests/libcurl_consistency/README.md`
- `tests/libcurl_consistency/tasks.md`
- `docs/dev/build-and-test.md`
