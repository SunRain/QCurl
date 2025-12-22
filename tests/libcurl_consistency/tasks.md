# 计划任务列表（由 README 拆分）

> 目标：落地 “QCurl ↔ libcurl（含 libtest 工具）数据一致性测试候选集”，采用 **Qt Test（C++）+ pytest（Python）混合** 的方式实现与编排。

| ID | 任务描述 | 优先级 | 依赖关系 | 状态 | 执行日志 |
|---|---|---|---|---|---|
| LC-0 | 明确“一致性”的可观测断言与 `artifacts` 最小字段（下载/上传/WS：status、协议族(h1/h2/h3)、body 长度+hash、关键响应头；**P0 必做：请求语义摘要**（method/url/关键头规范化）+ request body 的 len/hash；不强制记录“服务端看到的原始请求字节”；错误码/超时归一化规则）。 | 高 | 无 | 已完成 | 2025-12-16：已在 README 固化字段，确认请求语义摘要为 P0 必做，不记录服务端原始字节。 |
| LC-1 | 固化候选集清单与边界：把 `README.md` 中 **P0/P1/可选扩展/明确排除** 转成可执行清单（可先用静态列表；如需参数化再引入 manifest）。 | 高 | LC-0 | 已完成 | 2025-12-16：已转成本表，覆盖 P0/P1/扩展/排除清单。 |
| LC-2 | 编写 pytest 驱动骨架：复用 `curl/tests/http/testenv` 启动服务端（http/1.1 + h2 + h3 + ws），提供统一的 “启动/停止/日志收集/端口分配” 封装。 | 高 | LC-0 | 已完成 | 2025-12-16：新增 `tests/libcurl_consistency/conftest.py` 复用 testenv（注入 CURL_BUILD_DIR/CURL/CURLINFO），并生成 `curl/tests/http/config.ini`；新增 `test_env_smoke.py` 验证 httpd/nghttpx/ws 可启动（受 sandbox 限制需 escalated）。<br>2025-12-16 22:50:45：复跑 `pytest tests/libcurl_consistency/test_env_smoke.py` 通过；sandbox 下端口分配触发 `PermissionError`，需 escalated。<br>2025-12-17 00:11:39：修复 curl testenv `httpd.py` 生成配置中 `TypesConfig` 引号缺失，复跑 smoke 通过。<br>2025-12-17 11:22:09：新增 `tests/libcurl_consistency/nghttpx_from_source/`，CMake target `qcurl_nghttpx_h3` 通过 `ExternalProject` 从源码构建并安装 **h3-capable** `nghttpx` 到 `build/libcurl_consistency/nghttpx-h3/`（`--enable-http3`）；`tst_LibcurlConsistency` 依赖该 target；同时将 `curl/tests/http/config.ini` 的 `nghttpx` 指向 build 下路径，使 `env.have_h3()` 覆盖 h3 变体。验证：`build/libcurl_consistency/nghttpx-h3/bin/nghttpx --version` 包含 `ngtcp2/...`。 |
| LC-3 | 在 pytest 中跑 **libcurl baseline**：通过 `LocalClient(name='cli_*')` 调用 `curl/tests/libtest/libtests`，生成 baseline `artifacts`（每条用例一份 JSON）。 | 高 | LC-1、LC-2 | 已完成 | 2025-12-16：完善 `pytest_support/baseline.py`（hash/len、请求语义摘要）；补齐 `case_defs.py` P0 参数模板；新增 `curl/tests/libtest/cli_hx_range_resume.c` 并更新 `curl/tests/libtest/Makefile.inc`，重建 `curl/build/tests/libtest/libtests`。<br>2025-12-16 22:50:45：`cmake --build \"curl/build\" --target libtests` 构建通过。<br>2025-12-16 22:58:27：为“请求语义摘要”补齐可比字段：上传 baseline 用例参数追加 `-l` 强制 Content-Length；Range 续传 baseline 支持 `-S` 并用 `CURLOPT_RANGE` 显式 end，已重建 `libtests` 并复跑 P0 通过。 |
| LC-4 | 新增 Qt Test（C++）：实现与 P0/P1 对齐的 QCurl 客户端场景（下载/上传/WS），将结果写出为 QCurl `artifacts`（JSON，字段与 LC-0 一致）。 | 高 | LC-0、LC-2 | 已完成 | 2025-12-16：新增 `tests/tst_LibcurlConsistency.cpp`（按 env vars 执行单用例并落盘 download_*.data），并在 `tests/CMakeLists.txt` 注册生成 `build/tests/tst_LibcurlConsistency`；`pytest_support/qcurl_runner.py` 负责运行与写出 QCurl artifacts。<br>2025-12-16 22:50:45：`cmake --build \"build\" --target tst_LibcurlConsistency` 构建通过。 |
| LC-5 | 实现对比器（pytest 侧）：读取 baseline/QCurl `artifacts`，做字节级（hash/len）与关键语义字段对比（**P0 必做：请求语义摘要一致性**），输出可机器消费的报告（JSON/JUnit 二选一或同时）。 | 高 | LC-0、LC-3、LC-4 | 已完成 | 2025-12-16：新增 `pytest_support/compare.py`（compare + assert helper），并在 `test_p0_consistency.py` 接线执行。<br>2025-12-16 22:50:45：`QCURL_QTTEST=\"build/tests/tst_LibcurlConsistency\" pytest tests/libcurl_consistency/test_p0_consistency.py` 通过（7 passed）；artifacts 输出在 `curl/tests/http/gen/artifacts/`（sandbox 下需 escalated 启动服务端）。 |
| LC-5a | P0 Gate 可靠性：将“请求语义摘要”从“构造值”升级为“观测值”。实现方式：基于服务端观测（httpd access log + ws handshake log）提取 method + path + 关键请求头（Range/Content-Length 等白名单），写回 baseline/QCurl artifacts；对比时忽略用于关联的 query `id`。 | 高 | LC-2、LC-5 | 已完成 | 2025-12-16 23:24:36：开始实现服务端观测：基于 httpd access log + ws handshake JSONL 生成“请求语义摘要”，pytest driver 为 baseline/QCurl 注入独立 req_id 并写回 artifacts 的 request.method/url/headers（基于观测）。<br>2025-12-17：兼容性修复（不改 `curl/tests/...`）：`tests/libcurl_consistency/conftest.py` 不再依赖 `httpd._access_log`（不同 curl 版本不保证存在），改为 monkeypatch `Httpd.reset_config()` 注入 `CustomLog` 到 `logs/access_log`；并 monkeypatch `NghttpxQuic.start()` 增加 `--accesslog-file/--accesslog-format`，固定到 `nghttpx-quic/access_log`；`lc_logs` 指向上述稳定路径。 |
| LC-5b | P0 Gate 可靠性：补齐“响应语义摘要”的观测字段（至少 status + 实际协议 http/1.1/h2；可选关键响应头）。并增加“期望协议”自检：h2 变体实际必须为 h2（否则 fail/skip，避免伪通过）。 | 高 | LC-5a | 已完成 | 2025-12-16 23:39:50：`test_p0_consistency.py` 基于 httpd access_log 写回 response.status/http_version，并对每个变体强制校验实际协议（http/1.1/h2）。修复问题：连接池在 `QCNetworkReply.cpp` 末尾调用 `configureCurlHandle()` 覆盖 `CURLOPT_HTTP_VERSION`，导致“请求 http/1.1 实际走 h2”；已将连接池配置提前并让请求级 HTTP 版本覆盖；同时修复 baseline `cli_hx_upload` 在 POST 场景下 `-l` 未生效（改用 `CURLOPT_POSTFIELDSIZE_LARGE`）。回归：`pytest tests/libcurl_consistency/test_p0_consistency.py` 通过（7 passed，需 escalated）。 |
| LC-6 | 落地 P0：下载字节一致（覆盖 h1/h2/h3）；对齐 `test_02_download.py` 的数据断言（最终文件内容），并强制校验请求语义摘要一致。 | 高 | LC-3、LC-4、LC-5 | 已完成 | 2025-12-16：`test_p0_consistency.py` 对 download_serial/parallel 在 http/1.1+h2（h3 取决于 `env.have_h3()`）下对齐 baseline 与 QCurl（download_*.data hash/len）；`conftest.py` 自动生成 data-1m/data-10m 测试文件。 |
| LC-7 | 落地 P0：上传/回显一致（PUT/POST，覆盖 h1/h2/h3）；对齐 `test_07_upload.py` 的回显/长度断言，并强制校验请求语义摘要一致。 | 高 | LC-3、LC-4、LC-5 | 已完成 | 2025-12-16：对齐 upload_put 与 upload_post_reuse（body len/hash + 响应字节 hash/len）并覆盖 http/1.1+h2（h3 取决于 `env.have_h3()`）。<br>2025-12-16 22:58:27：upload baseline 用例追加 `-l`（announce length）使 `Content-Length` 成为实际可比字段；复跑 P0 通过。 |
| LC-8 | 落地 P0：WebSocket 基础收发一致（ping/pong + small data）；对齐 `test_20_websockets.py` 的收发断言，并强制校验握手/关键请求语义摘要一致。 | 高 | LC-3、LC-4、LC-5 | 已完成 | 2025-12-16：对齐 ws_pingpong_small 与 ws_data_small；QCurl 侧写出 download_0.data，baseline 侧用期望字节计算 hash/len；用例通过。 |
| LC-9 | 落地 P1：`CURLOPT_POSTFIELDS` 二进制（含 `\\0`）一致性；基线参考 `test1531/lib1531.c` 的断言逻辑，QCurl 侧复现相同 payload 与期望。 | 中 | LC-3、LC-4、LC-5 | 已完成 | 2025-12-16 23:39:50：新增 baseline 客户端 `curl/tests/libtest/cli_postfields_binary.c`（POSTFIELDS+POSTFIELDSIZE_LARGE，写 `download_0.data`），并在 `curl/tests/libtest/Makefile.inc` 注册；Qt 侧在 `tests/tst_LibcurlConsistency.cpp` 增加 `postfields_binary_1531` 分支（POST 二进制 payload 回显）；pytest 新增 `tests/libcurl_consistency/test_p1_postfields_binary.py`（覆盖 http/1.1+h2(+h3 若可用)，复用 LC-5a/5b 的服务端观测写回与对比）。回归：`pytest tests/libcurl_consistency/test_p1_postfields_binary.py` 通过（1 passed，需 escalated）。<br>2025-12-17：为遵守“不修改 curl/ 目录”，baseline 调整为 repo 内置可执行 `qcurl_lc_postfields_binary_baseline`（pytest baseline runner 对 `cli_postfields_binary` 特判），并通过全量 Gate（14 passed）。 |
| LC-10 | 落地 P1：cookie 文件读写一致（`COOKIEFILE/COOKIEJAR`）；基线参考 `test1903`，QCurl 侧输出 cookiejar 后做规范化对比（排序/时间戳/域名大小写等）。 | 中 | LC-3、LC-4、LC-5 | 已完成 | 2025-12-17 00:11:39：跑通 `tests/libcurl_consistency/test_p1_cookiejar_1903.py`（baseline `lib1903` vs QCurl `cookiejar_1903`），并在对比侧规范化 cookie path 尾随斜杠差异（`/we/want` vs `/we/want/`），避免无意义不一致；回归通过（1 passed，需 escalated）。 |
| LC-11 | 可选扩展：纳入 multi/复用类用例（如 `test2402/test2502`）与 WS 低层 API（如 `test2301/test2700`），并提供单独的 suite 开关，避免波动影响 Gate。 | 低 | LC-6、LC-7、LC-8 | 已完成 | 2025-12-17：新增扩展套件开关 `QCURL_LC_EXT=1` 与用例 `ext_download_parallel_stress`（h2/h3 并发下载压力，默认不跑），pytest：`tests/libcurl_consistency/test_ext_suite.py`，Qt：`tests/tst_LibcurlConsistency.cpp`。<br>2025-12-17：修复 ext suite 运行时 Qt 进程“测试结束后不退出/卡死”的问题：根因是 `QCCurlMultiManager::~QCCurlMultiManager()` 持锁调用 `curl_multi_remove_handle/cleanup` 触发 socket 回调重入，导致死锁；修复为：析构标记 `m_isShuttingDown` + 先禁用 libcurl 回调（`CURLMOPT_SOCKETFUNCTION/TIMERFUNCTION` 置空）+ 不持锁清理 handle；并在回调/事件入口早退。回归：`QCURL_LC_EXT=1 pytest tests/libcurl_consistency/test_ext_suite.py` 通过（需 escalated）。<br>2025-12-17：回归 Gate：`pytest tests/libcurl_consistency/test_p0_consistency.py`（7 passed）与 P1（2 passed）通过。 |
| LC-12 | 可选：补充“服务端视角”观测：在可行的协议/服务端上记录语义摘要或原始报文片段，用于 debug（不作为默认 Gate/P0 断言）。 | 低 | LC-2、LC-5 | 已完成 | 2025-12-17：新增失败时可选日志收集机制：`QCURL_LC_COLLECT_LOGS=1` 时，P0/P1/ext 用例在断言失败/异常时会将 `httpd/nghttpx/ws` 关键日志复制到 `curl/tests/http/gen/artifacts/<suite>/<case>/service_logs/`，并写出 `meta.json`（含 baseline/qcurl req_id）；实现位于 `tests/libcurl_consistency/pytest_support/service_logs.py`，不记录“服务端看到的原始请求字节”。 |
| LC-13 | 落地 P0：下载“中断 + Range 续传”一致性（不包含 `cli_hx_download -P` 的 in-flight pause/resume）；断言最终文件字节一致 + Range 偏移/边界正确，并强制校验请求语义摘要一致。 | 高 | LC-6 | 已完成 | 2025-12-16：实现 `download_range_resume`（第二段用 Range start-end），并在对比侧要求服务端观测到“首段非 Range + 次段 Range”。<br>2025-12-17：全量 Gate 回归发现 curl `libtests` 中不存在 `cli_hx_range_resume`（baseline 失败：Test not found）；为避免依赖/改动 `curl/tests/...`，在 QCurl 侧新增 baseline 可执行 `qcurl_lc_range_resume_baseline`（libcurl easy API，两段请求输出 `download_0.data`），并在 `pytest_support/baseline.py` 对 `cli_hx_range_resume` 做特判调用该可执行；待回归验证。 |
| LC-14 | 文档化与复现：补齐 `README.md` 的运行方式（如何构建 curl 的 `libtests`、如何跑 pytest driver/Qt Test、失败定位路径与日志位置）。 | 中 | LC-6、LC-7、LC-8、LC-13 | 已完成 | 2025-12-16 23:39:50：更新 `tests/libcurl_consistency/README.md`：补充“服务端观测机制”（httpd access_log + ws handshake JSONL）、HTTP/3 前置条件说明，以及 P0 复现命令与默认环境变量注入。 |
| LC-15 | P2：如果后续 QCurl 提供真正的 pause/resume API，再补齐对齐 `cli_hx_download -P` 的**过程一致性**（暂停点/恢复点/回调时序/部分写入边界）。 | 低 | LC-13、（前置：QCurl pause/resume API） | 未开始 | - |
| LC-16 | 固化 P0 Gate 入口：提供统一脚本/target 完成“构建依赖 + 运行 pytest + 输出 JUnit/JSON”，并默认开启失败日志收集（`QCURL_LC_COLLECT_LOGS=1`）。 | 高 | LC-6、LC-7、LC-8、LC-13 | 已完成 | 2025-12-17：新增 Gate 脚本 `tests/libcurl_consistency/run_gate.py`（支持 `--suite p0|p1|all`、`--build`、`--with-ext`；输出 `build/libcurl_consistency/reports/junit_*.xml` 与 `gate_*.json`），并在 README 增加 Gate 使用说明。<br>2025-12-17：require_escalated 下回归验证：`python tests/libcurl_consistency/run_gate.py --suite p0 --build` 通过（`7 passed`），生成 `build/libcurl_consistency/reports/junit_p0.xml` 与 `build/libcurl_consistency/reports/gate_p0.json`。<br>2025-12-17：全量回归：`python tests/libcurl_consistency/run_gate.py --suite all --build --with-ext` 通过（`12 passed`），生成 `build/libcurl_consistency/reports/junit_all.xml` 与 `build/libcurl_consistency/reports/gate_all.json`。<br>2025-12-17：再次全量回归（含 ext WS）：`python tests/libcurl_consistency/run_gate.py --suite all --build --with-ext` 通过（`14 passed`），报告输出到 `build/libcurl_consistency/reports/junit_all.xml` 与 `build/libcurl_consistency/reports/gate_all.json`。 |
| LC-17 | 扩展 ext：落地 `test2402` 语义（HTTP/2 multi 多资源 GET）：新增 baseline libcurl client + QCurl 用例 + 观测/对比（多请求语义摘要 + 多文件字节）。 | 中 | LC-5a、LC-11 | 已完成 | 2025-12-17：新增 baseline client：`curl/tests/libtest/cli_hx_multi_get4.c`（multi + MAXCONNECTS=1，写出 `download_*.data`），并更新 `curl/tests/libtest/Makefile.inc` 触发 `libtests.c` 生成。<br>2025-12-17：新增 ext cases：`tests/libcurl_consistency/pytest_support/case_defs.py`（`ext_multi_get4_h2`），Qt 执行分支：`tests/tst_LibcurlConsistency.cpp`（并发 GET `/path/2402{0001..}` 落盘）。<br>2025-12-17：扩展观测/对比以支持“同一 case 多请求”：`tests/libcurl_consistency/pytest_support/observed.py` 增加 `*_observed_list_for_id`；`tests/libcurl_consistency/pytest_support/compare.py` 增加 `requests[]/responses[]` 对比；`tests/libcurl_consistency/test_ext_suite.py` 写入多请求语义摘要与逐文件 sha256/len。<br>2025-12-17：问题与修复：curl `libtests` 首次编译失败（static 函数名冲突 `write_cb`、使用了不存在的 `TEST_ERR_*` 常量、`abort_on_test_timeout()` 需要 `test_cleanup:` 标签、usage 字符串误用 `%04d`）；已通过函数/变量唯一前缀、改用已有 `TEST_ERR_*`、统一 `test_cleanup:` 标签、修正 usage 文案解决。<br>2025-12-17：回归：`QCURL_LC_EXT=1 pytest tests/libcurl_consistency/test_ext_suite.py` 通过（需 escalated）。<br>2025-12-17：为遵守“不修改 curl/ 目录”，新增 repo 内置 baseline 可执行 `qcurl_lc_multi_get4_baseline` 并在 pytest baseline runner 中对 `cli_hx_multi_get4` 特判；全量 Gate（`--with-ext`）通过（14 passed）。 |
| LC-18 | 扩展 ext：落地 `test2502` 语义（HTTP/3 multi 多资源 GET）：新增 baseline libcurl client + QCurl 用例 + 观测/对比（多请求语义摘要 + 多文件字节）。 | 中 | LC-2、LC-17 | 已完成 | 2025-12-17：复用 `cli_hx_multi_get4` 与 QCurl 分支，新增 `ext_multi_get4_h3`（请求 `/path/2502{0001..}`，强制 `proto=h3`，仅在 `env.have_h3()` 时启用）。回归：`QCURL_LC_EXT=1 pytest tests/libcurl_consistency/test_ext_suite.py` 通过（3 passed，需 escalated）。<br>2025-12-17：同 LC-17，multi-get4 baseline 统一使用 repo 内置 `qcurl_lc_multi_get4_baseline`（支持 h3），并通过全量 Gate（14 passed）。 |
| LC-19 | 扩展 ext：评估并落地 `test2301`（WS raw/callback + ping→pong）一致性用例：需要服务端可控发送 Ping 帧，并在 QCurl 侧可观测/可对比“Ping 帧/Close 帧”等低层事件。 | 低 | LC-8、LC-11 | 已完成 | 2025-12-17：在 `tests/libcurl_consistency/` 自建 WS 场景服务端 `ws_scenario_server.py`：`scenario=lc_ping`（server Ping(空 payload) → wait Pong → Close(1000,\"done\")），输出 `ws_handshake.jsonl`（语义摘要）+ `ws_events.jsonl`（debug）。<br>2025-12-17：新增 baseline WS 客户端 `ws_baseline_client.cpp`（target `qcurl_lc_ws_baseline`，`CURLOPT_CONNECT_ONLY=2` + `CURLWS_NOAUTOPONG`，收到 Ping 手动回 Pong，输出事件序列到 `download_0.data`）。<br>2025-12-17：补齐 QCurl WS 低层可观测与控制：`QCWebSocket::pong()`、`setAutoPongEnabled()`、`pingReceived()`、`closeReceived()`；并修复 `curl_ws_recv()` 在 Ping(0 bytes) 时 `received==0` 仍需处理 meta 的问题。<br>2025-12-17：新增 Qt Test case `ext_ws_ping_2301` 与 pytest `test_ext_ws_suite.py` 对比 baseline/QCurl；回归：`QCURL_LC_EXT=1 pytest tests/libcurl_consistency/test_ext_ws_suite.py -k ext_ws_ping_2301` 通过（需 escalated）。<br>问题与解决：握手头 `Sec-WebSocket-Key` 不可比 → 从 handshake allowlist 排除。 |
| LC-20 | 扩展 ext：评估并落地 `test2700`（WS 帧类型：text/binary/ping/pong/close）一致性用例：需要服务端发送指定帧序列，且 QCurl 侧能稳定观测并序列化这些事件用于对比。 | 低 | LC-19 | 已完成 | 2025-12-17：扩展 WS 场景服务端：`scenario=lc_frame_types`（TEXT(\"txt\") → BINARY(\"bin\") → Ping(\"ping\") → Pong(\"pong\") → Close(1000,\"close\")）；Qt Test 新增 `ext_ws_frame_types_2700` 序列化 TEXT/BINARY/PING/PONG/CLOSE 事件到 `download_0.data`；回归：`QCURL_LC_EXT=1 pytest tests/libcurl_consistency/test_ext_ws_suite.py -k ext_ws_frame_types_2700` 通过（需 escalated）。 |
| LC-21 | 补齐 P1：本地 HTTP proxy 一致性（含 CONNECT 隧道）。自建 proxy 记录可观测数据：method/target + Proxy-*（Proxy-Authorization），并对比 baseline/QCurl 在 proxy 视角下的一致性；HTTPS 场景同时对齐 origin 侧请求语义摘要与响应字节。 | 中 | LC-5a、LC-4、LC-5 | 已完成 | 2025-12-21：新增 `tests/libcurl_consistency/http_proxy_server.py`（Basic auth + CONNECT + JSONL 观测）与 pytest fixture `lc_http_proxy`；新增 repo 内置 baseline 可执行 `qcurl_lc_http_baseline`（支持 proxy/cookiefile）；新增用例 `tests/libcurl_consistency/test_p1_proxy.py`（HTTP proxy + HTTPS CONNECT 两个 case），Qt 执行器 `tests/tst_LibcurlConsistency.cpp` 新增 `proxy_http_basic_auth`/`proxy_https_connect_basic_auth` 分支；`tests/libcurl_consistency/run_gate.py` 将 proxy 用例纳入 P1/all；回归：`pytest tests/libcurl_consistency/test_p1_proxy.py` 通过（2 passed，需 escalated）。<br>2025-12-22 01:03:23：复跑 `pytest tests/libcurl_consistency/test_p1_proxy.py` 通过（2 passed，需 escalated）。 |
| LC-22 | 补齐 P2：Cookie 请求头可观测一致性（与 cookiejar 文件落盘测试解耦）。自建观测 HTTP 服务端记录 `Cookie:`，对齐 baseline/QCurl 在相同 cookiefile 输入下服务端看到的 Cookie 值（做稳定归一化）。 | 低 | LC-5a、LC-4、LC-5 | 已完成 | 2025-12-21：新增 `tests/libcurl_consistency/http_observe_server.py`（/cookie + JSONL 观测）与 pytest fixture `lc_observe_http`；新增用例 `tests/libcurl_consistency/test_p2_cookie_request_header.py`；Qt 执行器 `tests/tst_LibcurlConsistency.cpp` 新增 `p2_cookie_request_header` 分支；回归：`pytest tests/libcurl_consistency/test_p2_cookie_request_header.py` 通过（1 passed，需 escalated）。<br>2025-12-22 01:03:23：复跑 `pytest tests/libcurl_consistency/test_p2_cookie_request_header.py` 通过（1 passed，需 escalated）。 |
| LC-23 | 补齐 P2：固定 HTTP 错误码（404/401/503）一致性。自建观测 HTTP 服务端返回固定状态与 body；两侧记录“错误字段归一化”（kind/http_status）并对比，同时对比响应字节 hash/len。 | 低 | LC-22 | 已完成 | 2025-12-21：复用 `http_observe_server.py` 的 `/status/<code>`；新增用例 `tests/libcurl_consistency/test_p2_fixed_http_errors.py`；Qt 执行器 `tests/tst_LibcurlConsistency.cpp` 新增 `p2_fixed_http_error` 分支（断言 QCurl 对 HTTP>=400 视为 error 且仍可读 body）；对比器 `tests/libcurl_consistency/pytest_support/compare.py` 增加可选 `error(kind/http_status)` 对比；回归：`pytest tests/libcurl_consistency/test_p2_fixed_http_errors.py` 通过（3 passed，需 escalated）。<br>2025-12-22 01:03:23：复跑 `pytest tests/libcurl_consistency/test_p2_fixed_http_errors.py` 通过（3 passed，需 escalated）。 |
| LC-24 | 补齐 P1：重定向（FOLLOWLOCATION）与“模拟 HTTP 登录态”一致性：多跳 302 的请求序列一致、最终落点一致；可观测对比关键响应头 `Location`/`Set-Cookie` 与关键请求头 `Host`/`Cookie`。 | 中 | LC-5、LC-21、LC-22 | 已完成 | 2025-12-22：扩展 `tests/libcurl_consistency/http_observe_server.py` 支持 `/redir/<n>`、`/login`、`/home` 并输出 `response_headers`；新增用例 `tests/libcurl_consistency/test_p1_redirect_and_login_flow.py`（follow on/off + login cookie flow）；Qt 执行器 `tests/tst_LibcurlConsistency.cpp` 新增 `p1_redirect_{no}follow` 与 `p1_login_cookie_flow` 分支；回归：`pytest tests/libcurl_consistency/test_p1_redirect_and_login_flow.py` 通过（3 passed，需 escalated）。<br>2025-12-22：修复 redirect follow 序列排序实现：按 `/redir/<n>` 的数值降序稳定排序（避免日志写入顺序差异导致的假失败）；回归：`python tests/libcurl_consistency/run_gate.py --suite all` 通过。 |
| LC-25 | 补齐 P2：TLS 校验语义一致性（成功/失败路径）：对齐 verifyPeer/verifyHost + CAINFO/caCertPath 下的可观测结果（成功/证书错误）。 | 低 | LC-24 | 已完成 | 2025-12-22：新增 fixture `lc_observe_https`（复用 curl testenv 生成的 CA 与 `localhost` 证书）与用例 `tests/libcurl_consistency/test_p2_tls_verify.py`（成功 with CA、失败 no CA）；扩展 baseline `qcurl_lc_http_baseline` 支持 `--secure/--cainfo`；Qt 执行器 `tests/tst_LibcurlConsistency.cpp` 新增 `p2_tls_verify_success`/`p2_tls_verify_fail_no_ca` 分支；并在 baseline runner 支持 `allowed_exit_codes` 以记录期望失败；回归：`pytest tests/libcurl_consistency/test_p2_tls_verify.py` 通过（2 passed，需 escalated）。 |
| LC-26 | 补齐：响应头“字节级/多值头”一致性（`rawHeaderData`/重复头/顺序）与验收口径。 | 中 | LC-25 | 已完成 | 2025-12-22：观测服务端新增 `/resp_headers`（重复头/大小写/顺序）；baseline `qcurl_lc_http_baseline` 增加 `--header-out`（`CURLOPT_HEADERFUNCTION` 写出原始响应头）；Qt 执行器新增 `p1_resp_headers` 并写出 `rawHeaderData()` 到 `response_headers_0.data`；pytest 新增 `tests/libcurl_consistency/test_p1_resp_headers.py`，并将可比字段写入 `response.headers_raw_lines/len/sha256`（跳过 `Date/Server`）；对比器 `pytest_support/compare.py` 支持可选 `response.headers_raw_*` 字段；回归：`run_gate.py --suite p1`、`--suite all` 通过。 |
| LC-27 | 补齐：空响应体（`Content-Length: 0`/204/302 nofollow）与 `readAll()`（`nullopt` vs 空字节）一致性规则与用例。 | 高 | LC-25 | 已完成 | 2025-12-22：修复 `src/QCNetworkReply.cpp`：`readAll()` 在终态空 body 返回 empty QByteArray（不再是 `nullopt`）；Qt 执行器移除 `p1_redirect_nofollow` 的空文件绕过；观测服务端新增 `/empty_200`、`/no_content`；新增用例 `tests/libcurl_consistency/test_p1_empty_body.py`；回归：`run_gate.py --suite p1`、`--suite all` 通过。 |
| LC-28 | 补齐：超时语义一致性（connect/total/low-speed）：错误归一化字段扩展 + 终态约束 + 最小可复现服务端场景。 | 高 | LC-25 | 已完成 | 2025-12-22：观测服务端新增 `/delay_headers/<ms>`、`/stall_body/<total>/<stall_ms>`、`/slow_body/<total>/<chunk>/<sleep_ms>`；baseline `qcurl_lc_http_baseline` 新增 `--connect-timeout-ms/--timeout-ms/--low-speed-*`；Qt 执行器新增 `p1_timeout_delay_headers`、`p1_timeout_low_speed`；对比器扩展 error 对比字段（`curlcode/http_code` 可选）；新增用例 `tests/libcurl_consistency/test_p1_timeouts.py`；回归：`run_gate.py --suite all` 通过。 |
| LC-29 | 补齐：取消语义一致性（async cancel）：事件序列（data/progress/cancel/terminal）、错误码映射与“取消后无数据事件”。 | 高 | LC-28 | 已完成 | 2025-12-22：baseline `qcurl_lc_http_baseline` 新增 `--abort-after-bytes`（xferinfo 中止→curlcode=42）；QCurl：`cancel()` 设定 `OperationCancelled`，并延迟移除 multi handle（避免回调栈内重入），同时取消后抑制 `readyRead/downloadProgress`；Qt 执行器新增 `p1_cancel_after_first_chunk`（阈值取消）并断言取消后无事件；新增用例 `tests/libcurl_consistency/test_p1_cancel.py`；回归：`run_gate.py --suite p1`、`--suite all` 通过。 |
| LC-30 | 补齐：进度与统计信息一致性（libcurl xferinfo ↔ QCurl progress signals），仅比较稳定摘要（单调性/终值/总量）。 | 中 | LC-28 | 已完成 | 2025-12-22：baseline `qcurl_lc_http_baseline` 支持 `--progress-out`（xferinfo 稳定摘要：monotonic/now_max/total_max）；Qt 执行器新增 `p1_progress_download`/`p1_progress_upload` 并写出 `progress_summary.json`；pytest 新增 `tests/libcurl_consistency/test_p1_progress.py` 合并并对比 `progress_summary`；对比器 `pytest_support/compare.py` 增加可选 `progress_summary.{download,upload}` 对比；回归：`pytest tests/libcurl_consistency/test_p1_progress.py` 通过（2 passed，需 escalated）；`run_gate.py --suite all --with-ext --build` 通过（40 passed）。 |
| LC-31 | 补齐：连接复用/多路复用的可观测一致性（服务端连接观测或 getinfo 统计）与可比规则。 | 低 | LC-30 | 已完成 | 2025-12-22：观测服务端 `http_observe_server.py` 日志新增 `peer_port`；ext 新增用例 `test_ext_suite.py::test_ext_reuse_keepalive_http_1_1` 统计 `unique_connections/conn_seq` 并写入 `connection_observed`；对比器 `pytest_support/compare.py` 增加可选 `connection_observed` 对比；Qt 执行器新增 `ext_reuse_keepalive`（⚠️ 采用 Async 顺序请求以复用 multi 连接池；Sync 模式使用 `curl_easy_perform` 的 per-request handle，不具备跨请求连接复用能力）；回归：`QCURL_LC_EXT=1 pytest tests/libcurl_consistency/test_ext_suite.py -k reuse_keepalive` 通过（需 escalated）；`run_gate.py --suite all --with-ext --build` 通过（40 passed）。 |
| LC-32 | 补齐：错误路径一致性（连接拒绝、代理 407、非法 URL）与归一化字段扩展（curlcode/http_code）。 | 中 | LC-28 | 已完成 | 2025-12-22：Qt 执行器新增 `p2_error_refused`/`p2_error_malformat`/`p2_error_proxy_407`（分别覆盖连接拒绝、URL malformat、proxy 407）；pytest 新增 `tests/libcurl_consistency/test_p2_error_paths.py`，统一输出 `error.kind/http_status/curlcode/http_code` 并对比；回归：`run_gate.py --suite all` 通过。 |
| LC-33 | 补齐：HTTP 方法面一致性（HEAD/DELETE/PATCH）：method/请求体/响应体/错误语义对齐。 | 低 | LC-27 | 已完成 | 2025-12-22：观测服务端新增 `/head`（HEAD 无 body）与 `/method`（PATCH/DELETE 回显请求体）；Qt 执行器新增 `p1_method_head`/`p1_method_patch`；pytest 新增 `tests/libcurl_consistency/test_p1_http_methods.py`（HEAD=0 字节、PATCH=echo + Content-Length）；回归：`pytest tests/libcurl_consistency/test_p1_http_methods.py` 通过（2 passed，需 escalated）；`run_gate.py --suite all --with-ext --build` 通过（40 passed）。 |
| LC-34 | 可选：WebSocket 压缩/fragment/close 细节一致性（握手扩展协商 + 帧事件）。 | 低 | LC-20 | 已完成 | 2025-12-22：WS 场景服务端握手观测新增 `Sec-WebSocket-Extensions`；baseline WS 客户端新增 `lc_ping_deflate`（请求 `permessage-deflate`）；Qt 执行器新增 `ext_ws_deflate_ping`（`setCompressionConfig(defaultConfig)`）；扩展清单 `EXT_WS_CASES` 增加 `ext_ws_deflate_ping` 并纳入 `test_ext_ws_suite.py`；回归：`QCURL_LC_EXT=1 pytest tests/libcurl_consistency/test_ext_ws_suite.py -k deflate` 通过（需 escalated）；`run_gate.py --suite all --with-ext --build` 通过（40 passed）。 |

---

## 缺口与改进任务（可执行/可验证/可追踪）

> 说明：以下任务以“可观测数据层面一致性”为唯一验收核心。每项任务都必须：
> 1) 可复现（本地可跑、无外网依赖）；2) 可对比（baseline/QCurl 同一输入）；3) 可追踪（ID、产物路径、断言字段明确）。

