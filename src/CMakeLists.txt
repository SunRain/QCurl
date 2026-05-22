# QCurl 库源文件配置

set(QCURL_CORE_SOURCES
    CurlGlobalConstructor.cpp
    QCBlockingCookieStore.cpp
    QCCookieAsyncResult.cpp
    QCNetworkAccessManager.cpp
    QCNetworkAccessManagerConfig.cpp
    QCNetworkAccessManagerCookies.cpp
    QCNetworkAccessManagerSend.cpp
    QCNetworkAccessManagerServices.cpp
    QCNetworkTestSupport.cpp
    QCNetworkHttpAuthConfig.cpp
    QCNetworkRequest.cpp
    QCNetworkRequestTransferAccessors.cpp
    QCNetworkRedirectConfig.cpp
    QCNetworkTransferConfig.cpp
    QCUtility.cpp
)

set(QCURL_CORE_HEADERS
    private/CurlGlobalConstructor_p.h
    QCBlockingCookieStore.h
    QCCookieAsyncResult.h
    QCNetworkAccessManager.h
    QCNetworkAccessManager_p.h
    QCNetworkRequestConfig.h
    QCNetworkRequest.h
    private/QCNetworkRequestPrivate_p.h
    QCUtility.h
    qbytedata_p.h
)

set(QCURL_FEATURE_SOURCES
    QCBlockingNetworkClient.cpp
    QCBlockingNetworkClientOptions.cpp
    QCBlockingNetworkResult.cpp
    private/QCBlockingCurlAdapter.cpp
    private/QCBlockingCurlMethodSetup.cpp
    private/QCBlockingCurlRequestSetup.cpp
    private/QCBlockingCurlRequestSetupExtensions.cpp
    private/QCBlockingRequestBody.cpp
    QCCurlHandleManager.cpp
    CurlFeatureProbe.cpp
    private/QCRequestPipeline.cpp
    private/QCNetworkReplyBodySource.cpp
    private/QCNetworkReplyCallbacks.cpp
    private/QCNetworkReplyFlowControl.cpp
    private/QCNetworkReplyResponse.cpp
    private/QCNetworkReplyRuntime.cpp
    private/QCNetworkReplyCurlOptions.cpp
    private/QCNetworkReplyExecution.cpp
    private/QCNetworkReplyState.cpp
    private/QCNetworkReplyControls.cpp
    private/QCNetworkReplyAccessors.cpp
    private/QCNetworkRequestSchedulerQueue.cpp
    private/QCNetworkRequestSchedulerLanePolicy.cpp
    private/QCNetworkRequestSchedulerDispatch.cpp
    private/QCNetworkRequestSchedulerPrivate.cpp
    private/QCNetworkRequestSchedulerValues.cpp
    private/QCNetworkRequestSchedulerControl.cpp
    private/QCNetworkRequestSchedulerQueries.cpp
    private/QCNetworkRequestSchedulerLifecycle.cpp
    private/QCNetworkRequestSchedulerConfig.cpp
    private/QCNetworkRequestSchedulerFinalize.cpp
    private/QCSingleFileMultipartBodyDevice.cpp
    QCNetworkBody.cpp
    QCNetworkMultipartBody.cpp
    QCNetworkSslConfig.cpp
    QCNetworkProxyConfig.cpp
    QCNetworkTimeoutConfig.cpp
    QCNetworkHttpVersion.cpp
    QCNetworkError.cpp
    QCNetworkReply.cpp
    QCCurlMultiManager.cpp
    private/QCCurlMultiManagerLimits.cpp
    private/QCCurlMultiManagerEventDriver.cpp
    private/QCCurlMultiManagerShare.cpp
    private/QCCurlMultiManagerShareLifecycle.cpp
    private/QCCurlMultiManagerCookieContext.cpp
    private/QCCurlMultiManagerCookies.cpp
    QCNetworkRetryPolicy.cpp
    QCNetworkRequestPriorityMetaTypeBootstrap.cpp
    QCNetworkRequestScheduler.cpp   # v2.6.0: 请求优先级调度
    QCMultipartFormData.cpp         # v2.11.0: Multipart 表单数据
    QCNetworkCache.cpp              # v2.12.0: 缓存基类
    QCNetworkMemoryCache.cpp        # v2.12.0: 内存缓存
    QCNetworkDiskCache.cpp          # v2.12.0: 磁盘缓存
    QCNetworkConnectionPoolConfig.cpp
    QCNetworkConnectionPoolStatistics.cpp
    QCNetworkConnectionPoolManager.cpp  # v2.14.0: 连接池管理器
    QCNetworkLogger.cpp             # v2.15.0: 日志系统
    QCNetworkDefaultLogger.cpp      # v2.15.0: 默认日志实现
    QCNetworkLogRedaction.cpp       # v2.20.0: 日志脱敏工具
    QCNetworkMiddleware.cpp         # v2.15.0: 中间件
    QCNetworkCancelToken.cpp        # v2.15.0: 取消令牌
    QCNetworkTransferJob.cpp
    QCNetworkDownloadToDeviceJob.cpp
    QCNetworkResumableDownloadJob.cpp
    QCNetworkCapturedRequest.cpp
    QCNetworkMockHandler.cpp        # v2.16.0: Mock 工具
    private/QCNetworkResumableDownloadWriter.cpp
    QCNetworkDiagnostics.cpp        # v2.19.0: 网络诊断工具
)

