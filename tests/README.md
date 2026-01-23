# QCurl 测试套件

本目录包含 QCurl v2.0 的完整测试套件，包括单元测试和集成测试。

---

## ✅ 证据门禁口径（skip=fail + LABELS）

本仓库的门禁原则是 **“未执行=无证据=必须失败”**，因此：

- **QSKIP 在 ctest 门禁下视为失败（skip=fail）**：`tests/CMakeLists.txt` 为所有 QtTest 目标设置了 `FAIL_REGULAR_EXPRESSION "SKIP\\s*:"`，避免“ctest 全绿≠已覆盖”的误读。
- **是否运行某类用例，唯一入口是 ctest 的 LABELS 分组**：不要依赖 `QSKIP` 去“规避失败”，而应通过 `-L/--label-regex` 选择要跑的证据集合。

推荐入口（可复现命令）：

```bash
# 取证式门禁：默认仅跑 LABELS=offline（不依赖外部服务）
python3 scripts/ctest_strict.py --build-dir build

# 取证式门禁：跑 LABELS=env（需要先准备本地依赖；见下方 httpbin）
python3 scripts/ctest_strict.py --build-dir build --label-regex env
```

期望工件（Evidence Artifacts）：

- ctest：终端输出（含每个 QtTest 的 `Totals:` 行；无 `SKIP:`）。
- `libcurl_consistency`（可选 gate）：`build/libcurl_consistency/reports/gate_<suite>.json`、`build/libcurl_consistency/reports/junit_<suite>.xml`，以及 `curl/tests/http/gen/artifacts/<suite>/<case>/...`。

## 📋 测试列表

测试清单与数量会随版本演进；**不要维护静态“测试数量/总计”表格**。  
测试项的唯一准确来源是 `ctest`（由 `tests/CMakeLists.txt` 收集并标注 LABELS）。

推荐查看方式：

```bash
# 列出全部测试（不执行）
ctest --test-dir build -N

# 仅列出某类证据集合（LABELS）
ctest --test-dir build -N -L offline
ctest --test-dir build -N -L env
```

---

## 🚀 快速开始

### 1.（可选）准备 env 测试环境（依赖 httpbin 的用例需要）

依赖 httpbin 的测试通过环境变量 `QCURL_HTTPBIN_URL` 获取 base URL（**不再硬编码端口**）。  
推荐使用仓库内置脚本启动固定版本的 httpbin 并生成 env 文件：

```bash
./tests/httpbin/start_httpbin.sh --write-env build/test-env/httpbin.env
source build/test-env/httpbin.env
```

说明：脚本会做健康检查；失败时应视为“无法出具 env/httpbin 证据”，不要继续跑 `LABELS=env`。

### 2. 运行测试

测试运行命令（offline/env 门禁、httpbin、HTTP/2、本地全量回归、libcurl_consistency）已统一维护在：  
[`docs/dev/build-and-test.md`](../docs/dev/build-and-test.md)

建议从以下章节开始：
- 2. 运行 Qt Test（ctest）
- 2.2（可选）启动本地 httpbin（用于部分集成用例）
- 2.3 全量回归（本地自检；非门禁）
- 3. libcurl_consistency Gate（可选）
- 3.3 全量回归（pytest 直跑；可选）

#### ✅ 全量测试复验命令（提交前建议，一键入口）

```bash
# 1) 构建
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

# 2) QtTest 全量（含 env/httpbin）
./tests/httpbin/start_httpbin.sh --write-env build/test-env/httpbin.env
source build/test-env/httpbin.env
CTEST_OUTPUT_ON_FAILURE=1 ctest --test-dir build --output-on-failure
./tests/httpbin/stop_httpbin.sh

# 3) libcurl_consistency 全量（含 ext + HTTP/3 强制）
QCURL_LC_EXT=1 QCURL_REQUIRE_HTTP3=1 \
  python3 tests/libcurl_consistency/run_gate.py --suite all --with-ext --build
```

---

## 🧪 WebSocket 测试说明（smoke vs evidence）

- `tests/websocket-fragment-server.js`：**message-level** echo smoke（基于 `ws`，只能证明回显链路；不提供帧级证据）。
- `tests/websocket-evidence-server.js`：**frame-level** evidence（零外部依赖；显式发送 fragmentation/close，并输出 JSONL 工件供复核）。

在证据口径下，帧级语义（continuation frames / close code&reason）以 evidence server 的工件为准，避免“通过但不证明”的误读。

## 🔍 查看详细输出

```bash
# 运行单个测试并显示详细信息
./tests/tst_Integration -v2

# 只运行特定测试用例
./tests/tst_Integration testRealHttpGetRequest
```

---

## 📊 测试覆盖详情

### tst_QCNetworkRequest（31 个测试）

**测试内容：**
- URL 设置和获取
- HTTP Header 管理
- SSL 配置
- 代理配置
- 超时配置
- Range 请求
- HTTP 版本设置
- 流式 API 调用链

**无需网络连接，可离线运行。**

### tst_QCNetworkReply（27 个测试）

**测试内容：**
- 同步/异步请求执行
- 信号发射（finished、readyRead、downloadProgress 等）
- 错误处理和错误码映射
- 状态管理
- 数据读取（readAll、peek）
- HTTP 状态码获取
- 请求取消

