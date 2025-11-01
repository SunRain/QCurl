/**
 * @file tst_QCMultipartFormData.cpp
 * @brief QCMultipartFormData 类单元测试
 * @author QCurl Project
 * @date 2025-11-07
 *
 * 测试 QCMultipartFormData 的所有公共 API，包括：
 * - 基础功能（构造、清空、计数）
 * - 文本字段（添加、编码）
 * - 文件字段（内存、路径、流式）
 * - 编码输出（toByteArray、contentType、boundary）
 * - 边界情况（空表单、特殊字符、大文件）
 */

#include <QtTest/QtTest>
#include <QCMultipartFormData.h>
#include <QFile>
#include <QBuffer>
#include <QTemporaryFile>

using namespace QCurl;

class TestQCMultipartFormData : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ========== 基础功能测试（3 个）==========
    void testConstructor();
    void testClear();
    void testFieldCount();

    // ========== 文本字段测试（3 个）==========
    void testAddTextField();
    void testAddMultipleTextFields();
    void testTextFieldEncoding();

    // ========== 文件字段测试（6 个）==========
    void testAddFileFieldFromByteArray();
    void testFileFieldEncoding();
    void testMultipleFileFields();
    void testAddFileFieldFromPath();
    void testAddFileFieldInvalidPath();
    void testMimeTypeDetection();

    // ========== 流式字段测试（3 个）==========
    void testAddFileFieldStream();
    void testHasStreamFields();
    void testStreamFieldNotInByteArray();

    // ========== 编码输出测试（4 个）==========
    void testToByteArray();
    void testContentType();
    void testBoundary();
    void testSize();

    // ========== 边界情况测试（3 个）==========
    void testEmptyFormData();
    void testSpecialCharactersInFields();
    void testLargeFileField();

private:
    QString m_tempDir;
};

// ============================================================================
// 测试套件初始化和清理
// ============================================================================

void TestQCMultipartFormData::initTestCase()
{
    qDebug() << "========================================";
    qDebug() << "QCMultipartFormData 单元测试";
    qDebug() << "========================================";

    m_tempDir = QDir::tempPath() + "/qcurl_test_multipart";
    QDir().mkpath(m_tempDir);
}

void TestQCMultipartFormData::cleanupTestCase()
{
    // 清理临时目录
    QDir dir(m_tempDir);
    dir.removeRecursively();

    qDebug() << "========================================";
    qDebug() << "测试套件完成";
    qDebug() << "========================================";
}

// ============================================================================
// 基础功能测试
// ============================================================================

void TestQCMultipartFormData::testConstructor()
{
    QCMultipartFormData form;

    // 验证 boundary 已生成
    QVERIFY(!form.boundary().isEmpty());
    QVERIFY(form.boundary().startsWith("----QCurlBoundary"));
    QVERIFY(form.boundary().length() > 20);

    // 验证初始状态
    QCOMPARE(form.fieldCount(), 0);
    QVERIFY(!form.hasStreamFields());
}

void TestQCMultipartFormData::testClear()
{
    QCMultipartFormData form;

    // 添加一些字段
    form.addTextField("name", "value");
    form.addTextField("email", "test@example.com");
    QCOMPARE(form.fieldCount(), 2);

    // 清空
    form.clear();
    QCOMPARE(form.fieldCount(), 0);

    // 再次添加应该可以正常工作
    form.addTextField("new", "field");
    QCOMPARE(form.fieldCount(), 1);
}

void TestQCMultipartFormData::testFieldCount()
{
    QCMultipartFormData form;

    QCOMPARE(form.fieldCount(), 0);

    form.addTextField("field1", "value1");
    QCOMPARE(form.fieldCount(), 1);

    form.addTextField("field2", "value2");
    QCOMPARE(form.fieldCount(), 2);

    QByteArray fileData = "test";
    form.addFileField("file", "test.txt", fileData, "text/plain");
    QCOMPARE(form.fieldCount(), 3);
}

