# QCurl 测试套件

本目录包含 QCurl v2.0 的完整测试套件，包括单元测试和集成测试。

---

## 📋 测试列表

| 测试文件 | 类型 | 测试数量 | 需要网络 | 说明 |
|---------|------|---------|---------|------|
| `tst_QCNetworkRequest.cpp` | 单元测试 | 31 | ❌ | 请求配置和流式接口 |
| `tst_QCNetworkReply.cpp` | 单元测试 | 27 | ✅ | Reply 功能、信号、错误处理（部分用例依赖本地 httpbin:8935） |
| `tst_QCNetworkError.cpp` | 单元测试 | 15 | ❌ | 错误码转换和字符串 |
| `tst_QCNetworkFileTransfer.cpp` | 功能测试 | 3 | ✅ | 流式下载/上传 + 断点续传 |
| `tst_Integration.cpp` | 集成测试 | 27 | ✅ | 真实网络请求和完整功能验证 |
| `tst_LargeFileDownload.cpp` | 外网回归 | 1 | ✅ | 外部 HTTPS 大文件下载（非门禁） |

**总计：100 个测试用例**

---

## 🚀 快速开始

### 1.（可选）准备 env 测试环境（依赖 httpbin 的用例需要）

依赖 httpbin 的测试需要本地 httpbin 服务（默认 `http://localhost:8935`）。  
启动/停止与健康检查请参考：[`docs/dev/build-and-test.md`](../docs/dev/build-and-test.md) 的 “2.2（可选）启动本地 httpbin（用于部分集成用例）”。

### 2. 运行测试

测试运行命令（offline/env 门禁、httpbin、HTTP/2、本地全量回归、libcurl_consistency）已统一维护在：  
[`docs/dev/build-and-test.md`](../docs/dev/build-and-test.md)

建议从以下章节开始：
- 2. 运行 Qt Test（ctest）
- 2.2（可选）启动本地 httpbin（用于部分集成用例）
- 2.3 全量回归（本地自检；非门禁）
- 3. libcurl_consistency Gate（可选）
- 3.3 全量回归（pytest 直跑；可选）

### 3. 查看详细输出

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

**依赖：** 部分用例需要本地 httpbin 服务（端口 8935），服务不可用时会自动跳过这些用例。

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

**依赖：** 需要本地 httpbin 服务（端口 8935），同时 `/bytes`、`/post`、`/range` 端点必须可用。

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

**⚠️ 需要本地 httpbin 服务（端口 8935）。**

#### 外部 HTTPS 大文件下载（非门禁）

`tst_Integration` 仅依赖本地 httpbin，不再包含外部大文件下载用例。  
外部 HTTPS + 大体量传输回归已迁移至 `tst_LargeFileDownload`（LABELS=external_heavy）。

---

## 🔧 配置选项

### 修改 httpbin 端口

如需使用其他端口，请编辑 `tst_Integration.cpp` 文件：

```cpp
// 文件顶部修改此常量
static const QString HTTPBIN_BASE_URL = QStringLiteral("http://localhost:YOUR_PORT");
```

然后重新编译：

```bash
cd build
cmake --build . --target tst_Integration
```

### 使用远程 httpbin 服务

虽然不推荐（网络不稳定），但如需使用远程服务（如 httpbin.org），请修改：

```cpp
static const QString HTTPBIN_BASE_URL = QStringLiteral("https://httpbin.org");
```

**注意：** 远程服务可能有限流、超时等问题，会导致测试不稳定。

---

## 🐛 常见问题

### Q1: 依赖 httpbin 的测试失败/跳过，提示连接拒绝

**原因：** httpbin 服务未启动。

**解决：** 按 [`docs/dev/build-and-test.md`](../docs/dev/build-and-test.md) 的 “2.2（可选）启动本地 httpbin（用于部分集成用例）” 启动并做健康检查。

### Q2: 测试超时失败

**原因：**
- httpbin 服务响应慢
- 网络问题
- Docker 容器性能问题

**解决：**
```bash
# 重启 httpbin 容器
docker restart qcurl-httpbin

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
