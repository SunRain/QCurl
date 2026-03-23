# QCurl 测试套件

本目录包含 QCurl 的测试套件，包括单元测试和集成测试。

---

## 📁 目录结构（测试分类）

- `tests/qcurl/`：QCurl 本身的 QtTest（C++）与本地测试依赖（httpbin/node server/testdata/node_modules）。
- `tests/libcurl_consistency/`：QCurl ↔ libcurl 一致性测试（pytest + baseline clients + gate）。
- `tests/public_api/`：public/install surface guardrails（逐头 self-compile、规则扫描、staging install、导出合同校验、isolated consumer smoke）。

## Public API 安装面门禁

当改动 public headers、`QCURL_INSTALL_HEADERS` 或 install/export 合同时，请执行：

```bash
ctest --test-dir build -L '^public-api$' --output-on-failure
ctest --test-dir build -L '^public-api-slow$' --output-on-failure
```

如需额外验证 `QCURL_WEBSOCKET_SUPPORT` 关闭时的条件安装面（WebSocket 相关头不应进入安装面），可使用
`-DQCURL_FORCE_DISABLE_WEBSOCKET_SUPPORT=ON` 配置一套额外 build 目录并复用上述两条命令（示例见
`docs/dev/build-and-test.md` 的 Public API 章节）。

门禁内容：

- `public-api`：每个安装头作为第一个 include 单独编译，并执行 public header 规则扫描
- `public-api-slow`：将当前 build install 到隔离的 staging prefix，校验安装头集合、导出合同，并在只指向 staging prefix 的 consumer 工程里执行正向/反向 smoke

反向 smoke 的 contract 是：

- `find_package(QCurl CONFIG REQUIRED)` + include/link `QCurl::QCurl` 成功
- `#include <QCNetworkReply_p.h>` 失败

> 说明：CTest 的 `-L` 参数是正则；这里使用 `'^public-api$'` / `'^public-api-slow$'` 是为了避免快路径误匹配慢路径。

## 证据门禁口径（skip=fail + LABELS）

本仓库的门禁原则是 **“未执行=无证据=必须失败”**，因此：

- **默认：QSKIP 在 ctest 证据门禁下视为失败（skip=fail）**：`tests/qcurl/CMakeLists.txt` 为（几乎）所有 QtTest 目标设置了 `FAIL_REGULAR_EXPRESSION "SKIP\\s*:"`，避免“ctest 全绿≠已覆盖”的误读。
  - **例外：`LABELS=external_network`（如 `tst_QCNetworkDiagnostics`）允许 QSKIP**。该类测试包含公网探测场景，默认不开外网以避免在受限网络/CI 中引入不稳定因素；如需执行公网探测，显式设置 `QCURL_ALLOW_EXTERNAL_NETWORK=1`。
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

### “基本无问题”验收门禁（强制归档）

当需要给出可作为工程证据链的“基本无问题”结论时，必须使用统一 runner：

```bash
python3 scripts/run_basic_no_problem_gate.py --build-dir build --run-id "<your-run-id>"
```

该 runner 会强制产出并归档关键工件：
- `build/evidence/basic-no-problem/<run-id>/manifest.json`
- `build/evidence/basic-no-problem/<run-id>.tar.gz`

门禁组合与前置条件见：[`docs/dev/build-and-test.md`](../docs/dev/build-and-test.md) 的 “基本无问题”章节。

## 📋 测试列表

测试清单与数量会随版本演进；**不要维护静态“测试数量/总计”表格**。  
测试项的唯一准确来源是 `ctest`（由 `tests/qcurl/CMakeLists.txt` 注册并标注 LABELS）。

推荐查看方式：

```bash
# 列出全部测试（不执行）
ctest --test-dir build -N

# 仅列出某类证据集合（LABELS）
ctest --test-dir build -N -L offline
ctest --test-dir build -N -L env
```

### 🌐 external_network（默认关闭公网探测）

`LABELS=external_network` 的测试会优先使用本地依赖（若设置了 `QCURL_HTTPBIN_URL`），并默认跳过公网探测用例。

