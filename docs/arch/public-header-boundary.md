# Public Header 边界与安装面

> 本文记录 QCurl 当前对外安装面的最小事实：哪些头文件属于 public API、哪些实现细节已经下沉，以及 public header 与 libcurl/Qt 命名空间的边界约束。

## 1. 安装面定义

当前安装面由 `src/CMakeLists.txt` 中的 `QCURL_INSTALL_HEADERS` 与生成头 `QCurlConfig.h` 共同定义：

- 仅安装真正的 public headers（`QCNetworkRequest` / `QCNetworkAccessManager` / `QCNetworkReply` / WebSocket / cache / diagnostics 等）。
- generated header：`QCurlConfig.h`。
- install 验证口径：`cmake --install` 后，下游只能通过这些头文件完成 include 与链接。

## 2. 不属于 install surface 的 internal/private 头

以下头文件属于库内实现细节，不安装给下游：

- `_p.h` / private：`QCNetworkAccessManager_p.h`、`QCNetworkReply_p.h`、`QCWebSocket_p.h`、`qbytedata_p.h`
- internal curl plumbing：`QCCurlHandleManager.h`、`QCCurlMultiManager.h`、`CurlFeatureProbe.h`、`QCUtility.h`
- internal pipeline：`src/private/QCRequestPipeline_p.h`

这些文件仍可作为库内实现细节存在，但不属于对外稳定承诺。

## 3. public header 中的 libcurl 暴露面收敛

| 位置 | 当前合同 | 说明 |
| --- | --- | --- |
| `src/QCGlobal.h` | 仅暴露 QCurl 自身宏 | 不包含 `<curl/curl.h>`；仅暴露 QCurl 自身版本/feature 宏 |
| `src/QCNetworkAccessManager.h` | 不透传 curl 类型 | public manager header 只暴露 QCurl 自有 API |
| `src/QCNetworkReply.h` | 仅暴露 QCurl 自有状态 | pause/backpressure 对外只暴露 QCurl 自有枚举与状态 |
| `src/QCNetworkError.h` | 自有值类型隔离 | `fromCurlCode(int)` 仅接受整数码值；`CURLcode` 转换留在 `.cpp` |
| `src/QCNetworkHttpVersion.h` | 仅暴露 QCurl 版本枚举 | 对外保留 `QCNetworkHttpVersion` 枚举与 `toCurlHttpVersion(...)` 声明，libcurl 常量映射留在 `.cpp` |
| `src/QCNetworkConnectionPoolManager.h` | opaque pointer 边界 | internal handle 参数使用 `void *`，具体 `CURL *` 仅在 `.cpp` 中还原 |
| `src/QCurlConfig.h.in` → `QCurlConfig.h` | 构建期注入版本信息 | 不包含 `curl/curlver.h`；libcurl 版本字符串由 CMake 写入 |

## 4. 导出与命名空间约束

- public 顶层类型 / 对外自由函数统一使用 `QCURL_EXPORT`，避免 Windows 下出现符号可见性不一致。
- QCurl 自身类型统一处于 `namespace QCurl`；不再混用 `QT_BEGIN_NAMESPACE/QT_END_NAMESPACE` 包裹 QCurl API。
- `QCNetworkLogRedaction` 统一为 `namespace QCurl { namespace QCNetworkLogRedaction { ... } }` 口径。

## 5. 维护规则

- 新增 public API 时，先判断是否属于安装面；若只是实现细节，优先放入 `src/private/` 或非安装头。
- 新增 public header 时，不得直接包含 `<curl/curl.h>`；需要 libcurl 类型时，优先使用：
  - 自有枚举/值类型映射；
  - opaque pointer；
  - `.cpp` 内部转换。
- 修改 install 面后，必须配套执行：
  - `cmake --install build --prefix <staging>`
  - 最小 consumer smoke（`find_package(QCurl)` + `target_link_libraries(... QCurl::QCurl)`）