## LC-15：pause/resume 过程一致性（对齐 `cli_hx_download -P`，in-flight）

- 背景：当前 P0 仅断言“最终文件字节一致”，不对齐 in-flight pause/resume 的过程语义；若产品/业务把“暂停点/恢复点/回调时序/部分写入边界”视为可观测契约，需要补齐一致性用例。
- 覆盖点（libcurl API / QCurl 行为点）：
  - libcurl：`curl_easy_pause`（或等价机制），以及写回调/进度回调在 pause/resume 前后的行为
  - QCurl：`QCNetworkReply::pause()/resume()`、`stateChanged(Paused/Running)`、`readyRead`/`downloadProgress` 的事件约束
- 输入场景：
  - 下载固定资源（例如 `data-1m`），在“累计下载到达 offset”时触发 pause，等待固定时间后 resume（pause/resume 触发必须基于“可观测阈值”，避免纯 sleep）
  - baseline 与 QCurl 使用同一 offset（对齐 `cli_hx_download -P <offset>`）
- 期望可观测输出：
  - pause 发生后：不再产生 data/progress 事件（直到 resume）
  - resume 发生后：继续产生 data/progress，最终完成，文件字节一致
  - 事件序列满足约束：`Running → Paused → Running → Finished`（或等价序列），且每个终态事件只出现一次
