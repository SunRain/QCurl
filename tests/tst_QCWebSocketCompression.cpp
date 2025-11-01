/**
 * @file tst_QCWebSocketCompression.cpp
 * @brief QCWebSocket 压缩功能单元测试
 * 
 * 测试 RFC 7692 permessage-deflate 扩展的实现。
 * 
 */

#include <QtTest/QtTest>
#include "QCWebSocketCompressionConfig.h"

#ifdef QCURL_WEBSOCKET_SUPPORT

QT_BEGIN_NAMESPACE

using namespace QCurl;

class tst_QCWebSocketCompression : public QObject
{
    Q_OBJECT

private slots:
    // 配置测试
    void testCompressionConfig_DefaultValues();
    void testCompressionConfig_PresetConfigs();
    void testCompressionConfig_ExtensionHeader();
    void testCompressionConfig_ParseExtensionHeader();
    
    // 算法测试（集成测试需要实际 WebSocket 服务器）
    void testCompressionConfig_WindowBitsValidation();
    void testCompressionConfig_CompressionLevelValidation();
};

// ============================================================================
// 配置测试
// ============================================================================

void tst_QCWebSocketCompression::testCompressionConfig_DefaultValues()
{
    QCWebSocketCompressionConfig config;
    
    QCOMPARE(config.enabled, false);
    QCOMPARE(config.clientMaxWindowBits, 15);
    QCOMPARE(config.serverMaxWindowBits, 15);
    QCOMPARE(config.clientNoContextTakeover, false);
    QCOMPARE(config.serverNoContextTakeover, false);
    QCOMPARE(config.compressionLevel, 6);
}

void tst_QCWebSocketCompression::testCompressionConfig_PresetConfigs()
{
    // 默认配置
    auto defaultConfig = QCWebSocketCompressionConfig::defaultConfig();
    QVERIFY(defaultConfig.enabled);
    QCOMPARE(defaultConfig.clientMaxWindowBits, 15);
    QCOMPARE(defaultConfig.compressionLevel, 6);
    QVERIFY(!defaultConfig.clientNoContextTakeover);
    
    // 低内存配置
    auto lowMemConfig = QCWebSocketCompressionConfig::lowMemoryConfig();
    QVERIFY(lowMemConfig.enabled);
    QCOMPARE(lowMemConfig.clientMaxWindowBits, 9);
    QCOMPARE(lowMemConfig.compressionLevel, 3);
    QVERIFY(lowMemConfig.clientNoContextTakeover);
    
    // 最大压缩配置
    auto maxCompConfig = QCWebSocketCompressionConfig::maxCompressionConfig();
    QVERIFY(maxCompConfig.enabled);
    QCOMPARE(maxCompConfig.clientMaxWindowBits, 15);
    QCOMPARE(maxCompConfig.compressionLevel, 9);
    QVERIFY(!maxCompConfig.clientNoContextTakeover);
}

void tst_QCWebSocketCompression::testCompressionConfig_ExtensionHeader()
{
    QCWebSocketCompressionConfig config;
    config.enabled = false;
    
    // 禁用时不生成头
    QString header = config.toExtensionHeader();
    QVERIFY(header.isEmpty());
    
    // 启用且使用默认值
    config.enabled = true;
    header = config.toExtensionHeader();
    QVERIFY(header.contains("permessage-deflate"));
    QVERIFY(!header.contains("client_max_window_bits"));  // 默认15不显示
    QVERIFY(!header.contains("server_max_window_bits"));
    
    // 自定义窗口大小
    config.clientMaxWindowBits = 12;
    config.serverMaxWindowBits = 10;
    header = config.toExtensionHeader();
    QVERIFY(header.contains("client_max_window_bits=12"));
    QVERIFY(header.contains("server_max_window_bits=10"));
    
    // 无上下文接管
    config.clientNoContextTakeover = true;
    config.serverNoContextTakeover = true;
    header = config.toExtensionHeader();
    QVERIFY(header.contains("client_no_context_takeover"));
    QVERIFY(header.contains("server_no_context_takeover"));
}