// ============================================================================
// 文本字段测试
// ============================================================================

void TestQCMultipartFormData::testAddTextField()
{
    QCMultipartFormData form;
    form.addTextField("username", "alice");

    QCOMPARE(form.fieldCount(), 1);

    QByteArray encoded = form.toByteArray();
    QVERIFY(encoded.contains("Content-Disposition: form-data; name=\"username\""));
    QVERIFY(encoded.contains("alice"));
}

void TestQCMultipartFormData::testAddMultipleTextFields()
{
    QCMultipartFormData form;
    form.addTextField("username", "alice");
    form.addTextField("email", "alice@example.com");
    form.addTextField("age", "25");

    QCOMPARE(form.fieldCount(), 3);

    QByteArray encoded = form.toByteArray();
    QVERIFY(encoded.contains("username"));
    QVERIFY(encoded.contains("alice"));
    QVERIFY(encoded.contains("email"));
    QVERIFY(encoded.contains("alice@example.com"));
    QVERIFY(encoded.contains("age"));
    QVERIFY(encoded.contains("25"));
}

void TestQCMultipartFormData::testTextFieldEncoding()
{
    QCMultipartFormData form;

    // 测试包含换行的文本
    form.addTextField("message", "Hello\nWorld\r\n测试");

    QByteArray encoded = form.toByteArray();
    QVERIFY(encoded.contains("Hello\nWorld\r\n测试"));

    // 测试空值
    form.clear();
    form.addTextField("empty", "");
    encoded = form.toByteArray();
    QVERIFY(encoded.contains("name=\"empty\""));
}

// ============================================================================
// 文件字段测试（内存数据）
// ============================================================================

void TestQCMultipartFormData::testAddFileFieldFromByteArray()
{
    QCMultipartFormData form;
    QByteArray fileData = "File content here";
    form.addFileField("file", "test.txt", fileData, "text/plain");

    QCOMPARE(form.fieldCount(), 1);

    QByteArray encoded = form.toByteArray();
    QVERIFY(encoded.contains("Content-Disposition: form-data; name=\"file\"; filename=\"test.txt\""));
    QVERIFY(encoded.contains("Content-Type: text/plain"));
    QVERIFY(encoded.contains(fileData));
}

void TestQCMultipartFormData::testFileFieldEncoding()
{
    QCMultipartFormData form;
    QByteArray fileData = "Binary\x00Data\xFF";
    form.addFileField("binary", "data.bin", fileData, "application/octet-stream");

    QByteArray encoded = form.toByteArray();
    QVERIFY(encoded.contains("filename=\"data.bin\""));
    QVERIFY(encoded.contains("Content-Type: application/octet-stream"));
    QVERIFY(encoded.contains(fileData));
}

void TestQCMultipartFormData::testMultipleFileFields()
{
    QCMultipartFormData form;

    form.addFileField("file1", "test1.txt", QByteArray("Content 1"), "text/plain");
    form.addFileField("file2", "test2.txt", QByteArray("Content 2"), "text/plain");
    form.addFileField("file3", "test3.txt", QByteArray("Content 3"), "text/plain");

    QCOMPARE(form.fieldCount(), 3);

    QByteArray encoded = form.toByteArray();
    QVERIFY(encoded.contains("test1.txt"));
    QVERIFY(encoded.contains("Content 1"));
    QVERIFY(encoded.contains("test2.txt"));
    QVERIFY(encoded.contains("Content 2"));
    QVERIFY(encoded.contains("test3.txt"));
    QVERIFY(encoded.contains("Content 3"));
}

// ============================================================================
// 文件字段测试（文件路径）
// ============================================================================