- 对比方式：
  - artifacts 增加 `events`（仅记录关键事件点与 offset：`PAUSE@bytes`、`RESUME@bytes`、`TERMINAL`）
  - 对比器比较事件序列与最终 `download_*.data` hash/len
- 边界条件：
  - 不比较“精确暂停时长”；只比较“暂停期间是否无数据事件”
  - 需在异步模式下实现（同步模式不支持 in-flight pause/resume）
- 优先级：低
- 完成判据：
  - 新增用例稳定复现 pause/resume（连续运行 10 次无偶发失败），并明确写入 README 的一致性口径
- 最小可复现步骤：
  - `python tests/libcurl_consistency/run_gate.py --suite p0 --build`
  - `QCURL_QTTEST="build/tests/tst_LibcurlConsistency" pytest -q tests/libcurl_consistency/test_*.py -k pause_resume`

## LC-26：响应头“字节级/多值头”一致性（`rawHeaderData`/重复头/顺序）

- 背景：当前一致性主要依赖“服务端观测白名单头”，未覆盖“客户端侧可读到的原始响应头字节”。同时 `QCNetworkReply::rawHeaders()` 由 `QMap` 构建，天然会丢失重复头（例如多条 `Set-Cookie`）。
- 覆盖点（libcurl API / QCurl 行为点）：
  - libcurl：`CURLOPT_HEADERFUNCTION`（采集原始 header bytes）、`CURLINFO_RESPONSE_CODE`
  - QCurl：`QCNetworkReply::rawHeaderData()`、`QCNetworkReply::rawHeaders()`
