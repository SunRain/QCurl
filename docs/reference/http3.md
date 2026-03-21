# QCurl HTTP/3 参考

本文只描述 QCurl 对 HTTP/3 的稳定使用合同，不记录版本快照、单次性能结论或环境特例。

## 1. 能力边界

- HTTP/3 是否可用取决于当前构建所链接的 libcurl 运行时能力，以及目标服务端是否支持 QUIC/HTTP/3。
- QCurl 只负责把请求的 HTTP 版本偏好映射到 libcurl，不承诺在任意环境下都一定走到 HTTP/3。
- `Http3` 与 `Http3Only` 的区别必须由调用方显式选择：
  - `Http3`: 优先尝试 HTTP/3；不可用时允许按 libcurl 能力与服务器协商降级。
  - `Http3Only`: 仅接受 HTTP/3；环境或服务端不满足时应直接失败，而不是静默降级。
  - `HttpAny`: 不表达偏好，由 libcurl 自动协商。

## 2. 最小用法

```cpp
#include "QCNetworkAccessManager.h"
#include "QCNetworkHttpVersion.h"
#include "QCNetworkRequest.h"

using namespace QCurl;

QCNetworkAccessManager manager;
QCNetworkRequest request(QUrl("https://example.com"));
request.setHttpVersion(QCNetworkHttpVersion::Http3);

auto *reply = manager.sendGet(request);
```

严格要求 HTTP/3 时，改为：

```cpp
request.setHttpVersion(QCNetworkHttpVersion::Http3Only);
```

## 3. 调用方应当知道的合同

- 是否“偏好 HTTP/3”是请求级配置，不是全局默认。
- 如果业务允许降级，应优先使用 `Http3` 而不是 `Http3Only`。
- 如果业务把 HTTP/3 视为交付门槛，应在运行前做能力探测，并在 CI 或交付门禁中显式失败。
- HTTP/3 不等于一定更快。首次握手、网络质量、服务器实现和 UDP 可达性都会影响结果。

## 4. 验证入口

先确认运行时 libcurl 是否具备 HTTP/3 能力：

```bash
curl --version | grep HTTP3
```

然后使用仓库内门禁或回归入口验证 QCurl 的实际行为：

- `tests/qcurl/tst_QCNetworkHttp3`
- `tests/libcurl_consistency/`
- `docs/dev/build-and-test.md`

如果交付要求“缺少 HTTP/3 能力即失败”，应配合环境变量或对应 gate 脚本使用，而不是依赖文档中的经验判断。

## 5. 常见失败原因

### 5.1 `Http3Only` 直接失败

优先检查：

1. 运行时 libcurl 是否编译进 HTTP/3 支持。
2. 目标服务端是否真的开放了 HTTP/3。
3. 当前网络是否允许 UDP/443。

### 5.2 `Http3` 实际走了降级路径

这通常表示 QCurl 允许了协商回退，并不代表实现异常。若业务不接受该结果，应改用 `Http3Only` 并把失败视为能力缺失。

### 5.3 单次压测结果波动大

HTTP/3 的性能判断必须依赖同一环境、多次采样和一致的门禁脚本；单次样本不能写成产品承诺。

## 6. 相关文件

- `src/QCNetworkHttpVersion.h`
- `src/QCNetworkHttpVersion.cpp`
- `tests/qcurl/tst_QCNetworkHttp3.cpp`
- `docs/reference/performance.md`

## 7. 非目标

本文不负责：

- 记录某个版本的 benchmark 数字
- 记录某台机器的成功截图
- 替代交付门禁或 CI 报告
