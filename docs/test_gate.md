  - 结论（证明力）：现有测试只能在“明确限定的可观测维度/受控环境”内提供证据，不足以支撑“项目实现无问题”的全局断言；其中 libcurl_consistency 的设计总体更接近“证据链”，但 P0 若干用例存在“README/命
    名语义 ≠ QCurl 侧实际执行语义” 的关键缺口，会导致假阳性。
  - 最可靠部分：tests/libcurl_consistency/run_gate.py 的 skip=fail + schema 校验 + 脱敏扫描 门禁；以及 pause_resume_strict / resp_headers_raw 这类“强判据/字节级”一致性用例。
  - 最不可靠部分：P0 download_parallel_resume / upload_post_reuse 等用例 未在 QCurl 侧复现 upstream 语义（并发/复用/pause-resume）但仍可能全绿；以及“API 报告字段”（如 httpStatusCode）
    在缺少专门强证据用例时仍可能误报而不被发现。
  - 主要风险：团队可能被“门禁全绿”误导为并发/连接复用/重定向序列/HTTP/3 覆盖已经足够；同时仍可能存在 竞态、资源释放、API 侧错误报告（状态码/头/错误字符串）、边界条件 等缺陷而不被测出。
  - 建议方向：把 README 契约改成“可审计的证据声明”（明确哪些维度被比较、哪些被归一化、哪些不在范围），并对 P0 的“语义不对齐”做最小修复（要么补齐 QCurl 执行器语义，要么降级/更名并把对应能力移入
    ext/P2 并做强判据）。

  ────
  变更:

  - ✅ README 口径纠偏：tests/README.md 不再维护固定“总数/静态清单”，真实清单以 `ctest -N/-L` 与 `tests/qcurl/CMakeLists.txt`（注册/分组）为准。
  - ✅ P0 WebSocket 证据对称：ws_* baseline 改为落盘 baseline（qcurl_lc_ws_baseline 新增 ws_pingpong_small/ws_data_small），P0 对比基于 download_0.data 产物，移除“预期 bytes 注入”。
  - ✅ 取证工件隔离：proxy CONNECT 诊断字段 baseline/qcurl 各自仅写本侧，避免工件污染。
  - ✅ API 报告强证据：新增 ext 用例输出 reported_meta.json（至少 httpStatusCode），并在 pytest 中与 observe_http 观测值对比（需 QCURL_LC_EXT=1）。
  - ✅ 稳定性：observe_http 观测等待窗口支持 QCURL_LC_OBSERVE_HTTP_TIMEOUT_S 覆盖默认 2.0s。

  下一步: 如需继续降低假阳性风险，建议优先处理 P0 “语义不对齐”用例（并发/复用/暂停恢复）——要么补齐 QCurl 执行器语义，要么降级/更名并把强判据下沉到 ext/P2。

  ———

  ## 0) 范围澄清（避免歧义）

  - 你描述的 @tests/ 对应仓库实际目录为 tests/；核心一致性子集为 tests/libcurl_consistency/。

  ———

  ## 1) Q1 可靠性与一致性审计（逐项、可复核证据）

  ### 1.1 README（tests/README.md）口径已纠偏：清单以 ctest/CMakeLists 为准（避免误导）

  证据链

  - README 已移除固定“总数/静态清单”声明，改为明确：真实清单以 `tests/qcurl/CMakeLists.txt`（注册/分组）与 `ctest -N/-L`（当前 build 实际集合）为准。
  - 因此“门禁覆盖面”可以被直接复核：同一 `build/` 目录下，任何人都可用 `ctest -N/-L` 重建清单并对照 LABELS。

  证明力破坏方式

  - ✅ 已降低：通过“以 ctest/CMake 为准”的可审计口径，避免文档口径漂移导致的覆盖误判。

  最小修复建议

  - ✅ 已实施：README 现作为“门禁入口/证据边界导航”而非静态列表维护点。

  ———

  ### 1.2 “skip=fail”门禁声明：实现存在且一致（这是优点，增强证据严谨性）

  证据链

  - tests/qcurl/CMakeLists.txt 的 add_qcurl_test() 对所有 QtTest 目标设置：

    set_tests_properties(${test_name} PROPERTIES
        FAIL_REGULAR_EXPRESSION "SKIP\\s*:"
    )
    QtTest 的 QSKIP 返回码为 0，ctest 默认会当作通过；这里用 FAIL_REGULAR_EXPRESSION 把 “SKIP:” 视为失败，符合 tests/README.md 的“未执行=无证据=必须失败”。
  - tests/libcurl_consistency/run_gate.py 在 finally 阶段解析 JUnit，并把 skipped_tests / no_tests_executed 设为 policy violation，直接把 gate_returncode 置为 3（失败）。

  证明力增强点

  - 防止“跑不起来也绿”的伪证据，这是取证式门禁非常关键的强点。

  ———

  ### 1.3 libcurl_consistency 的 P0：存在“README/用例名语义 ≠ QCurl 侧执行语义”的覆盖缺口（严重：可导致假阳性）

  这条是本次审阅中最关键的“证明力断裂点”。

  #### (A) download_parallel_resume 并未在 QCurl 侧复现并发/恢复语义

  证据链

  - upstream/baseline 侧：tests/libcurl_consistency/pytest_support/case_defs.py 中 download_parallel_resume 的 baseline 参数模板包含并发与 pause 扰动：

    "args_template": ["-n","{count}","-m","{max_parallel}","-P","{pause_offset}","-V","{proto}","{url}"]
  - QCurl 侧执行器：tests/libcurl_consistency/tst_LibcurlConsistency.cpp 对 download_serial_resume / download_parallel_resume 的实现是同步串行下载循环：

    if (caseId == "download_serial_resume" || caseId == "download_parallel_resume") {
        for (int i = 0; i < count; ++i) {
            ... httpMethodToFile(... ExecutionMode::Sync ...)
        }
        return;
    }
    这里既没有并发（multi）路径，也没有 in-flight pause/resume 语义。
  - pytest P0 驱动：tests/libcurl_consistency/test_p0_consistency.py 给 QCurl 注入的环境变量集合 不包含 pause_offset/max_parallel，QCurl 侧根本无法复现 baseline 行为。

  失败模式

  - 覆盖缺失 → 假阳性：即使 QCurl 的 multi 并发路径/暂停恢复路径存在严重缺陷，P0 依然可能全绿，因为 QCurl 根本没走到那些路径。

  错误结论风险

  - 团队会（基于 case 名和 README）误以为“并发下载 + resume 的一致性已经验证”。

  最小修复建议（两条选其一，按你想要的证据强度）

  1. 证据强（推荐）：在 tests/libcurl_consistency/tst_LibcurlConsistency.cpp 为 download_parallel_resume 引入真正的并发下载（复用 ext 里的并发模式），并明确“本用例只验证终态文件一致，不对齐完成顺序”；同时把
     pause_offset 以环境变量传入并实际触发 pauseTransport/resumeTransport（若产品不要求 in-flight pause，则不要叫 resume）。
  2. 证据诚实（次优但最小）：保留现状，但把 caseId 与 README 文案改成“download_parallel_bytes_only / download_serial_bytes_only”，并把并发/暂停语义明确移到 P2/ext 的强判据（已有雏形：
     test_p2_pause_resume_strict.py、test_ext_suite.py）。

  #### (B) upload_post_reuse 同样未在 QCurl 侧复现“复用（reuse）”语义

  证据链

  - QCurl P0 执行器中 upload_post_reuse 仍走 httpMethodToFile(... ExecutionMode::Sync ...) 的同步路径循环：

    if (caseId == "upload_post_reuse") {
        for (int i = 0; i < count; ++i) {
            httpMethodToFile(... ExecutionMode::Sync ...)
        }
        return;
    }
  - README 本身也提示了关键事实：Sync 模式不可跨请求复用连接（tests/libcurl_consistency/README.md 的“Sync 模式连接复用差异”说明）。
    这意味着 用例名语义（reuse）与执行路径（sync）逻辑上矛盾。

  失败模式

  - 覆盖缺失 → 假阳性：即使 QCurl 的 keep-alive/复用策略有 bug，此 P0 case 也可能全绿。

  最小修复建议

  - 把“复用可观测一致性”强制落到 ext/专用用例（现有 test_ext_suite.py::test_ext_reuse_keepalive_http_1_1 已做），并将 P0 的此 case 明确降级为“仅校验回显字节一致”。

  ———

  ### 1.4 libcurl_consistency 的“可观测字段”有清晰对比器，但 P0 默认比较维度偏窄（这是设计选择，但必须承认其证明边界）

  证据链

  - 对比器核心在 tests/libcurl_consistency/pytest_support/compare.py::compare_artifacts()，主断言字段是：
      - request: method/url/headers/body_len/body_sha256
      - response: status/http_version/headers/body_len/body_sha256
  - 但 P0 的 request/response 很多字段被“观测值覆盖”且 allowlist 极窄：
    tests/libcurl_consistency/pytest_support/observed.py::httpd_observed_for_id() 只从 httpd/nghttpx 日志提取：

    headers = {}
    if range_v: headers["range"]=...
    if include_content_length: headers["content-length"]=...
    Host/Cookie/Auth 等不在 P0 httpd/nghttpx allowlist。

  失败模式

  - 比较维度缺失 → 假阳性：QCurl 可能在关键头（Cookie/Authorization/Accept-Encoding/Host）处理上与 libcurl 不一致，但在 P0 下载/上传“最终字节”层面仍一致，于是 P0 全绿。

  最小修复建议

  - 不要求一次性扩全：为每个“你认为是产品契约”的头新增一条 白名单 + 对应用例，否则明确写入 README 的“不在范围”。
  - P0 若继续保持极窄维度，请把 README 的“关键头白名单”具体化到“P0 仅 Range/Content-Length”。

  ———

  ### 1.5 工件（artifacts）存在“证据污染”的实例（会破坏取证可追溯性）

  证据链

  - tests/libcurl_consistency/test_p1_proxy.py 在 CONNECT 诊断段落构造 diag，同时包含 baseline 与 qcurl 的解析结果，然后：

    baseline["payload"]["connect_headers_diag"] = diag
    qcurl["payload"]["connect_headers_diag"] = diag
    这会导致 baseline.json 中混入 qcurl_connect_blocks（对照侧信息），违反“基线与被测侧证据隔离”的取证要求。

  失败模式

  - 取证污染 → 复核困难/易误判：事后只拿到 baseline.json 时无法确定哪些字段来自 baseline、哪些来自 qcurl。
  - 若未来把该 diag 纳入比较字段，会出现“自我对比导致假绿”的风险（虽然当前 compare.py 未比较它）。

  最小修复建议

  - baseline.json 只写 baseline 诊断；qcurl.json 只写 qcurl 诊断；或把 diag 单独落盘到 service_logs/meta.json，并在 artifacts 中只放指针路径。

  ———

  ### 1.6 时序/事件循环敏感点：存在可解释的“潜在 flaky”窗口（目前多数用例已做缓冲，但仍要承认风险）

  证据链（示例）

  - tests/libcurl_consistency/pytest_support/observed.py::_wait_for_observe_http_matches() 固定轮询等待 2s：

    timeout_s: float = 2.0
    time.sleep(0.01)
    在极端慢 CI/受限容器下，可能出现“服务端写 log 较晚 → 观测空读 → 假失败”。
  - tests/libcurl_consistency/tst_LibcurlConsistency.cpp::p1_cancel_after_first_chunk 要求取消后 cancelled 与 finished 都必须在同一次等待窗口内发生，且取消后不得再有 readyRead/progress（非常严格的时序契约），在实现
    或 Qt 信号调度变化时存在波动风险。

  最小修复建议

  - 把 observe_http 的等待超时变为可配置（env），并在失败日志里输出“最后一次观测到的条目数”。
  - 对“取消后无额外信号”的断言可以保留，但建议用“事件循环 drain 后再断言”或引入“短窗口内不得再出现”的更稳健表述，避免纯调度噪声导致的假红。

  ———

  ## 2) Q2 “证明实现无问题”是否成立（覆盖与约束强度）

  结论：不成立（只能在严格限定范围内成立）

  ### 2.1 现有证据最多能证明什么

  在以下假设下，tests/libcurl_consistency 能提供中高强度证据：

  - 假设 A：baseline（libcurl + curl testenv + repo baseline clients）本身正确且稳定；
  - 假设 B：运行环境具备必要能力（端口/进程/httpd/nghttpx/ws），且按 gate 方式执行（skip=fail）；
  - 假设 C：你接受 README 明确写出的归一化/限制（例如 multi 不比完成顺序、multipart 不比 boundary、Date/Server 不比等）。

  则可以证明：在被纳入 suite 的用例集合中，QCurl 与 libcurl 在“被定义的可观测字段集合”上等价（尤其是字节级 body/hash、部分错误语义、部分头部/序列合同）。

  ### 2.2 仍可能“全部测试通过”的实现缺陷盲区（系统枚举）

  即使 tests 全绿，以下类别缺陷仍高度可能存在（且当前体系难以排除）：

  - 资源释放/生命周期：easy/multi/共享句柄、socket、QIODevice 生命周期泄露或 double-free；目前缺少 ASAN/LSAN/valgrind 证据链与专门断言。
  - 并发/异步竞态：
      - multi 回调顺序、重入、跨线程提交（部分有 tst_QCNetworkActorThreadModel 等覆盖，但一致性套件对“完成顺序/关键事件序列”多处明确不比）。
      - “取消/暂停/恢复”在不同平台/调度下的边界行为（少量强判据覆盖，但不是全面矩阵）。
  - 连接复用/池化：只在少数 ext/专项用例里用 peer_port 做可观测统计，无法覆盖：DNS 缓存、连接迁移、HTTP/2 multiplex 限制、连接逐出策略、连接复用错误导致的跨请求污染。
  - TLS/证书细节：覆盖了 verify 成功/失败、pinned public key 等，但未覆盖：证书链边界、OCSP/CRL、SNI/ALPN 分歧、TLS session resumption、错误字符串一致性等。
  - 编码/解压：Accept-Encoding 用例存在，但整体仍未覆盖：br/deflate 组合、自动解压与原始字节交付策略（尤其是 header 与 body 的组合一致性）。
  - Header 处理的高阶语义：重复头/折叠行已覆盖一部分，但请求侧的重复头、header 顺序、大小写规范化、trailer 等仍未纳入可观测模型。
  - 真实网络漂移：tests/ 下部分用例依赖 httpbin/docker 或外网（external_*），在 CI/审计环境可能不稳定，不能作为强证据来源。
  - 性能/退化：吞吐、背压峰值、CPU 占用、内存峰值与抖动，只做了最小合同或 smoke（例如 connection limits），不能证明不存在性能级缺陷。

  ———

  ## 3) Q3 libcurl_consistency 深挖：可观测数据采集与对齐模型是否“科学/不干扰”

  ### 3.1 实际采集了哪些维度（以代码为准）

  主断言（compare.py 强制比较）

  - 请求：method/url/headers/body_len/body_sha256
  - 响应：status/http_version/headers/body_len/body_sha256
    证据：tests/libcurl_consistency/pytest_support/compare.py::_cmp_dict/_cmp_list_dict

  可选但已在多用例启用的维度

  - 原始响应头字节级：response.headers_raw_len/headers_raw_sha256/headers_raw_lines
    证据：tests/libcurl_consistency/test_p1_resp_headers.py
  - 错误归一化：observed.error.{http_status,http_code} + derived.error.{kind,curlcode}（兼容 legacy error）
    证据：tests/libcurl_consistency/pytest_support/artifacts.py::apply_error_namespaces 与 compare.py _extract_error_namespaces
  - 进度稳定摘要：progress_summary.download/upload.{monotonic,now_max,total_max}
    证据：tests/libcurl_consistency/test_p1_progress.py
  - 连接复用统计：connection_observed.{request_count,unique_connections,conn_seq}（来自服务端 peer_port 归一化）
    证据：tests/libcurl_consistency/test_ext_suite.py::test_ext_reuse_keepalive_http_1_1
  - pause/resume：弱判据 pause_resume.{pause_offset,pause_count,resume_count,event_seq}；强判据 pause_resume_strict 结构化 events + Δ=0 合同
    证据：tests/libcurl_consistency/test_p2_pause_resume*.py 与 tests/libcurl_consistency/tst_LibcurlConsistency.cpp::p2_pause_resume_strict
  - backpressure 合同：backpressure_contract（bp_on/bp_off + 软上界校验）
    证据：tests/libcurl_consistency/test_p2_backpressure_contract.py + pytest_support/compare.py::_validate_backpressure_contract
  - 上传 READFUNC_PAUSE 合同：upload_pause_resume
    证据：pytest_support/compare.py::_validate_upload_pause_resume_contract

  明显未采集或未纳入对比的维度（因此无法作为一致性证据）

  - 详细 TLS 细节（握手信息、协议协商细节、证书链信息、TLS session resumption）
  - DNS/连接建立统计（CURLINFO_* 的时间统计、namelookup/connect/appconnect 等）
  - 错误字符串（errorString）一致性
  - HTTP/2/3 帧级事件、流优先级、trailer

  ### 3.2 采集方式是否会改变被测行为（干扰性评估）

  确凿干扰源

  - correlation id query：几乎所有一致性用例会把 id=<req_id> 追加到 URL（例如 test_p0_consistency.py::_append_req_id、Qt 执行器 withRequestId()）。
    这会改变 URL，从而可能改变缓存命中/服务端路由/日志行为；虽然对比时会 strip id，但运行时行为仍受影响。
    结论：可接受但必须在 README 中明确“这是测试注入扰动，且可能改变缓存/重定向链行为”。
  - http_observe_server.py 的 /delay_headers：先 _write_log(status=0) 再 sleep，再发送真实响应，但不再写 log。
    这意味着 “observed.status=0” 不是实际 HTTP status，而是“未发头标记”。
    结论：这是“语义建模”，不是纯观测；必须明确写成合同（目前 test_p1_timeouts.py 已写），否则外部会误判为伪造。

  相对低干扰

  - httpd/nghttpx 的 access_log 格式 monkeypatch（tests/libcurl_consistency/conftest.py::_patch_httpd_access_log/_patch_nghttpx_access_log）：主要影响日志格式，通常不改变响应语义。
  - 失败时日志收集 QCURL_LC_COLLECT_LOGS=1：仅在失败路径复制文件，不改变被测行为。

  ### 3.3 对比规则是否“洗掉真实差异”

  明确存在的“洗差异”点（均为设计取舍）

  - P0 只比少量请求头（Range/Content-Length），其余头差异会被忽略（除非专项用例）。
    影响：可能放过 Cookie/Auth/Accept-Encoding 之类的错误。
  - multi 用例默认按 URL 排序做集合等价，不比较完成顺序（observed.py::httpd_observed_list_for_id 返回 sorted；README 也声明）。
    影响：若业务依赖时序（回调顺序/首包先后），当前不会报错。
  - upload_post_reuse 明确排除 Content-Length（P0），以避免 chunked vs length 的不稳定；这会放过 Content-Length 相关缺陷。

  建模合理且必要的“归一化”点（应视为允许差异）

  - 动态头 Date/Server（test_p1_resp_headers.py 过滤）
  - WS 随机握手字段（Sec-WebSocket-Key 不记录）
  - Digest Authorization 参数（cnonce 等不稳定，test_p1_httpauth.py 把 Digest 归一到 scheme）

  ———

  ## 4) Q4 造假/弱化迹象排查（现有代码中“更易假通过”的机制）

  我没有发现典型的“为了通过而强行放水”的 xfail/强行吞错/把失败改 skip 等模式；相反 skip=fail 的门禁很严格。
  但存在多处可导致“看起来测了，实际没测到关键语义”的弱化（仍然会假绿）：

  1. P0 用例名/README 语义强，但 QCurl 侧执行语义弱：download_parallel_resume/upload_post_reuse（见上文 1.3）。
  2. P0 关键字段用服务端观测覆盖，未对齐 QCurl API 输出：test_p0_consistency.py 覆盖 response.status/http_version 等，导致“QCurl API 报告错误”类缺陷可能不被测出。
  3. 字段被显式排除：test_p0_consistency.py 对 upload_post_reuse 设置 include_content_length = False。
  4. 集合等价代替序列等价：ext_multi 不比完成顺序。
  5. 工件污染风险：proxy diag 同时写入 baseline/qcurl（见 1.5）。

  ———

  ## 5) Q5 工程化问题清单（按你要求的交付格式）

  ### P1（严重）P0：并发/复用语义未在 QCurl 侧复现（假阳性）

  - 具体位置：tests/libcurl_consistency/tst_LibcurlConsistency.cpp::TestLibcurlConsistency::testCase
      - caseId == "download_serial_resume" || "download_parallel_resume"
      - caseId == "upload_post_reuse"
  - 触发条件：运行 tests/libcurl_consistency/test_p0_consistency.py 的对应 case（h1/h2/h3 变体）。
  - 失败模式：覆盖缺失 → 假阳性（并发/multi、连接复用、in-flight pause/resume 的实现缺陷测不出来）。
  - 错误结论风险：把“终态字节一致”误读为“并发/复用/暂停恢复行为一致”。
  - 最小修复建议：
      1. 若你要保留“P0 作为最小强证据集”：让 QCurl 执行器在这些 case 中走真实 async/multi 路径，并把“reuse/parallel”作为可观测统计或合同字段纳入 artifacts；
      2. 若你只想保留“字节一致”证据：重命名/重写 README，删除“resume/reuse/parallel”暗示，把这些语义移到 ext/P2（已有相关测试雏形）。

  ### P2（严重）P0 WS：baseline 侧用“预期字节”代替“实际 baseline 输出”（A/B 不对称）

  - 具体位置：tests/libcurl_consistency/test_p0_consistency.py
      - _build_response_proto() 为 ws_* 返回固定 body
      - ws case 的 baseline run_libtest_case(... response_meta={"body": expected_bytes} ...) 未从 baseline 真实输出计算 hash/len
  - 触发条件：运行 ws_pingpong_small、ws_data_small。
  - 失败模式：覆盖缺失/不可复核 → 假阳性（若 baseline 行为漂移/实现差异，P0 不会报警）。
  - 错误结论风险：将“QCurl 符合预期”误当成“QCurl 与 libcurl 可观测输出一致”。
  - 最小修复建议：把 P0 WS baseline 切换为可落盘的 baseline（例如使用 repo 内置 qcurl_lc_ws_baseline + pytest_support/ws_baseline.py），或扩展 upstream ws client 的落盘采集并纳入 artifacts。

  ### P3（中等）P0：用服务端观测覆盖 status/http_version，无法证明 QCurl API 侧的报告正确

  - 具体位置：tests/libcurl_consistency/test_p0_consistency.py（覆盖 payload["response"]["status"/"http_version"]）
  - 触发条件：运行任意 P0 HTTP case。
  - 失败模式：比较维度缺失 → 假阳性（网络正确但 QCurl API 报告错误仍会通过）。
  - 错误结论风险：误以为“QCNetworkReply 的 status/httpVersion 输出与 libcurl 一致”。
  - 最小修复建议：在 Qt 执行器落盘“QCurl 侧 API 可观测字段”（例如 statusCode、rawHeaderData hash、errorString hash），并在 compare.py 中作为可选但强制的字段比较；或者在 README 明确声明“P0 不比较
    QCurl 的 status API”。

  ### P4（中等）请求头 allowlist 偏窄 + headers 用 dict（会丢重复头/顺序）

  - 具体位置：
      - tests/libcurl_consistency/pytest_support/observed.py：httpd/nghttpx 只取 range/content-length
      - tests/libcurl_consistency/pytest_support/artifacts.py::normalize_headers() 用 dict 表示 headers
  - 触发条件：P0/P1 中大量依赖 httpd/nghttpx access_log 的 case。
  - 失败模式：比较维度缺失 → 假阳性（Host/Cookie/Auth 等错误可能不报；重复头/顺序语义可能丢失）。
  - 错误结论风险：误以为“请求头一致性已验证”。
  - 最小修复建议：把 headers 表示升级为“raw_lines + 归一化摘要”双轨（已对响应头做了类似工作），或对关键请求头新增 observe_server 场景并纳入 P1/P2。

  ### P5（已修复）工件污染：proxy CONNECT 诊断同时写入 baseline 与 qcurl

  - 具体位置：tests/libcurl_consistency/test_p1_proxy.py（connect_headers_diag）
  - 触发条件：运行 proxy_https_connect_basic_auth、proxy_connect_headers_1941。
  - 失败模式：取证污染 → 可复核性降低；未来若误把该字段纳入对比可能出现“自我对比假绿”。
  - 错误结论风险：审计者误把 baseline.json 中的 qcurl_* 字段当 baseline 证据。
  - ✅ 已修复：baseline/qcurl 各自只写本侧 connect_headers_diag（不入门禁），避免工件污染与“自我对比假绿”风险。

  ### P6（已修复）观测日志等待窗口固定 2s（极端慢环境可能 flaky）

  - 具体位置：tests/libcurl_consistency/pytest_support/observed.py::_wait_for_observe_http_matches(timeout_s=2.0)
  - 触发条件：IO/调度极慢、服务端写 log 延迟、容器受限。
  - 失败模式：不可复现/假阴性（偶发红）。
  - 错误结论风险：误以为实现不一致，实际是观测同步问题。
  - ✅ 已修复：支持 QCURL_LC_OBSERVE_HTTP_TIMEOUT_S 覆盖默认等待窗口（默认保持 2.0s）。

  ———

  ## 6) 当前体系“最可靠/最不可靠”部分（含证据与信心等级）

  ### 最可靠（高信心）

  - tests/libcurl_consistency/run_gate.py：skip=fail + schema 校验 + 脱敏扫描，能有效阻断“无执行也绿/泄密也绿”。
  - 强判据合同用例：
      - tests/libcurl_consistency/test_p2_pause_resume_strict.py + tests/libcurl_consistency/tst_LibcurlConsistency.cpp::p2_pause_resume_strict + pytest_support/compare.py::_validate_pause_resume_contract
      - tests/libcurl_consistency/test_p1_resp_headers.py（字节级响应头 + 形状断言防失效）

  ### 最不可靠（中低信心）

  - P0 若干 case 的语义不对齐（并发/复用/暂停恢复未复现）：会造成最危险的假阳性——“看起来测了关键能力，实际没有”。
  - “API 报告字段”若缺少专门强证据用例，仍可能存在误报/漏报而不被 P0 的可观测对照发现（已补齐 httpStatusCode 的 ext 证伪用例）。

  ———
 
  ## 附录 A) 可证明清单（证据口径）

  - tests/（ctest 门禁）：
      - 证明点：在选定 LABELS 集合内，QtTest 目标真实执行且无 QSKIP 假绿（skip=fail）。
      - 复核入口：`tests/qcurl/CMakeLists.txt`（注册/分组） + `ctest -N/-L`（当前 build 清单） + `python3 scripts/ctest_strict.py`（门禁执行）。
  - libcurl_consistency（baseline ↔ qcurl 可观测对照）：
      - 证明点：在纳入 suite 的用例集合中，baseline 与 QCurl 在“可观测语义摘要”和“响应体/工件 sha256”上等价（对比字段见 `pytest_support/compare.py`）。
      - 复核入口：`build/libcurl_consistency/reports/*`（JUnit/XML + gate JSON）与 `curl/tests/http/gen/artifacts/<suite>/<case>/`（artifacts）。
  - WebSocket：
      - P0：`ws_pingpong_small/ws_data_small` 的 baseline ↔ qcurl 对照基于 `download_0.data` 产物（字节级 len/sha256）。
      - ext：帧语义（PING/PONG/CLOSE 等）以事件序列落盘（download_0.data）并对照。
  - API 报告字段（ext/P2 强证据示例）：
      - `ext_api_reported_status`：证明 QCurl `httpStatusCode()` 与 observe_http 服务端观测 `status` 一致（可证伪误报）。

  ## 附录 B) 不可证明清单（边界）

  - “全绿 ≠ 实现无问题”：当前体系无法证明不存在 竞态/内存泄漏/性能退化/平台差异 等全局性质。
  - “可观测一致 ≠ 全语义一致”：对照仅覆盖被定义的可观测字段集合；未纳入字段视为“不在证据范围”，不能据此做强断言。
  - “外部环境依赖”：env/external_* 组受本地服务/网络/系统能力影响，默认不宜作为强门禁证据。

  ## 附录 C) 遗漏清单（未覆盖/弱覆盖项）

  - P0 语义不对齐风险：`download_parallel_resume / upload_post_reuse` 等用例的名称语义与 QCurl 执行器实际语义存在差异时，可能产生假阳性（需补齐执行器语义或下沉到 ext/P2 强判据）。
  - 并发/复用/暂停恢复：若缺少“事件序列/强判据”类用例，容易出现“测到了结果但没测到路径/序列”的盲区。

  ## 附录 D) 报告错误清单（API 报告层：默认不在 P0 对照范围）

  - P0 对照以服务端观测重写 artifacts 的 request/response 关键字段；这能证明“实际线上行为一致”，但**不能直接证伪**“QCurl API 对外报告字段”是否一致。
  - 建议策略：对需要被审计/验收的 API 报告字段，逐项增加 ext/P2 强证据用例（示例：`httpStatusCode()` 已由 `ext_api_reported_status` 覆盖）。