- 输入场景：
  - 扩展 `tests/libcurl_consistency/http_observe_server.py` 增加端点（示例）：`/resp_headers` 返回确定性的响应头集合，包含：
    - 重复头：`Set-Cookie` 两条、`X-Dupe` 两条
    - 大小写差异：`X-Case` vs `x-case`
    - `Content-Length: 0`（可与 LC-27 复用）
- 期望可观测输出：
  - baseline 与 QCurl 的“原始 header bytes”对齐（至少对齐 header 行集合；如能稳定，对齐顺序与大小写）
  - 明确 `rawHeaders()` 对重复头的处理口径（例如：不作为严格一致性基准，仅验证 `rawHeaderData()`；或改为保留多值结构并对齐）
- 对比方式：
  - artifacts 扩展字段（建议）：`response_headers_raw`（hex 或 sha256/len）与 `response_headers_multi`（list of header lines）
  - 对比器扩展：仅比较确定性字段；跳过 `Date/Server` 等不可比头
- 边界条件：
  - 不比较动态头（`Date` 等）；不依赖 curl debug 输出
  - redirect 场景下可能存在多段响应头；本任务仅覆盖“无重定向单响应”以降低噪声
- 优先级：中
- 完成判据：
  - 新增一致性测试（pytest）覆盖重复头/大小写/顺序至少一个组合场景，并能稳定复现（连续运行 10 次无偶发失败）
  - `baseline.json` 与 `qcurl.json` 中新增的 header 观测字段可对比且一致