set(QCURL_FEATURE_HEADERS
    QCGlobal.h
    QCBlockingNetworkClient.h
    QCBlockingNetworkResult.h
    private/QCBlockingCurlAdapter_p.h
    private/QCBlockingCurlMethodSetup_p.h
    private/QCBlockingCurlRequestSetup_p.h
    private/QCBlockingRequestBody_p.h
    QCCurlHandleManager.h
    CurlFeatureProbe.h
    private/QCMultipartHeaderEncoding_p.h
    private/QCNetworkReplyBodySource_p.h
    private/QCNetworkReplyCallbacks_p.h
    private/QCNetworkReplyFlowControl_p.h
    private/QCNetworkReplyResponse_p.h
    private/QCNetworkReplyRuntime_p.h
    private/QCNetworkRequestSchedulerQueue_p.h
    private/QCNetworkRequestSchedulerDispatch_p.h
    private/QCNetworkRequestSchedulerPrivate_p.h
    private/QCSingleFileMultipartBodyDevice.h
    QCNetworkBody.h
    QCNetworkMultipartBody.h
    QCNetworkSslConfig.h
    QCNetworkProxyConfig.h
    QCNetworkTimeoutConfig.h
    QCNetworkHttpMethod.h
    QCNetworkHttpVersion.h
    QCNetworkError.h
    QCNetworkReply.h
    QCNetworkReply_p.h
    QCCurlMultiManager.h
    QCNetworkRetryPolicy.h
    QCNetworkRequestPriority.h      # v2.6.0: 优先级枚举
    QCNetworkRequestScheduler.h     # v2.6.0: 请求调度器
    QCMultipartFormData.h           # v2.11.0: Multipart 表单数据
    QCNetworkCachePolicy.h          # v2.12.0: 缓存策略枚举
    QCNetworkCache.h                # v2.12.0: 缓存基类
    QCNetworkMemoryCache.h          # v2.12.0: 内存缓存
    QCNetworkDiskCache.h            # v2.12.0: 磁盘缓存
    QCNetworkConnectionPoolConfig.h # v2.14.0: 连接池配置
    QCNetworkConnectionPoolManager.h # v2.14.0: 连接池管理器
    QCNetworkConnectionPoolManager_p.h # v2.14.0: 连接池内部 companion（内部）
    QCNetworkLogger.h               # v2.15.0: 日志系统
    QCNetworkDefaultLogger.h        # v2.15.0: 默认日志实现
    private/QCNetworkLogRedaction_p.h # v2.20.0: 日志脱敏工具（内部）
    QCNetworkMiddleware.h           # v2.15.0: 中间件
    QCNetworkMiddlewareExtras.h     # Middleware Extras opt-in surface
    private/QCNetworkMiddlewareInternal_p.h # 内部策略型 middleware
    QCNetworkCancelToken.h          # v2.15.0: 取消令牌
    QCNetworkTransferJob.h
    QCNetworkDownloadToDeviceJob.h
    QCNetworkResumableDownloadJob.h
    QCNetworkMockHandler.h          # v2.16.0: Mock 工具
    QCNetworkTestSupport.h          # Test Support 显式 manager 绑定
    QCNetworkMockHandler_p.h        # v2.16.0: Mock 内部 companion（内部）
    private/QCNetworkResumableDownloadWriter_p.h
    QCNetworkDiagnostics.h          # v2.19.0: 网络诊断工具
)

