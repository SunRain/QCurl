# QCurl 库源文件配置

set(QCURL_CORE_SOURCES
    CurlGlobalConstructor.cpp
    QCGlobal.cpp
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

set(QCURL_BLOCKING_EXTRAS_SOURCES
    QCBlockingCookieStore.cpp
    QCBlockingNetworkClient.cpp
    QCBlockingNetworkClientOptions.cpp
    QCBlockingNetworkResult.cpp
    private/QCBlockingCurlAdapter.cpp
    private/QCBlockingCurlMethodSetup.cpp
    private/QCBlockingCurlRequestSetup.cpp
    private/QCBlockingCurlRequestSetupExtensions.cpp
    private/QCBlockingRequestBody.cpp
)

set(QCURL_TEST_SUPPORT_SOURCES)

set(QCURL_OTHER_EXTRAS_SOURCES
    QCNetworkDiagnostics.cpp        # Other Extras / Preview: 网络诊断工具
    QCNetworkMiddlewareExtras.cpp   # Middleware Extras opt-in 实现
)

set(QCURL_FEATURE_SOURCES
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
    QCNetworkRequestScheduler.cpp   # Core / Stable: 请求优先级调度
    QCMultipartFormData.cpp         # Core / Stable: Multipart 表单数据
    QCNetworkCache.cpp              # Core / Stable: 缓存基类
    QCNetworkMemoryCache.cpp        # Core / Stable: 内存缓存
    QCNetworkDiskCache.cpp          # Core / Stable: 磁盘缓存
    QCNetworkConnectionPoolConfig.cpp
    QCNetworkConnectionPoolStatistics.cpp
    QCNetworkConnectionPoolManager.cpp  # Core / Stable: 连接池管理器
    QCNetworkLogger.cpp             # Core / Stable: 日志系统
    QCNetworkDefaultLogger.cpp      # Core / Stable: 默认日志实现
    QCNetworkLogRedaction.cpp       # Core / Stable: 日志脱敏工具
    QCNetworkMiddleware.cpp         # Core / Stable: 中间件 base
    QCNetworkCancelToken.cpp        # Core / Stable: 取消令牌
    QCNetworkMockHandler.cpp        # Test Support / Explicit opt-in: Mock 工具
    QCNetworkCapturedRequest.cpp
    QCNetworkTransferJob.cpp
    QCNetworkDownloadToDeviceJob.cpp
    QCNetworkResumableDownloadJob.cpp
    private/QCNetworkResumableDownloadWriter.cpp
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
    QCNetworkRequestPriority.h      # Core / Stable: 优先级枚举
    QCNetworkRequestScheduler.h     # Core / Stable: 请求调度器
    QCMultipartFormData.h           # Core / Stable: Multipart 表单数据
    QCNetworkCachePolicy.h          # Core / Stable: 缓存策略枚举
    QCNetworkCache.h                # Core / Stable: 缓存基类
    QCNetworkMemoryCache.h          # Core / Stable: 内存缓存
    QCNetworkDiskCache.h            # Core / Stable: 磁盘缓存
    QCNetworkConnectionPoolConfig.h # Core / Stable: 连接池配置
    QCNetworkConnectionPoolManager.h # Core / Stable: 连接池管理器
    QCNetworkConnectionPoolManager_p.h # Core / Stable: 连接池内部 companion
    QCNetworkLogger.h               # Core / Stable: 日志系统
    QCNetworkDefaultLogger.h        # Core / Stable: 默认日志实现
    private/QCNetworkLogRedaction_p.h # Core / Stable: 日志脱敏工具（内部）
    QCNetworkMiddleware.h           # Core / Stable: 中间件 base
    QCNetworkMiddlewareExtras.h     # Middleware Extras opt-in surface
    private/QCNetworkMiddlewareInternal_p.h # Core / Stable: 内部策略型 middleware
    QCNetworkCancelToken.h          # Core / Stable: 取消令牌
    QCNetworkTransferJob.h
    QCNetworkDownloadToDeviceJob.h
    QCNetworkResumableDownloadJob.h
    QCNetworkMockHandler.h          # Test Support / Explicit opt-in: Mock 工具
    QCNetworkTestSupport.h          # Test Support / Explicit opt-in: 显式 manager 绑定
    QCNetworkMockHandler_p.h        # Test Support / Explicit opt-in: Mock companion（内部）
    private/QCNetworkResumableDownloadWriter_p.h
    QCNetworkDiagnostics.h          # Other Extras / Preview: 网络诊断工具
)

set(QCURL_WEBSOCKET_PREVIEW_SOURCES
    QCWebSocket.cpp
    QCWebSocketReconnectPolicy.cpp
    QCWebSocketPool.cpp                 # Other Extras / Preview: WebSocket 连接池
    QCWebSocketCompressionConfig.cpp    # Other Extras / Preview: WebSocket 压缩配置
)

# ============================================================================
# WebSocket 支持（Other Extras / Preview）+ 重连机制
# ============================================================================
if(QCURL_WEBSOCKET_SUPPORT)
    list(APPEND QCURL_OTHER_EXTRAS_SOURCES ${QCURL_WEBSOCKET_PREVIEW_SOURCES})
    list(APPEND QCURL_FEATURE_HEADERS
        QCWebSocket.h
        QCWebSocketReconnectPolicy.h
        QCWebSocketPool.h                   # Other Extras / Preview: WebSocket 连接池
        QCWebSocketCompressionConfig.h      # Other Extras / Preview: WebSocket 压缩配置
    )
    list(APPEND QCURL_FEATURE_HEADERS
        QCWebSocket_p.h
    )
    message(STATUS "✅ WebSocket 支持已启用（libcurl ${QCURL_LIBCURL_VERSION_STRING} + Other Extras / Preview）")
else()
    message(STATUS "⏭️ WebSocket 支持未启用（需要 libcurl 7.86.0+）")
endif()

# 合并所有源文件和头文件
set(QCURL_ALL_SOURCES
    ${QCURL_CORE_SOURCES}
    ${QCURL_FEATURE_SOURCES}
    ${QCURL_BLOCKING_EXTRAS_SOURCES}
)
set(QCURL_BLOCKING_EXTRAS_ALL_SOURCES ${QCURL_BLOCKING_EXTRAS_SOURCES})
set(QCURL_TEST_SUPPORT_ALL_SOURCES ${QCURL_TEST_SUPPORT_SOURCES})
set(QCURL_OTHER_EXTRAS_ALL_SOURCES ${QCURL_OTHER_EXTRAS_SOURCES})
set(QCURL_ALL_HEADERS ${QCURL_CORE_HEADERS} ${QCURL_FEATURE_HEADERS})
list(APPEND QCURL_CORE_HEADERS QCNetworkRequestPriority.h)
set(QCURL_OTHER_EXTRAS_HEADERS ${QCURL_INSTALL_HEADERS_OTHER_EXTRAS})
foreach(_qcurl_header IN LISTS QCURL_FEATURE_HEADERS)
    if(_qcurl_header MATCHES "^QCWebSocket")
        list(APPEND QCURL_OTHER_EXTRAS_HEADERS "${_qcurl_header}")
    endif()
endforeach()
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

# 创建 QCurl 库。默认发布 shared；static 需要显式 opt-in，并有独立 gate 证明。
set(_qcurl_library_type STATIC)
if(QCURL_BUILD_SHARED_LIBS)
    set(_qcurl_library_type SHARED)
endif()
add_library(QCurl ${_qcurl_library_type} ${QCURL_ALL_SOURCES} ${QCURL_CORE_HEADERS})

# 添加库别名（用于 CMake 导出）
add_library(QCurl::QCurl ALIAS QCurl)

add_library(QCurlBlockingExtras INTERFACE)
add_library(QCurl::BlockingExtras ALIAS QCurlBlockingExtras)

add_library(QCurlTestSupport INTERFACE)
add_library(QCurl::TestSupport ALIAS QCurlTestSupport)

add_library(QCurlOtherExtras ${_qcurl_library_type}
    ${QCURL_OTHER_EXTRAS_ALL_SOURCES}
    ${QCURL_OTHER_EXTRAS_HEADERS}
)
add_library(QCurl::OtherExtras ALIAS QCurlOtherExtras)

# 设置库属性
set_target_properties(QCurl PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    AUTOMOC ON  # 自动处理 Qt MOC
    EXPORT_NAME QCurl
    POSITION_INDEPENDENT_CODE ON
)
set_target_properties(QCurlOtherExtras PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
)
set_target_properties(QCurlBlockingExtras PROPERTIES EXPORT_NAME BlockingExtras)
set_target_properties(QCurlTestSupport PROPERTIES EXPORT_NAME TestSupport)
set_target_properties(QCurlOtherExtras PROPERTIES EXPORT_NAME OtherExtras)

set(_qcurl_component_targets
    QCurl
    QCurlBlockingExtras
    QCurlTestSupport
    QCurlOtherExtras
)

# 链接依赖
target_link_libraries(QCurl
    PUBLIC
        Qt6::Core
        Qt6::Network
)

target_link_libraries(QCurlBlockingExtras
    INTERFACE
        QCurl
)

target_link_libraries(QCurlTestSupport
    INTERFACE
        QCurl
)

if(NOT QCURL_BUILD_SHARED_LIBS)
    target_link_libraries(QCurlOtherExtras
        PUBLIC
            QCurl
            ZLIB::ZLIB
    )
else()
    target_link_libraries(QCurlOtherExtras
        PUBLIC
            QCurl
        PRIVATE
            ZLIB::ZLIB
    )
endif()

# zlib 仅服务 WebSocket Preview 的 permessage-deflate 实现，由 OtherExtras opt-in
# target 承担；Core 导出 / qcurl.pc 合同不传播 zlib 给默认 Core consumer。

if(QCURL_BUILD_LIBCURL_CONSISTENCY)
    target_include_directories(QCurl PRIVATE "${CMAKE_SOURCE_DIR}/curl/include")
    target_link_libraries(QCurl PRIVATE "$<BUILD_INTERFACE:$<TARGET_FILE:libcurl_shared>>")
    add_dependencies(QCurl libcurl_shared)
    target_include_directories(QCurlOtherExtras PRIVATE "${CMAKE_SOURCE_DIR}/curl/include")
    target_link_libraries(QCurlOtherExtras PRIVATE "$<BUILD_INTERFACE:$<TARGET_FILE:libcurl_shared>>")
    add_dependencies(QCurlOtherExtras libcurl_shared)
else()
    if(NOT QCURL_BUILD_SHARED_LIBS)
        target_link_libraries(QCurl PUBLIC CURL::libcurl)
        target_link_libraries(QCurlOtherExtras PUBLIC CURL::libcurl)
    else()
        target_link_libraries(QCurl PRIVATE CURL::libcurl ZLIB::ZLIB)
        target_link_libraries(QCurlOtherExtras PRIVATE CURL::libcurl)
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
foreach(_qcurl_target IN ITEMS QCurlBlockingExtras QCurlTestSupport)
    target_include_directories(${_qcurl_target}
        INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
            $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>
            $<INSTALL_INTERFACE:include/qcurl>
    )
endforeach()
target_include_directories(QCurlOtherExtras
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>
        $<INSTALL_INTERFACE:include/qcurl>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/private
)

# 编译定义
target_compile_definitions(QCurl
    PRIVATE
        $<$<BOOL:${QCURL_BUILD_SHARED_LIBS}>:QCURL_BUILDING_LIBRARY>
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)
target_compile_definitions(QCurlOtherExtras
    PRIVATE
        $<$<BOOL:${QCURL_BUILD_SHARED_LIBS}>:QCURL_BUILDING_OTHER_EXTRAS_LIBRARY>
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)

# 测试专用：为单元测试提供可控的 capability 分支覆盖（不影响默认构建）
if(BUILD_TESTING)
    target_compile_definitions(QCurl PRIVATE QCURL_ENABLE_TEST_HOOKS)
    target_compile_definitions(QCurlOtherExtras PRIVATE QCURL_ENABLE_TEST_HOOKS)
endif()

# 特性宏定义
if(QCURL_WEBSOCKET_SUPPORT)
    target_compile_definitions(QCurl PUBLIC QCURL_WEBSOCKET_SUPPORT)
    target_compile_definitions(QCurlOtherExtras PUBLIC QCURL_WEBSOCKET_SUPPORT)
endif()

if(QCURL_HTTP2_SUPPORT)
    target_compile_definitions(QCurl PUBLIC QCURL_HTTP2_SUPPORT)
    target_compile_definitions(QCurlOtherExtras PUBLIC QCURL_HTTP2_SUPPORT)
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