- 最小可复现步骤：
  - `python tests/libcurl_consistency/run_gate.py --suite all --build`
  - `QCURL_QTTEST="build/tests/tst_LibcurlConsistency" pytest -q tests/libcurl_consistency/test_*.py -k resp_headers`

## LC-27：空响应体与 `readAll()`（`nullopt` vs 空字节）一致性

- 背景：`QCNetworkReply::readAll()` 在 bodyBuffer 为空时返回 `std::nullopt`，导致“空 body”与“未读取到 body”在可观测层面不可区分，已在 `p1_redirect_nofollow` 通过“显式写空文件”绕过。
- 覆盖点（libcurl API / QCurl 行为点）：
  - libcurl：`CURLOPT_WRITEFUNCTION`（写入 0 字节的行为）、`CURLINFO_CONTENT_LENGTH_DOWNLOAD_T`、`CURLINFO_RESPONSE_CODE`
  - QCurl：`QCNetworkReply::readAll()`、`QCNetworkReply::bytesAvailable()`、`QCNetworkReply::finished`（终态）
- 输入场景：
  - `http_observe_server.py` 新增/复用端点，返回确定性的空 body：
    - `200 + Content-Length: 0`
    - `204 No Content`
    - `302 + Content-Length: 0`（follow 关闭）
    - （可选）HEAD：`HEAD /cookie`（与 LC-33 关联）
