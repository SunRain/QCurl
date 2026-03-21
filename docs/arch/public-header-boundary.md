# Public Header 边界与安装面

> 本文记录 QCurl 当前对外安装面的唯一合同：哪些头文件属于 public API、哪些内容必须留在 internal/private，以及 install/export/consumer smoke 的验收边界。

## 1. 安装面 SSOT

QCurl 的 install surface 只有两个来源：

- `src/CMakeLists.txt` 中的 `QCURL_INSTALL_HEADERS`
- 生成头 `QCurlConfig.h`

其中：

- `QCNetworkHttpMethod.h` 现在是独立的 public type header，`HttpMethod` 不再由 `QCNetworkReply.h` 承载。
- `QCNetworkHttpVersion.h` 只暴露 `QCNetworkHttpVersion` 枚举；libcurl 常量映射函数 `detail::toCurlHttpVersion(...)` 仅存在于 internal header `src/private/QCNetworkHttpVersion_p.h`。
- WebSocket 相关头是否进入安装面，仍由 `QCURL_WEBSOCKET_SUPPORT` 条件追加控制。

任何不在上述清单中的头文件，都不属于对下游的稳定承诺。

## 2. Public Header 设计准则

- public header 只暴露 QCurl 自有 API、值类型和 Qt-only 公开依赖。
- 若多个 public header 共享同一个轻量类型，优先拆出独立 type header（例如 `QCNetworkHttpMethod.h`），而不是让某个“大头文件”成为事实上的转运站。
- 能用 forward declaration 的地方优先 forward declaration；调试输出、字符串化和实现细节优先放到 `.cpp`。
- libcurl 语义转换必须下沉到 `.cpp` 或 `src/private/*`，不得通过 public header 把 `CURL*`、`curl_*` 或 `<curl/...>` 透传给下游。

## 3. 不属于安装面的 internal/private 头

以下头文件属于库内实现细节，不安装给 consumer：

- `_p.h` / private：`QCNetworkAccessManager_p.h`、`QCNetworkReply_p.h`、`QCWebSocket_p.h`、`qbytedata_p.h`
- internal curl plumbing：`QCCurlHandleManager.h`、`QCCurlMultiManager.h`、`CurlFeatureProbe.h`、`QCUtility.h`
- internal pipeline / adapters：`src/private/*`

这些文件可以在库内自由演进，但不应出现在安装前缀，也不应被对外文档当作 public API 引用。

## 4. 安装头禁止项

对 `QCURL_INSTALL_HEADERS` 中的头文件，以下内容一律视为违约：

- `#include <curl/...>`
- `CURL*`、`curl_*`
- Qt private include（如 `Qt.../private/...`）
- 任意 `*_p.h`
- `<tuple>`、`std::tuple`

这些规则由 `tests/public_api/run_public_api_checks.py scan` 执行；出现违约项时必须以“文件 + 行号 + 规则名”失败。

## 5. Install / Export 合同

### 5.1 安装集合

- `cmake --install` 到 staging prefix 后，`<staging>/include/qcurl/` 的头文件集合必须与 `QCURL_INSTALL_HEADERS + QCurlConfig.h` **完全一致**。
- 不允许多装 internal/private 头，也不允许漏装 manifest 中的 public 头。

### 5.2 导出目标

- 安装后的 package 只对下游暴露 `QCurl::QCurl`。
- `QCurlTargets*.cmake` 不得定义或引用 `QCurl::libcurl_shared`。
- `QCurl::QCurl` 的公开接口不得泄漏 `CURL::libcurl`。
- 当 `QCURL_BUILD_LIBCURL_CONSISTENCY=ON` 时，bundled `libcurl_shared` 只允许作为 staging/runtime 细节存在，不进入 public package/export 合同。

### 5.3 Consumer Smoke

- 正向 consumer：独立工程只能通过 staging prefix 执行 `find_package(QCurl CONFIG REQUIRED)`，随后 include public headers 并链接 `QCurl::QCurl` 成功。
- 反向断言：独立 consumer 尝试 `#include <QCNetworkReply_p.h>` 必须编译失败。
- consumer smoke 不允许回落到源码树 include path。

## 6. 验证口径

public header 边界的最低验收口径如下：

- 快路径：`ctest --test-dir build -L '^public-api$' --output-on-failure`
- 慢路径：`ctest --test-dir build -L '^public-api-slow$' --output-on-failure`

其中：

- `public-api`：逐头 self-compile + 规则扫描
- `public-api-slow`：staging install、安装集合校验、导出合同校验、isolated consumer smoke

> 说明：CTest 的 label 参数是正则；为了避免 `public-api` 误匹配 `public-api-slow`，本仓库文档统一使用带锚点的写法。