void TestQCMultipartFormData::testAddFileFieldFromPath()
{
    // 创建临时文件
    QString tempPath = m_tempDir + "/test_file.txt";
    QFile tempFile(tempPath);
    QVERIFY(tempFile.open(QIODevice::WriteOnly));
    tempFile.write("Test file content");
    tempFile.close();

    QCMultipartFormData form;
    QVERIFY(form.addFileField("document", tempPath, "text/plain"));

    QCOMPARE(form.fieldCount(), 1);

    QByteArray encoded = form.toByteArray();
    QVERIFY(encoded.contains("Test file content"));
    QVERIFY(encoded.contains("filename=\"test_file.txt\""));

    // 清理
    QFile::remove(tempPath);
}

void TestQCMultipartFormData::testAddFileFieldInvalidPath()
{
    QCMultipartFormData form;

    // 尝试添加不存在的文件
    bool success = form.addFileField("file", "/nonexistent/file.txt");
    QVERIFY(!success);

    // 字段应该没有被添加
    QCOMPARE(form.fieldCount(), 0);
}

void TestQCMultipartFormData::testMimeTypeDetection()
{
    QCMultipartFormData form;

    // 创建不同类型的临时文件
    QString jpgPath = m_tempDir + "/test.jpg";
    QFile jpgFile(jpgPath);
    QVERIFY(jpgFile.open(QIODevice::WriteOnly));
    jpgFile.write("fake jpg");
    jpgFile.close();

    // 不指定 MIME 类型，应该自动推断
    QVERIFY(form.addFileField("image", jpgPath));

    QByteArray encoded = form.toByteArray();
    // QMimeDatabase 应该推断为 image/jpeg
    QVERIFY(encoded.contains("Content-Type: image/jpeg") ||
            encoded.contains("Content-Type: image/jpg"));

    // 清理
    QFile::remove(jpgPath);
}

// ============================================================================
// 流式字段测试
// ============================================================================

void TestQCMultipartFormData::testAddFileFieldStream()
{
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    buffer.write("Stream data");
    buffer.seek(0);

    QCMultipartFormData form;
    QVERIFY(form.addFileFieldStream("stream", &buffer, "stream.bin", "application/octet-stream"));

    QCOMPARE(form.fieldCount(), 1);
    QVERIFY(form.hasStreamFields());
}

void TestQCMultipartFormData::testHasStreamFields()
{
    QCMultipartFormData form;

    // 没有流式字段
    QVERIFY(!form.hasStreamFields());

    // 添加文本字段
    form.addTextField("name", "value");
    QVERIFY(!form.hasStreamFields());

    // 添加内存文件字段
    form.addFileField("file", "test.txt", QByteArray("data"), "text/plain");
    QVERIFY(!form.hasStreamFields());

    // 添加流式字段
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    buffer.write("Stream");
    buffer.seek(0);
    form.addFileFieldStream("stream", &buffer, "file.bin", "application/octet-stream");

    QVERIFY(form.hasStreamFields());
}

void TestQCMultipartFormData::testStreamFieldNotInByteArray()
{
    // ✅ v2.13.0 修复: 流式字段现在被序列化了
    // 更新为验证新行为：流式字段现在被包含在 toByteArray() 中
    
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    buffer.write("Stream data");
    buffer.seek(0);

    QCMultipartFormData form;
    form.addTextField("text", "value");
    form.addFileFieldStream("stream", &buffer, "stream.bin", "application/octet-stream");

    QCOMPARE(form.fieldCount(), 2);
    QVERIFY(form.hasStreamFields());

    // ✅ 新行为: 流式字段现在被包含在编码中（v2.13.0修复）
    QByteArray encoded = form.toByteArray();
    QVERIFY(encoded.contains("text"));
    QVERIFY(encoded.contains("value"));
    QVERIFY(encoded.contains("Stream data"));  // ← 新增验证
    
    // 验证 multipart 结构正确
    QVERIFY(encoded.contains("name=\"stream\""));
    QVERIFY(encoded.contains("filename=\"stream.bin\""));
    QVERIFY(encoded.contains("Content-Type: application/octet-stream"));
}