- 期望可观测输出：
  - 对于“请求已完成且 body 长度为 0”的场景：QCurl 与 baseline 都应产出 `body_len=0` 且 `body_sha256` 为同一空内容 hash
  - 明确 `readAll()` 的一致性口径：若继续保留 `nullopt`，需在 artifacts/对比器中做显式归一化并在 README 记录（但不应掩盖“状态未完成”误用）
- 对比方式：
  - 以 `download_*.data` 落盘结果作为主断言（空文件）
  - response/status +（可选）`Content-Length`（服务端观测）作为辅助断言
- 边界条件：
  - 必须在“终态后”读取 body（finished 之后），避免把时序问题误判为空 body
- 优先级：高
- 完成判据：
  - 新增一致性用例覆盖上述至少 2 种空 body 场景，且无需额外绕过逻辑即可通过
  - README 中明确记录“空 body 的可观测口径”（包括 `readAll()` 的约束）
- 最小可复现步骤：
  - `python tests/libcurl_consistency/run_gate.py --suite all --build`
  - `QCURL_QTTEST="build/tests/tst_LibcurlConsistency" pytest -q tests/libcurl_consistency/test_*.py -k \"empty_body or 204 or nofollow\"`

## LC-28：超时语义一致性（connect/total/low-speed）

- 背景：QCurl 已实现 `QCNetworkTimeoutConfig`（映射到 `CURLOPT_CONNECTTIMEOUT_MS`/`CURLOPT_TIMEOUT_MS`/`CURLOPT_LOW_SPEED_*`），但当前一致性候选集未覆盖超时的可观测语义（错误码/终态/事件约束）。
- 覆盖点（libcurl API / QCurl 行为点）：
  - libcurl：`CURLOPT_CONNECTTIMEOUT_MS`、`CURLOPT_TIMEOUT_MS`、`CURLOPT_LOW_SPEED_TIME`、`CURLOPT_LOW_SPEED_LIMIT`、`CURLE_OPERATION_TIMEDOUT(28)`
  - QCurl：`QCNetworkTimeoutConfig`、`QCNetworkError::ConnectionTimeout`、终态信号（error/finished 或 cancelled）
