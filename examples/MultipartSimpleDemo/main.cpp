/**
 * @file main.cpp
 * @brief Multipart/form-data 简单示例
 * @author QCurl Project
 * @date 2025-11-07
 *
 * 演示 QCMultipartFormData 的基本用法，不需要实际发送网络请求
 */

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QCMultipartFormData.h>

using namespace QCurl;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "========================================";
    qDebug() << "QCurl Multipart/form-data 简单示例";
    qDebug() << "========================================\n";

    // ========== 示例 1: 纯文本字段 ==========
    qDebug() << "示例 1: 纯文本字段编码";
    {
        QCMultipartFormData formData;
        formData.addTextField("username", "alice");
        formData.addTextField("email", "alice@example.com");
        formData.addTextField("age", "25");

        qDebug() << "字段数量:" << formData.fieldCount();
        qDebug() << "Content-Type:" << formData.contentType();
        qDebug() << "Boundary:" << formData.boundary();
        qDebug() << "总大小:" << formData.size() << "字节";

        QByteArray encoded = formData.toByteArray();
        qDebug() << "编码结果 (" << encoded.size() << " 字节):";
        qDebug() << "----------------------------------------";
        qDebug() << encoded.constData();
        qDebug() << "----------------------------------------\n";
    }

    // ========== 示例 2: 文本 + 文件字段（从 QByteArray）==========
    qDebug() << "示例 2: 文本 + 文件字段（内存数据）";
    {
        QCMultipartFormData formData;
        formData.addTextField("userId", "12345");
        formData.addTextField("description", "用户头像上传");

        // 模拟一个小文件（实际场景中这是图片数据）
        QByteArray fileData = "这是模拟的图片文件数据...";
        formData.addFileField("avatar", "photo.jpg", fileData, "image/jpeg");

        qDebug() << "字段数量:" << formData.fieldCount();
        qDebug() << "Content-Type:" << formData.contentType();
        qDebug() << "总大小:" << formData.size() << "字节";

        QByteArray encoded = formData.toByteArray();
        qDebug() << "编码结果 (" << encoded.size() << " 字节):";
        qDebug() << "----------------------------------------";
        qDebug() << encoded.constData();
        qDebug() << "----------------------------------------\n";
    }

    // ========== 示例 3: 从文件路径添加 ==========
    qDebug() << "示例 3: 从文件路径添加（创建临时测试文件）";
    {
        // 创建一个临时测试文件
        QString tempFilePath = "/tmp/qcurl_test_file.txt";
        QFile tempFile(tempFilePath);
        if (tempFile.open(QIODevice::WriteOnly)) {
            tempFile.write("这是一个测试文件的内容。\n");
            tempFile.write("可以是任意格式的文件。\n");
            tempFile.close();
        }

        QCMultipartFormData formData;
        formData.addTextField("uploadType", "document");

        // 从文件路径添加（会自动读取文件内容和推断 MIME 类型）
        bool success = formData.addFileField("document", tempFilePath);
        if (success) {
            qDebug() << "成功添加文件:" << tempFilePath;
            qDebug() << "字段数量:" << formData.fieldCount();
            qDebug() << "Content-Type:" << formData.contentType();
            qDebug() << "总大小:" << formData.size() << "字节";

            QByteArray encoded = formData.toByteArray();
            qDebug() << "\n编码结果 (" << encoded.size() << " 字节，仅显示前 500 字节):";
            qDebug() << "----------------------------------------";
            qDebug() << encoded.left(500).constData();
            qDebug() << "... (剩余" << (encoded.size() - 500) << "字节)";
            qDebug() << "----------------------------------------\n";
        } else {
            qDebug() << "添加文件失败！";
        }

        // 清理临时文件
        QFile::remove(tempFilePath);
    }

    // ========== 示例 4: 多文件上传 ==========
    qDebug() << "示例 4: 多文件上传";
    {
        QCMultipartFormData formData;
        formData.addTextField("projectId", "P-2023-001");
        formData.addTextField("category", "reports");

        // 添加多个文件
        QByteArray file1Data = "文件1的内容...";
        QByteArray file2Data = "文件2的内容...";
        QByteArray file3Data = "文件3的内容...";

        formData.addFileField("files", "report1.pdf", file1Data, "application/pdf");
        formData.addFileField("files", "report2.pdf", file2Data, "application/pdf");
        formData.addFileField("files", "data.csv", file3Data, "text/csv");

        qDebug() << "字段数量:" << formData.fieldCount();
        qDebug() << "Content-Type:" << formData.contentType();
        qDebug() << "总大小:" << formData.size() << "字节";
        qDebug() << "";
    }

    // ========== 示例 5: 流式字段检测 ==========
    qDebug() << "示例 5: 流式字段检测";
    {
        QCMultipartFormData formData1;
        formData1.addTextField("name", "test");
        formData1.addFileField("file", "test.txt", QByteArray("data"), "text/plain");

        qDebug() << "formData1 包含流式字段:" << (formData1.hasStreamFields() ? "是" : "否");

        // 创建一个带流式字段的表单
        QString tempFilePath = "/tmp/qcurl_stream_test.txt";
        QFile tempFile(tempFilePath);
        if (tempFile.open(QIODevice::WriteOnly)) {
            tempFile.write("流式字段测试数据");
            tempFile.close();
        }

        QFile *streamFile = new QFile(tempFilePath);
        if (streamFile->open(QIODevice::ReadOnly)) {
            QCMultipartFormData formData2;
            formData2.addTextField("name", "stream-test");
            formData2.addFileFieldStream("bigfile", streamFile, "large.bin", "application/octet-stream");

            qDebug() << "formData2 包含流式字段:" << (formData2.hasStreamFields() ? "是" : "否");
            qDebug() << "注意：流式字段不会包含在 toByteArray() 返回的数据中";

            streamFile->close();
        }

        delete streamFile;
        QFile::remove(tempFilePath);
    }

    qDebug() << "\n========================================";
    qDebug() << "所有示例完成！";
    qDebug() << "========================================";

    return 0;
}
