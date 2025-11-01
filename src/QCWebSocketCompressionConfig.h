#ifndef QCWEBSOCKETCOMPRESSIONCONFIG_H
#define QCWEBSOCKETCOMPRESSIONCONFIG_H

#include "QCurlConfig.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

#include <QString>

QT_BEGIN_NAMESPACE

namespace QCurl {

/**
 * @brief WebSocket 压缩配置（RFC 7692 - permessage-deflate）
 * 
 * 配置 WebSocket permessage-deflate 扩展参数，用于协商压缩设置。
 * 
 * permessage-deflate 是 WebSocket 的标准压缩扩展，使用 DEFLATE 算法
 * （与 gzip 相同）对每个 WebSocket 消息进行压缩，显著减少传输数据量。
 * 
 * @par 基本用法
 * @code
 * QCWebSocketCompressionConfig config = QCWebSocketCompressionConfig::defaultConfig();
 * socket->setCompressionConfig(config);
 * socket->open();
 * @endcode
 * 
 * @par 自定义配置
 * @code
 * QCWebSocketCompressionConfig config;
 * config.enabled = true;
 * config.clientMaxWindowBits = 12;  // 降低内存使用
 * config.compressionLevel = 6;      // 平衡压缩率和速度
 * @endcode
 * 
 */
struct QCWebSocketCompressionConfig
{
    /**
     * @brief 是否启用压缩（默认 false）
     * 
     * 设置为 true 时，客户端会在握手时请求 permessage-deflate 扩展。
     * 服务器可能接受、拒绝或修改压缩参数。
     */
    bool enabled = false;
    
    /**
     * @brief 客户端最大窗口位数（8-15，默认 15）
     * 
     * 控制客户端的 LZ77 滑动窗口大小 (2^bits)。
     * - 15: 32KB 窗口（最佳压缩率，更高内存）
     * - 9:  512B 窗口（较低内存，较差压缩率）
     * 
     * 值越大，压缩率越好，但内存占用越高。
     */
    int clientMaxWindowBits = 15;
    
    /**
     * @brief 服务器最大窗口位数（8-15，默认 15）
     * 
     * 客户端建议服务器使用的窗口大小。
     * 服务器可以选择更小的值，但不能超过此限制。
     */
    int serverMaxWindowBits = 15;
    
    /**
     * @brief 客户端无上下文接管（默认 false）
     * 
     * 如果设置为 true，客户端每次压缩都重置压缩器状态，
     * 不保留上一条消息的压缩上下文。
     * 
     * 优点：减少内存占用，避免状态累积
     * 缺点：降低压缩率（无法利用消息间的相似性）
     */
    bool clientNoContextTakeover = false;
    
    /**
     * @brief 服务器无上下文接管（默认 false）
     * 
     * 如果设置为 true，要求服务器每次压缩都重置状态。
     */
    bool serverNoContextTakeover = false;
    
    /**
     * @brief 压缩级别（1-9，默认 6）
     * 
     * zlib 压缩级别：
     * - 1: 最快速度，最低压缩率
     * - 6: 平衡（推荐）
     * - 9: 最高压缩率，最慢速度
     * 
     * 通常使用 6 提供良好的压缩率和性能平衡。
     */
    int compressionLevel = 6;
    
    /**
     * @brief 生成 Sec-WebSocket-Extensions 请求头
     * 
     * 将配置转换为 RFC 7692 格式的扩展协商字符串。
     * 
     * @return 扩展请求头字符串
     * 
     * 示例输出:
     * "permessage-deflate; client_max_window_bits=15; server_max_window_bits=15"
     */
    [[nodiscard]] QString toExtensionHeader() const;
    
    /**
     * @brief 从响应头解析压缩参数
     * 
     * 解析服务器返回的 Sec-WebSocket-Extensions 头，
     * 提取实际协商的压缩参数。
     * 
     * @param header 服务器响应的扩展头
     * @return 解析后的配置
     * 
     * 示例输入:
     * "permessage-deflate; server_max_window_bits=12"
     */
    static QCWebSocketCompressionConfig fromExtensionHeader(const QString &header);
    
    /**
     * @brief 默认配置（启用压缩，标准参数）
     * 
     * @return 推荐的默认压缩配置
     * 
     * 默认配置：
     * - enabled = true
     * - client/server MaxWindowBits = 15（最佳压缩）
     * - 无上下文接管 = false（最佳压缩率）
     * - 压缩级别 = 6（平衡）
     */
    static QCWebSocketCompressionConfig defaultConfig();
    
    /**
     * @brief 低内存配置
     * 
     * @return 适合内存受限环境的配置
     * 
     * 配置特点：
     * - 较小的窗口大小（9 = 512B）
     * - 启用无上下文接管
     * - 较低的压缩级别（3）
     */
    static QCWebSocketCompressionConfig lowMemoryConfig();
    
    /**
     * @brief 最大压缩率配置
     * 
     * @return 追求最佳压缩率的配置（牺牲速度和内存）
     * 
     * 配置特点：
     * - 最大窗口大小（15 = 32KB）
     * - 禁用无上下文接管
     * - 最高压缩级别（9）
     */
    static QCWebSocketCompressionConfig maxCompressionConfig();
};

} // namespace QCurl

QT_END_NAMESPACE

#endif // QCURL_WEBSOCKET_SUPPORT

#endif // QCWEBSOCKETCOMPRESSIONCONFIG_H