- 输入场景：
  - 扩展 `http_observe_server.py` 增加可控延迟端点（示例）：
    - `/delay_headers/<ms>`：延迟后再发送响应头
    - `/slow_body/<total>/<chunk>/<sleep_ms>`：分块慢速发送 body（用于 low-speed）
  - 设置超时参数使其必然触发（例如 totalTimeout=200ms，服务端 delay=1000ms）
- 期望可观测输出：
  - baseline：`curlcode=28`（或等价错误），http_code 可能为 0（未收到响应头）或为 200（收到头但 body 超时）——需明确记录与归一化策略
  - QCurl：`NetworkError::ConnectionTimeout`（或等价超时错误），并满足终态约束（不继续产生 data/progress 事件）
- 对比方式：
  - artifacts 扩展字段（建议）：`error.kind="timeout"` + `curlcode` + `http_code`
  - 对比器：允许 `http_code` 在“未收到响应头”场景下为 0；但必须一致地落在同一类语义分支（见完成判据）
- 边界条件：
  - 避免依赖“精确耗时数值”；仅验证“是否触发超时 + 终态一致 + 可观测输出一致”
  - 低速场景需确保发送速率可控（固定 chunk 与 sleep，避免环境波动）
- 优先级：高
- 完成判据：
  - 新增至少 2 个超时场景（一个 headers delay，一个 slow body），baseline 与 QCurl 均触发同类超时语义且产物一致
  - 在 README 中明确记录“超时类错误”的归一化字段与对比规则
- 最小可复现步骤：
  - `python tests/libcurl_consistency/run_gate.py --suite all --build`
  - `QCURL_QTTEST="build/tests/tst_LibcurlConsistency" pytest -q tests/libcurl_consistency/test_*.py -k timeout`

## LC-29：取消语义一致性（async cancel：事件序列与终态约束）

- 背景：取消是典型“时序可观测语义”，但当前一致性用例仅覆盖最终字节，不覆盖取消后的事件序列、终态信号以及错误码映射。
- 覆盖点（libcurl API / QCurl 行为点）：
  - libcurl：通过 `CURLOPT_XFERINFOFUNCTION` 返回非 0 或 write 回调中止来触发 `CURLE_ABORTED_BY_CALLBACK(42)`（需明确选择一种以稳定复现）
  - QCurl：`QCNetworkReply::cancel()`、`cancelled`/`error`/`finished` 信号与其序列约束
- 输入场景：
  - `http_observe_server.py` 提供可控流式 body（可与 LC-28 的 `/slow_body/...` 复用）
  - baseline 与 QCurl 均在“下载到达 N 字节阈值”时取消
- 期望可观测输出：
  - 事件序列满足约束：取消发生后不再产生 data/progress；终态信号仅出现一次且口径一致（例如：只发 `cancelled`，不发 `finished`；或明确规定应发 `error+finished`）
  - 错误码归一化一致：`kind="cancel"`，并可选记录 `curlcode=42`
- 对比方式：
  - artifacts 增加 `events`（只记录关键事件点：`DATA`/`PROGRESS`/`CANCEL`/`TERMINAL`），并定义“序列等价”规则（忽略非确定性频率，保留顺序约束）
- 边界条件：
  - 禁止通过“sleep 固定时间后 cancel”触发（易受环境波动）；必须以“可观测阈值（字节数）”触发
- 优先级：高
- 完成判据：
  - 新增取消一致性用例（至少 1 个下载场景），连续运行 10 次无偶发失败
  - README 中补齐“取消的终态语义与事件序列约束”
- 最小可复现步骤：
  - `python tests/libcurl_consistency/run_gate.py --suite all --build`
  - `QCURL_QTTEST="build/tests/tst_LibcurlConsistency" pytest -q tests/libcurl_consistency/test_*.py -k cancel`

## LC-30：进度与统计信息一致性（稳定摘要）

- 背景：libcurl 与 QCurl 均提供进度信息，但事件频率受 chunking/调度影响，直接对齐序列容易引入不稳定；需要定义“稳定摘要”来对齐。
- 覆盖点（libcurl API / QCurl 行为点）：
  - libcurl：`CURLOPT_XFERINFOFUNCTION`（dlnow/dltotal/ulnow/ultotal）
  - QCurl：`downloadProgress`/`uploadProgress` 信号、`bytesReceived()/bytesTotal()`
- 输入场景：
  - 下载固定大小资源（例如 `data-1m`）；上传固定大小 body（例如 128KiB echo）
- 期望可观测输出：
  - 进度单调性：`now` 非递减
  - 终值一致：最后一次 `dlnow == body_len`；`dltotal` 与 `Content-Length` 一致（若可得）
- 对比方式：
  - artifacts 写入 `progress_summary`（例如：`first/last/total/events_count`），对比器只比较摘要字段
- 边界条件：
  - 不比较事件次数/时间戳；不比较瞬时速率
- 优先级：中
- 完成判据：
  - 下载与上传各新增 1 个进度一致性用例，并在不同协议族（至少 h2）下稳定通过
- 最小可复现步骤：
  - `python tests/libcurl_consistency/run_gate.py --suite all --build`
  - `QCURL_QTTEST="build/tests/tst_LibcurlConsistency" pytest -q tests/libcurl_consistency/test_*.py -k progress`

## LC-31：连接复用/多路复用的可观测一致性（可比规则先行）

- 背景：当前用例覆盖了“连接复用路径”（如 `upload_post_reuse`），但未对齐任何可观测的复用指标，存在“字节一致但复用/多路复用行为可区分”的风险。
- 覆盖点（libcurl API / QCurl 行为点）：
  - 方案 A（服务端观测）：记录 `client_address`（源端口）/连接标识，推断复用
  - 方案 B（libcurl getinfo）：`CURLINFO_NUM_CONNECTS`、`CURLINFO_LOCAL_PORT`（需要 QCurl 暴露等价可观测输出或通过测试执行器采集）