// ============================================================================
// 编码输出测试
// ============================================================================

void TestQCMultipartFormData::testToByteArray()
{
    QCMultipartFormData form;
    form.addTextField("name", "alice");
    form.addFileField("file", "test.txt", QByteArray("content"), "text/plain");

    QByteArray encoded = form.toByteArray();

    // 验证结构
    QVERIFY(encoded.contains("----QCurlBoundary"));
    QVERIFY(encoded.contains("Content-Disposition: form-data"));
    QVERIFY(encoded.contains("name=\"name\""));
    QVERIFY(encoded.contains("alice"));
    QVERIFY(encoded.contains("filename=\"test.txt\""));
    QVERIFY(encoded.contains("content"));

    // 验证结束标记
    QString boundary = form.boundary();
    QVERIFY(encoded.endsWith((boundary + "--\r\n").toUtf8()));
}

void TestQCMultipartFormData::testContentType()
{
    QCMultipartFormData form;
    QString contentType = form.contentType();

    QVERIFY(contentType.startsWith("multipart/form-data; boundary="));
    QVERIFY(contentType.contains(form.boundary()));

    // 验证格式
    QVERIFY(contentType.contains("----QCurlBoundary"));
}

void TestQCMultipartFormData::testBoundary()
{
    QCMultipartFormData form;
    QString boundary = form.boundary();

    // 验证格式
    QVERIFY(boundary.startsWith("----QCurlBoundary"));
    QVERIFY(boundary.length() > 20);

    // 验证唯一性（多次创建应该生成不同的 boundary）
    QCMultipartFormData form2;
    QVERIFY(form.boundary() != form2.boundary());
}

void TestQCMultipartFormData::testSize()
{
    QCMultipartFormData form;
    form.addTextField("name", "value");

    qint64 size = form.size();
    QVERIFY(size > 0);

    QByteArray encoded = form.toByteArray();
    QCOMPARE(size, encoded.size());

    // 添加更多字段，大小应该增加
    form.addTextField("email", "test@example.com");
    qint64 newSize = form.size();
    QVERIFY(newSize > size);
}

// ============================================================================
// 边界情况测试
// ============================================================================

void TestQCMultipartFormData::testEmptyFormData()
{
    QCMultipartFormData form;

    QCOMPARE(form.fieldCount(), 0);

    QByteArray encoded = form.toByteArray();
    QString boundary = form.boundary();

    // 空表单应该只有结束标记或为空
    // 不同实现可能有不同的处理方式
    QVERIFY(encoded.isEmpty() ||
            encoded == (boundary + "--\r\n").toUtf8() ||
            encoded == ("--" + boundary + "--\r\n").toUtf8());
}

void TestQCMultipartFormData::testSpecialCharactersInFields()
{
    QCMultipartFormData form;

    // 测试特殊字符
    form.addTextField("special", "Value with \"quotes\" and \r\n newlines");
    form.addTextField("unicode", "中文字符 🎉 emoji");

    QByteArray encoded = form.toByteArray();
    QVERIFY(!encoded.isEmpty());
    QVERIFY(encoded.contains("quotes"));
    QVERIFY(encoded.contains("中文字符"));
}

void TestQCMultipartFormData::testLargeFileField()
{
    QCMultipartFormData form;

    // 创建一个较大的文件（1MB）
    QByteArray largeData(1024 * 1024, 'X');
    form.addFileField("large", "large.bin", largeData, "application/octet-stream");

    QCOMPARE(form.fieldCount(), 1);

    qint64 size = form.size();
    QVERIFY(size > 1024 * 1024);

    QByteArray encoded = form.toByteArray();
    QVERIFY(encoded.size() > 1024 * 1024);
    QVERIFY(encoded.contains("large.bin"));
}

// ============================================================================
// Qt Test 主函数
// ============================================================================

QTEST_MAIN(TestQCMultipartFormData)
#include "tst_QCMultipartFormData.moc"