void tst_QCWebSocketCompression::testCompressionConfig_ParseExtensionHeader()
{
    // 空字符串
    auto config1 = QCWebSocketCompressionConfig::fromExtensionHeader("");
    QVERIFY(!config1.enabled);
    
    // 基本 permessage-deflate
    auto config2 = QCWebSocketCompressionConfig::fromExtensionHeader("permessage-deflate");
    QVERIFY(config2.enabled);
    QCOMPARE(config2.clientMaxWindowBits, 15);  // 默认值
    
    // 带窗口大小参数
    auto config3 = QCWebSocketCompressionConfig::fromExtensionHeader(
        "permessage-deflate; client_max_window_bits=12; server_max_window_bits=10"
    );
    QVERIFY(config3.enabled);
    QCOMPARE(config3.clientMaxWindowBits, 12);
    QCOMPARE(config3.serverMaxWindowBits, 10);
    
    // 带无上下文接管
    auto config4 = QCWebSocketCompressionConfig::fromExtensionHeader(
        "permessage-deflate; client_no_context_takeover; server_no_context_takeover"
    );
    QVERIFY(config4.enabled);
    QVERIFY(config4.clientNoContextTakeover);
    QVERIFY(config4.serverNoContextTakeover);
    
    // 完整参数
    auto config5 = QCWebSocketCompressionConfig::fromExtensionHeader(
        "permessage-deflate; client_max_window_bits=9; server_max_window_bits=9; "
        "client_no_context_takeover; server_no_context_takeover"
    );
    QVERIFY(config5.enabled);
    QCOMPARE(config5.clientMaxWindowBits, 9);
    QCOMPARE(config5.serverMaxWindowBits, 9);
    QVERIFY(config5.clientNoContextTakeover);
    QVERIFY(config5.serverNoContextTakeover);
}

void tst_QCWebSocketCompression::testCompressionConfig_WindowBitsValidation()
{
    // 测试窗口大小范围（8-14，15 是默认值不显示）
    for (int bits = 8; bits <= 14; ++bits) {
        QCWebSocketCompressionConfig config;
        config.enabled = true;
        config.clientMaxWindowBits = bits;
        config.serverMaxWindowBits = bits;
        
        QString header = config.toExtensionHeader();
        QVERIFY(header.contains(QString("client_max_window_bits=%1").arg(bits)));
        
        // 测试往返转换
        auto parsed = QCWebSocketCompressionConfig::fromExtensionHeader(header);
        QCOMPARE(parsed.clientMaxWindowBits, bits);
        QCOMPARE(parsed.serverMaxWindowBits, bits);
    }
    
    // 测试默认值 15（不在头中显示）
    QCWebSocketCompressionConfig config15;
    config15.enabled = true;
    config15.clientMaxWindowBits = 15;
    QString header15 = config15.toExtensionHeader();
    QVERIFY(!header15.contains("client_max_window_bits"));  // 默认值不显示
}

void tst_QCWebSocketCompression::testCompressionConfig_CompressionLevelValidation()
{
    // 测试压缩级别范围（1-9）
    for (int level = 1; level <= 9; ++level) {
        QCWebSocketCompressionConfig config = QCWebSocketCompressionConfig::defaultConfig();
        config.compressionLevel = level;
        
        // 压缩级别不影响扩展头
        QString header = config.toExtensionHeader();
        QVERIFY(!header.contains("compression_level"));  // 不是 RFC 7692 参数
        
        // 验证级别存储正确
        QCOMPARE(config.compressionLevel, level);
    }
}

QT_END_NAMESPACE

QTEST_MAIN(tst_QCWebSocketCompression)
#include "tst_QCWebSocketCompression.moc"

#else
// WebSocket 支持未启用，跳过测试
QTEST_MAIN(QObject)
#endif // QCURL_WEBSOCKET_SUPPORT