如需显式启用公网探测（非门禁、可能不稳定），使用：

```bash
QCURL_ALLOW_EXTERNAL_NETWORK=1 ctest --test-dir build -L external_network --output-on-failure
```

说明：

- `tst_QCNetworkHttp2`、`tst_QCNetworkActorThreadModel` 这类依赖本地监听端口的测试，在沙箱或受限容器中若出现 `listen EPERM` / 本地端口不可绑定，会显式 `QSKIP`，并通过 `local_port` 语义与离线门禁隔离。
- `tst_QCNetworkDiagnostics` 在设置了 `QCURL_HTTPBIN_URL` 时，会对本地 httpbin 做有限次短重试；若本地依赖未稳定就绪，会带详细原因跳过，而不是给出缺乏上下文的硬失败。

### 📦 external_heavy（显式 opt-in 的外部大体量 smoke）

`LABELS=external_heavy` 不属于 deterministic 门禁，默认关闭。当前集合仅包含 `tst_LargeFileDownload`，用于验证“真实 HTTPS + 大体量传输”链路。

使用方式：

```bash
QCURL_RUN_EXTERNAL_HEAVY=1 \
  ctest --test-dir build -L external_heavy --output-on-failure
```

可选环境变量：

- `QCURL_LARGE_FILE_URL`：覆盖默认下载 URL。
- `QCURL_LARGE_FILE_EXPECTED_BYTES`：当使用自定义 URL 时，可显式给出期望字节数。

执行语义：

- 未设置 `QCURL_RUN_EXTERNAL_HEAVY=1` 时默认 `QSKIP`。
- 执行前会先做一次 HEAD preflight；若出现 DNS 失败、HTTP 404、远端资源下线或超时，则显式 `QSKIP`，避免把第三方镜像漂移误判为产品缺陷。

---

## 🚀 快速开始

### 1.（可选）准备 env 测试环境（依赖 httpbin 的用例需要）

依赖 httpbin 的测试通过环境变量 `QCURL_HTTPBIN_URL` 获取 base URL（**不再硬编码端口**）。  
推荐使用仓库内置脚本启动固定版本的 httpbin 并生成 env 文件：

```bash
./tests/qcurl/httpbin/start_httpbin.sh --write-env build/test-env/httpbin.env
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

#### 全量测试复验命令（提交前建议，一键入口）

```bash
# 1) 构建
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

# 2) QtTest 全量（含 env/httpbin）
./tests/qcurl/httpbin/start_httpbin.sh --write-env build/test-env/httpbin.env
source build/test-env/httpbin.env
CTEST_OUTPUT_ON_FAILURE=1 ctest --test-dir build --output-on-failure
./tests/qcurl/httpbin/stop_httpbin.sh

# 3) libcurl_consistency 全量（含 ext + HTTP/3 强制）
QCURL_LC_EXT=1 QCURL_REQUIRE_HTTP3=1 \
  python3 tests/libcurl_consistency/run_gate.py --suite all --with-ext --build
```

---

## 🧪 WebSocket 测试说明（smoke vs evidence）

- `tests/qcurl/websocket-fragment-server.js`：**message-level** echo smoke（基于 `ws`，只能证明回显链路；不提供帧级证据）。
- `tests/qcurl/websocket-evidence-server.js`：**frame-level** evidence（零外部依赖；显式发送 fragmentation/close，并输出 JSONL 工件供复核）。

在证据口径下，帧级语义（continuation frames / close code&reason）以 evidence server 的工件为准，避免“通过但不证明”的误读。

## 🔒 HTTPS/TLS 证据说明（可控成功/失败）

`tst_Integration` 中包含本地 HTTPS 证据用例（自签名证书）：
- 失败路径：默认安全配置应拒绝（`NetworkError::SslHandshakeFailed`）
- 成功路径：配置 `caCertPath=tests/qcurl/testdata/http2/localhost.crt` 后应成功

用例会输出 `QCURL_EVIDENCE ...` 行，便于在 CI/日志中复核。

## 🔍 查看详细输出

```bash
# 运行单个测试并显示详细信息
./build/tests/tst_Integration -v2