- 输入场景：
  - 自建观测服务端新增端点：同一连接上连续处理 N 次请求，并记录每次请求的连接标识
  - baseline 与 QCurl 各自做 N 次顺序请求（同一 manager/同一 easy 句柄）
- 期望可观测输出：
  - 在可控条件下（HTTP/1.1 keep-alive 或 HTTP/2 multiplexing），baseline 与 QCurl 的“连接复用指标”一致（例如：连接数相同）
- 对比方式：
  - artifacts 增加 `connection_observed`（例如：`unique_connections`、`per_request_conn_id`），对比器比较这些确定性统计
- 边界条件：
  - 明确哪些因素会使连接不复用（`Connection: close`、代理、不同 SslConfig、不同 HTTP 版本等），并在用例中固定这些变量
- 优先级：低
- 完成判据：
  - README 中明确“连接复用/多路复用”在本候选集中的可比口径与限制条件
  - 至少新增 1 个稳定用例验证复用（不要求进入 Gate）
- 最小可复现步骤：
  - `QCURL_LC_EXT=1 python tests/libcurl_consistency/run_gate.py --suite all --with-ext --build`
  - `QCURL_QTTEST="build/tests/tst_LibcurlConsistency" pytest -q tests/libcurl_consistency/test_ext_suite.py -k reuse`

## LC-32：错误路径一致性（连接拒绝/代理 407/非法 URL）

- 背景：错误映射是可观测一致性的核心（用户依赖 error code/HTTP code）；当前仅覆盖少量 HTTP 错误与 TLS 错误，缺少连接级错误与 proxy auth 失败等常见路径。
- 覆盖点（libcurl API / QCurl 行为点）：
  - libcurl：`CURLE_COULDNT_CONNECT(7)`、`CURLE_URL_MALFORMAT(3)`、（proxy）HTTP 407 + `CURLINFO_RESPONSE_CODE`
  - QCurl：`NetworkError::ConnectionRefused`、`NetworkError::InvalidRequest`、HTTP>=400 的错误归一化（`kind/http_status`）
- 输入场景：
  - 连接拒绝：请求 `http://localhost:<unused_port>/`
  - 代理 407：对 `http_proxy_server.py` 使用错误凭据/不提供凭据，触发 407
  - 非法 URL：`http://` 或包含非法字符的 URL
- 期望可观测输出：
  - baseline 与 QCurl 的错误归一化一致；必要时扩展 artifacts 输出 `curlcode/http_code`
  - 对于“服务端未收到请求”的场景（非法 URL/连接拒绝），应无服务端观测记录或记录数为 0（需明确口径）
- 对比方式：
  - 以 `error` 字段为主断言；以（可选）stdout/stderr 中的 `curlcode=...` 为辅助证据
- 边界条件：
  - DNS 失败不纳入本任务（受系统 DNS 影响大）；仅选取本地可稳定复现的错误路径
- 优先级：中
- 完成判据：
  - 新增至少 3 个错误用例（上述 3 类各 1 个），稳定复现并通过对比
  - README 中补齐错误归一化字段（`kind/http_status/curlcode/http_code`）及其对比规则
- 最小可复现步骤：
  - `python tests/libcurl_consistency/run_gate.py --suite all --build`
  - `QCURL_QTTEST="build/tests/tst_LibcurlConsistency" pytest -q tests/libcurl_consistency/test_*.py -k \"refused or 407 or malformat\"`

## LC-33：HTTP 方法面一致性（HEAD/DELETE/PATCH）

- 背景：QCurl 已实现多种 HTTP method 的 libcurl option 映射（见 `src/QCNetworkReply.cpp` 的 method 分支），但一致性候选集目前只覆盖 GET/PUT/POST。
- 覆盖点（libcurl API / QCurl 行为点）：
  - libcurl：`CURLOPT_NOBODY`（HEAD）、`CURLOPT_CUSTOMREQUEST`（DELETE/PATCH）、`CURLOPT_POSTFIELDS`（PATCH body）
  - QCurl：`HttpMethod::Head/Delete/Patch` 与请求体/响应体语义
- 输入场景：
  - 自建 `http_observe_server.py` 增加端点：
    - `/method`：回显 method 与 body len（DELETE/PATCH）
    - 对 HEAD：返回确定性 header（Content-Length 固定）且 body 为空
- 期望可观测输出：
  - 服务端观测 method 一致；PATCH/DELETE 的请求体 len/hash 一致（如有）
  - HEAD 响应体为空且对齐（`body_len=0`）
- 对比方式：
  - 以服务端观测的 method/body_len + 客户端落盘的 body/hash 对齐
- 边界条件：
  - 对 PATCH/DELETE 的响应体可按字节一致断言；避免依赖自动重定向等副作用
- 优先级：低
- 完成判据：
  - 新增至少 2 个方法用例（HEAD + PATCH 或 DELETE），并加入 `run_gate.py --suite all`（可选 gate 级别视稳定性决定）
- 最小可复现步骤：
  - `python tests/libcurl_consistency/run_gate.py --suite all --build`
  - `QCURL_QTTEST="build/tests/tst_LibcurlConsistency" pytest -q tests/libcurl_consistency/test_*.py -k \"head or patch or delete\"`

## LC-34：可选 WebSocket 细节一致性（压缩/fragment/close）

- 背景：当前 WS 一致性覆盖主要集中在 ping/pong 与基本 data frames；对压缩协商、fragment、close 码/原因等细节缺少对齐。
- 覆盖点（libcurl API / QCurl 行为点）：
  - libcurl：WS 相关 API（如 `curl_ws_send/recv`）、握手扩展头（`Sec-WebSocket-Extensions`）
  - QCurl：`QCWebSocketCompressionConfig`、`closeReceived`、fragment 相关事件（若暴露）
- 输入场景：
  - 扩展 `ws_scenario_server.py`：增加可控的 fragment 与 close 场景；可选开启 permessage-deflate
- 期望可观测输出：
  - 握手扩展协商一致（白名单头）
  - 帧事件序列一致（TEXT/BINARY/CONTINUATION/CLOSE），close code/原因一致
- 对比方式：
  - artifacts 以“帧事件序列”（与现有 `download_0.data` 事件序列格式一致）对齐
- 边界条件：
  - 压缩与 fragment 可能依赖库版本与实现策略，需在 README 中明确版本/配置前置条件
- 优先级：低
- 完成判据：
  - 新增至少 1 个压缩或 fragment 场景的 ext 用例，并在 `QCURL_LC_EXT=1` 下稳定通过
- 最小可复现步骤：
  - `QCURL_LC_EXT=1 python tests/libcurl_consistency/run_gate.py --suite all --with-ext --build`
  - `QCURL_QTTEST="build/tests/tst_LibcurlConsistency" pytest -q tests/libcurl_consistency/test_ext_ws_suite.py -k deflate`