**依赖：** 部分用例需要本地 httpbin 服务（通过 `QCURL_HTTPBIN_URL` 配置）。在证据门禁口径下，缺失依赖会触发 `QSKIP` 并导致门禁失败。

### tst_QCNetworkError（15 个测试）

**测试内容：**
- CURLcode → NetworkError 转换
- HTTP 状态码 → NetworkError 转换
- 错误字符串生成
- 错误类型判断（isCurlError、isHttpError）
- 边界情况处理

**无需网络连接，可离线运行。**

### tst_QCNetworkFileTransfer（3 个测试）

**测试内容：**
- `downloadToDevice()` 流式下载写入 QIODevice
- `uploadFromDevice()` 流式上传并回显校验
- `downloadFileResumable()` 断点续传（先取消再续传）

**依赖：** 需要本地 httpbin 服务（通过 `QCURL_HTTPBIN_URL` 配置），同时 `/bytes`、`/post`、`/range` 端点必须可用。

### tst_Integration（27 个测试）

**测试内容：**
- 真实 HTTP 请求（GET、POST、PUT、DELETE、PATCH、HEAD）
- Cookie 持久化和发送
- 自定义 Header（User-Agent、Authorization）
- 超时配置（连接超时、总超时）
- 重定向处理（自动跟随、最大重定向次数）
- SSL/TLS 配置
- 大文件下载
- 并发请求（并行和顺序）
- 错误处理（无效主机、连接拒绝、HTTP 错误码）

**⚠️ 需要本地 httpbin 服务（通过 `QCURL_HTTPBIN_URL` 配置）。**

#### 外部 HTTPS 大文件下载（非门禁）

`tst_Integration` 仅依赖本地 httpbin，不再包含外部大文件下载用例。  
外部 HTTPS + 大体量传输回归已迁移至 `tst_LargeFileDownload`（LABELS=external_heavy）。

---

## 🔧 配置选项

### 配置 httpbin base URL（推荐）

通过环境变量设置（无需改代码）：

```bash
# 推荐：启动固定 digest 的 httpbin 并自动写出 env（动态端口，避免冲突）
./tests/httpbin/start_httpbin.sh --write-env build/test-env/httpbin.env
source build/test-env/httpbin.env

# 或手动：指向你自己的 httpbin（不要求固定端口）
export QCURL_HTTPBIN_URL="http://127.0.0.1:<port>"
```

推荐使用 `./tests/httpbin/start_httpbin.sh --write-env ...` 自动生成并 `source`，避免端口冲突与版本漂移。

---

## 🐛 常见问题

### Q1: 依赖 httpbin 的测试失败/跳过，提示连接拒绝

**原因：** httpbin 服务未启动或 `QCURL_HTTPBIN_URL` 未设置/指向不可达地址。

**解决：** 运行 `./tests/httpbin/start_httpbin.sh` 并 `source build/test-env/httpbin.env`，或手动设置 `QCURL_HTTPBIN_URL`。

### Q2: 测试超时失败

**原因：**
- httpbin 服务响应慢
- 网络问题
- Docker 容器性能问题

**解决：**
```bash
# 重启 httpbin（以脚本/容器为准）
./tests/httpbin/stop_httpbin.sh || true
./tests/httpbin/start_httpbin.sh --write-env build/test-env/httpbin.env

# 或增加测试超时时间（编辑测试文件）
QVERIFY(waitForSignal(reply, SIGNAL(finished()), 30000));  // 改为 30 秒
```

### Q3: PUT/PATCH 测试失败

**原因：** libcurl 配置问题或 httpbin 版本兼容性。

**解决：** 建议按 [`docs/dev/build-and-test.md`](../docs/dev/build-and-test.md) 的 httpbin 章节重建容器（必要时先 `docker pull kennethreitz/httpbin`）。

### Q4: 如何跳过集成测试？

```bash
# 只运行单元测试
cd build/tests
./tst_QCNetworkRequest
./tst_QCNetworkReply
./tst_QCNetworkError

# 通过 CTest 排除集成测试
ctest -E Integration
```

---

## 📝 添加新测试

### 单元测试示例

```cpp
void TestYourClass::testNewFeature()
{
    // Arrange
    YourClass obj;

    // Act
    obj.doSomething();

    // Assert
    QCOMPARE(obj.result(), expectedValue);
}
```

### 集成测试示例

```cpp
void TestIntegration::testNewEndpoint()
{
    QCNetworkRequest request(QUrl(HTTPBIN_BASE_URL + "/your-endpoint"));
    auto *reply = manager->sendGet(request);

    QVERIFY(waitForSignal(reply, SIGNAL(finished()), 10000));
    QCOMPARE(reply->error(), NetworkError::NoError);

    auto data = reply->readAll();
    QVERIFY(data.has_value());

    // 验证响应数据
    QJsonObject json = parseJsonResponse(*data);
    QVERIFY(json.contains("expected_field"));

    reply->deleteLater();
}
```

---

## 📚 参考资料

- **Qt Test 框架文档**: https://doc.qt.io/qt-6/qtest-overview.html
- **httpbin API 文档**: https://httpbin.org/
- **Docker httpbin**: https://hub.docker.com/r/kennethreitz/httpbin

---

**QCurl 测试套件** - 确保代码质量和稳定性 ✅