# ============================================================================
# WebSocket 支持（v2.3.0）+ 重连机制（v2.4.0）
# ============================================================================
if(QCURL_WEBSOCKET_SUPPORT)
    list(APPEND QCURL_FEATURE_SOURCES
        QCWebSocket.cpp
        QCWebSocketReconnectPolicy.cpp
        QCWebSocketPool.cpp                 # v2.5.0: WebSocket 连接池
        QCWebSocketCompressionConfig.cpp    # v2.18.0: WebSocket 压缩配置
    )
    list(APPEND QCURL_FEATURE_HEADERS
        QCWebSocket.h
        QCWebSocketReconnectPolicy.h
        QCWebSocketPool.h                   # v2.5.0: WebSocket 连接池
        QCWebSocketCompressionConfig.h      # v2.18.0: WebSocket 压缩配置
    )
    list(APPEND QCURL_FEATURE_HEADERS
        QCWebSocket_p.h
    )
    message(STATUS "✅ WebSocket 支持已启用（libcurl ${QCURL_LIBCURL_VERSION_STRING} + v2.4.0 重连 + v2.5.0 连接池 + v2.18.0 压缩）")
else()
    message(STATUS "⏭️ WebSocket 支持未启用（需要 libcurl 7.86.0+）")
endif()

# 合并所有源文件和头文件
set(QCURL_ALL_SOURCES ${QCURL_CORE_SOURCES} ${QCURL_FEATURE_SOURCES})
set(QCURL_ALL_HEADERS ${QCURL_CORE_HEADERS} ${QCURL_FEATURE_HEADERS})
# 默认稳定安装面（public package SSOT）只包含 Core contract：
# - 仅 `QCURL_INSTALL_HEADERS + QCurlConfig.h` 进入默认安装前缀
# - `QCNetworkLogger.h` 因 `QCNetworkAccessManager` 的对外接口依赖而进入 Core
# - `QCNetworkCachePolicy.h` 因 `QCNetworkRequest` 的对外接口依赖而进入 Core
# - `QCNetworkDefaultLogger.h` / `QCNetworkCancelToken.h` 作为 P1 低风险 Core helper 进入默认安装面
# - Cache concrete headers 完成 accessor/pimpl 重塑后作为 P2 Core 能力进入默认安装面
# - `QCMultipartFormData.h` 完成 shared-data 重塑后作为 P2 Core builder 进入默认安装面
# - ConnectionPool config / statistics 完成 accessor/shared-data 重塑后作为 P3 Core 管理面进入默认安装面
set(QCURL_INSTALL_HEADERS
    QCGlobal.h
    QCCookieAsyncResult.h
    QCNetworkAccessManager.h
    QCNetworkRequestConfig.h
    QCNetworkRequest.h
    QCNetworkSslConfig.h
    QCNetworkProxyConfig.h
    QCNetworkTimeoutConfig.h
    QCNetworkHttpMethod.h
    QCNetworkHttpVersion.h
    QCNetworkError.h
    QCNetworkReply.h
    QCNetworkRetryPolicy.h
    QCNetworkRequestPriority.h
    QCNetworkRequestScheduler.h
    QCNetworkCachePolicy.h
    QCNetworkCache.h
    QCNetworkMemoryCache.h
    QCNetworkDiskCache.h
    QCMultipartFormData.h
    QCNetworkBody.h
    QCNetworkMultipartBody.h
    QCNetworkTransferJob.h
    QCNetworkDownloadToDeviceJob.h
    QCNetworkResumableDownloadJob.h
    QCNetworkLogger.h
    QCNetworkDefaultLogger.h
    QCNetworkCancelToken.h
    QCNetworkConnectionPoolConfig.h
    QCNetworkConnectionPoolManager.h
    QCNetworkMiddleware.h
)

# 非默认 Extras：
# - 用于源码树示例、可选发行包或后续独立 package
# - 不参与默认 install surface / public-api / consumer smoke 合同
set(QCURL_INSTALL_HEADERS_EXTRAS
    QCBlockingCookieStore.h
    QCBlockingNetworkClient.h
    QCBlockingNetworkResult.h
    QCNetworkDiagnostics.h
    QCNetworkMockHandler.h
    QCNetworkTestSupport.h
)

set(QCURL_INSTALL_HEADERS_BLOCKING_EXTRAS
    QCBlockingCookieStore.h
    QCBlockingNetworkClient.h
    QCBlockingNetworkResult.h
)

set(QCURL_INSTALL_HEADERS_TEST_SUPPORT
    QCNetworkMockHandler.h
    QCNetworkTestSupport.h
)

set(QCURL_INSTALL_HEADERS_OTHER_EXTRAS
    QCNetworkDiagnostics.h
    QCNetworkMiddlewareExtras.h
)

if(QCURL_WEBSOCKET_SUPPORT)
    list(APPEND QCURL_INSTALL_HEADERS_EXTRAS
        QCWebSocket.h
        QCWebSocketReconnectPolicy.h
        QCWebSocketPool.h
        QCWebSocketCompressionConfig.h
    )
    list(APPEND QCURL_INSTALL_HEADERS_OTHER_EXTRAS
        QCWebSocket.h
        QCWebSocketReconnectPolicy.h
        QCWebSocketPool.h
        QCWebSocketCompressionConfig.h
    )