# 只运行特定测试用例
./build/tests/tst_Integration testRealHttpGetRequest
```

---

## 测试覆盖主题

本节只描述各测试目标的大致职责，不维护静态用例数量；具体清单始终以 `ctest -N` 为准。

### tst_QCNetworkRequest

- 覆盖 URL、HTTP Header、SSL、代理、超时、Range、HTTP 版本与流式 API 调用链
- 无需网络连接，可离线运行

### tst_QCNetworkReply

- 覆盖同步/异步执行、信号发射、错误处理、状态机、数据读取与取消语义
- 部分用例依赖本地 httpbin（通过 `QCURL_HTTPBIN_URL` 配置）；在证据门禁口径下，缺失依赖会触发 `QSKIP` 并导致门禁失败

### tst_QCNetworkError

- 覆盖 `CURLcode` / HTTP 状态码到 `NetworkError` 的映射，以及错误字符串和错误类型判断
- 无需网络连接，可离线运行

### tst_QCNetworkFileTransfer

- 覆盖 `downloadToDevice()` 的流式写入、`uploadFromDevice()` 的回显校验，以及 `downloadFileResumable()` 在“先制造部分下载、再续传”路径上的语义
- 依赖本地 httpbin，且 `/bytes`、`/post`、`/range` 端点必须可用

### tst_Integration

- 覆盖真实 HTTP 方法、Cookie、自定义 Header、超时、重定向、SSL/TLS、大文件、并发与错误处理
- 依赖本地 httpbin（通过 `QCURL_HTTPBIN_URL` 配置）

#### 外部 HTTPS 大文件下载（非门禁）

`tst_Integration` 仅依赖本地 httpbin，不再包含外部大文件下载用例。  
外部 HTTPS + 大体量传输回归已迁移至 `tst_LargeFileDownload`（LABELS=external_heavy）。

---

## 🔧 配置选项

### 配置 httpbin base URL（推荐）

通过环境变量设置（无需改代码）：

```bash
# 推荐：启动固定 digest 的 httpbin 并自动写出 env（动态端口，避免冲突）
./tests/qcurl/httpbin/start_httpbin.sh --write-env build/test-env/httpbin.env
source build/test-env/httpbin.env

# 或手动：指向你自己的 httpbin（不要求固定端口）
export QCURL_HTTPBIN_URL="http://127.0.0.1:<port>"
```

推荐使用 `./tests/qcurl/httpbin/start_httpbin.sh --write-env ...` 自动生成并 `source`，避免端口冲突与版本漂移。

---

## 🐛 常见问题

### Q1: 依赖 httpbin 的测试失败/跳过，提示连接拒绝

**原因：** httpbin 服务未启动或 `QCURL_HTTPBIN_URL` 未设置/指向不可达地址。

**解决：** 运行 `./tests/qcurl/httpbin/start_httpbin.sh` 并 `source build/test-env/httpbin.env`，或手动设置 `QCURL_HTTPBIN_URL`。

### Q2: 测试超时失败

**原因：**
- httpbin 服务响应慢
- 网络问题
- Docker 容器性能问题

**解决：**
```bash
# 重启 httpbin（以脚本/容器为准）
./tests/qcurl/httpbin/stop_httpbin.sh || true
./tests/qcurl/httpbin/start_httpbin.sh --write-env build/test-env/httpbin.env
```

若仍超时，优先排查容器资源、网络质量或门禁环境差异；不要把“直接放宽用例超时”当作常规修复手段。

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

## 参考资料

- **Qt Test 框架文档**: https://doc.qt.io/qt-6/qtest-overview.html
- **httpbin API 文档**: https://httpbin.org/
- **Docker httpbin**: https://hub.docker.com/r/kennethreitz/httpbin

---

本目录的长期真相源是测试代码、`tests/qcurl/CMakeLists.txt` 中的 LABELS 分组，以及自动化产出的证据工件。