endif()

set(QCURL_INSTALL_HEADERS "${QCURL_INSTALL_HEADERS}" PARENT_SCOPE)
set(QCURL_INSTALL_HEADERS_EXTRAS "${QCURL_INSTALL_HEADERS_EXTRAS}" PARENT_SCOPE)
set(QCURL_INSTALL_HEADERS_BLOCKING_EXTRAS "${QCURL_INSTALL_HEADERS_BLOCKING_EXTRAS}" PARENT_SCOPE)
set(QCURL_INSTALL_HEADERS_TEST_SUPPORT "${QCURL_INSTALL_HEADERS_TEST_SUPPORT}" PARENT_SCOPE)
set(QCURL_INSTALL_HEADERS_OTHER_EXTRAS "${QCURL_INSTALL_HEADERS_OTHER_EXTRAS}" PARENT_SCOPE)
set(QCURL_GENERATED_CONFIG_HEADER "${CMAKE_BINARY_DIR}/src/QCurlConfig.h" PARENT_SCOPE)

# 创建 QCurl 主库。默认发布 shared；static 需要显式 opt-in，并有独立 gate 证明。
set(_qcurl_library_type SHARED)
if(QCURL_BUILD_STATIC)
    set(_qcurl_library_type STATIC)
endif()
add_library(QCurl ${_qcurl_library_type} ${QCURL_ALL_SOURCES} ${QCURL_ALL_HEADERS})

# 添加库别名（用于 CMake 导出）
add_library(QCurl::QCurl ALIAS QCurl)

# 设置库属性
set_target_properties(QCurl PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 3
    AUTOMOC ON  # 自动处理 Qt MOC
    EXPORT_NAME QCurl
    POSITION_INDEPENDENT_CODE ON
)

# 链接依赖
target_link_libraries(QCurl
    PUBLIC
        Qt6::Core
        Qt6::Network
)

if(QCURL_BUILD_STATIC)
    target_link_libraries(QCurl PUBLIC ZLIB::ZLIB) # v2.18.0: WebSocket 压缩
else()
    target_link_libraries(QCurl PRIVATE ZLIB::ZLIB) # v2.18.0: WebSocket 压缩
endif()

if(QCURL_BUILD_LIBCURL_CONSISTENCY)
    target_include_directories(QCurl PRIVATE "${CMAKE_SOURCE_DIR}/curl/include")
    target_link_libraries(QCurl PRIVATE "$<BUILD_INTERFACE:$<TARGET_FILE:libcurl_shared>>")
    add_dependencies(QCurl libcurl_shared)
else()
    if(QCURL_BUILD_STATIC)
        target_link_libraries(QCurl PUBLIC CURL::libcurl)
    else()
        target_link_libraries(QCurl PRIVATE CURL::libcurl)
    endif()
endif()

# 包含目录
target_include_directories(QCurl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>  # 用于 QCurlConfig.h
        $<INSTALL_INTERFACE:include/qcurl>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/private
)

# 编译定义
target_compile_definitions(QCurl
    PRIVATE
        $<$<NOT:$<BOOL:${QCURL_BUILD_STATIC}>>:QCURL_BUILDING_LIBRARY>
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)

# 测试专用：为单元测试提供可控的 capability 分支覆盖（不影响默认构建）
if(BUILD_TESTING)
    target_compile_definitions(QCurl PRIVATE QCURL_ENABLE_TEST_HOOKS)
endif()

# 特性宏定义
if(QCURL_WEBSOCKET_SUPPORT)
    target_compile_definitions(QCurl PUBLIC QCURL_WEBSOCKET_SUPPORT)
endif()

if(QCURL_HTTP2_SUPPORT)
    target_compile_definitions(QCurl PUBLIC QCURL_HTTP2_SUPPORT)
endif()

# 安装头文件
install(FILES ${QCURL_INSTALL_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qcurl
        COMPONENT Development)

install(FILES ${QCURL_INSTALL_HEADERS_BLOCKING_EXTRAS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qcurl
        COMPONENT BlockingExtrasDevelopment)

install(FILES ${QCURL_INSTALL_HEADERS_TEST_SUPPORT}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qcurl
        COMPONENT TestSupportDevelopment)

install(FILES ${QCURL_INSTALL_HEADERS_OTHER_EXTRAS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qcurl
        COMPONENT OtherExtrasDevelopment)

# 安装配置头文件
install(FILES ${CMAKE_BINARY_DIR}/src/QCurlConfig.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qcurl
        COMPONENT Development)
