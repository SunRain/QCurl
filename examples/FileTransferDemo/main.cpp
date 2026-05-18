#include "QCNetworkAccessManager.h"
#include "QCNetworkDownloadToDeviceJob.h"
#include "QCNetworkError.h"
#include "QCNetworkMultipartBody.h"
#include "QCNetworkResumableDownloadJob.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

using namespace QCurl;

class FileTransferDemo : public QObject
{
public:
    explicit FileTransferDemo(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_streamFilePath    = QDir::temp().filePath("qcurl_stream_download.bin");
        m_resumableFilePath = QDir::temp().filePath("qcurl_resumable_download.bin");
    }

    void start()
    {
        qDebug() << "========================================";
        qDebug() << "FileTransferDemo - 流式/断点续传示例";
        qDebug() << "httpbin 服务:" << baseUrl();
        qDebug() << "========================================";
        runStreamingDownload();
    }

private:
    QString baseUrl() const
    {
        const QString env = qEnvironmentVariable("QCURL_HTTPBIN_BASE");
        return env.isEmpty() ? QStringLiteral("http://localhost:8935") : env;
    }

    void runStreamingDownload()
    {
        QFile *file = new QFile(m_streamFilePath, this);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning() << "无法打开文件用于写入:" << m_streamFilePath;
            return finishDemo(1);
        }

        QUrl url(baseUrl() + "/stream-bytes/65536?seed=42&chunk_size=4096");
        qDebug() << "[1/3] 开始流式下载:" << url.toString();

        auto *job = new QCNetworkDownloadToDeviceJob(&m_manager, url, file, this);
        connect(job, &QCNetworkTransferJob::progress, this, [](qint64 received, qint64 total) {
            if (total > 0) {
                qDebug() << "  -> 下载进度" << received << "/" << total;
            }
        });

        connect(job, &QCNetworkTransferJob::finished, this, [this, file, job]() {
            file->close();
            if (job->error() == NetworkError::NoError) {
                qDebug() << "  ✓ 流式下载完成，文件位于" << m_streamFilePath;
                runStreamingUpload();
            } else {
                qWarning() << "  ✗ 流式下载失败:" << job->errorString();
                finishDemo(1);
            }
            if (auto *reply = job->reply()) {
                reply->deleteLater();
            }
            job->deleteLater();
            file->deleteLater();
        });
        job->start();
    }

    void runStreamingUpload()
    {
        QFile *file = new QFile(m_streamFilePath, this);
        if (!file->open(QIODevice::ReadOnly)) {
            qWarning() << "无法读取流式下载后的文件" << m_streamFilePath;
            return finishDemo(1);
        }

        QUrl url(baseUrl() + "/post");
        qDebug() << "[2/3] 从文件流式上传:" << url.toString();

        QString bodyError;
        auto body = QCNetworkMultipartBody::fromSingleFileDevice(
            file,
            QStringLiteral("file"),
            QFileInfo(*file).fileName(),
            QStringLiteral("application/octet-stream"),
            file->size(),
            &bodyError);
        if (!body.has_value()) {
            qWarning() << "无法创建 multipart body:" << bodyError;
            file->deleteLater();
            return finishDemo(1);
        }

        QCNetworkRequest request(url);
        request.setRawHeader(QByteArrayLiteral("Content-Type"), body->contentType());
        QString takeError;
        auto *bodyDevice = body->takeDevice(nullptr, &takeError);
        if (!bodyDevice) {
            qWarning() << "无法取得 multipart 流式设备:" << takeError;
            file->deleteLater();
            return finishDemo(1);
        }
        auto *reply = m_manager.sendPost(request, bodyDevice, body->sizeBytes());
        bodyDevice->setParent(reply);

        connect(reply, &QCNetworkReply::uploadProgress, this, [](qint64 sent, qint64 total) {
            if (total > 0) {
                qDebug() << "  -> 上传进度" << sent << "/" << total;
            }
        });

        connect(reply, &QCNetworkReply::finished, this, [this, reply, file]() {
            if (reply->error() == NetworkError::NoError) {
                auto data = reply->readAll();
                if (data.has_value()) {
                    QJsonDocument doc = QJsonDocument::fromJson(data.value());
                    qDebug() << "  ✓ 上传成功，httpbin 回显大小"
                             << doc["files"].toObject()["file"].toString().toUtf8().size()
                             << "字节";
                }
                runResumableDownload();
            } else {
                qWarning() << "  ✗ 流式上传失败:" << reply->errorString();
                finishDemo(1);
            }
            reply->deleteLater();
            file->close();
            file->deleteLater();
        });
    }

    void runResumableDownload()
    {
        QFile::remove(m_resumableFilePath);
        m_cancelledOnce = false;
        qDebug() << "[3/3] 断点续传演示:" << m_resumableFilePath;
        startResumableAttempt(false);
    }

    void startResumableAttempt(bool resume)
    {
        const int totalBytes = 131072;
        QUrl url(baseUrl() + QStringLiteral("/range/%1").arg(totalBytes));
        auto *job = new QCNetworkResumableDownloadJob(&m_manager,
                                                      url,
                                                      m_resumableFilePath,
                                                      false,
                                                      this);

        if (!resume) {
            connect(job,
                    &QCNetworkTransferJob::progress,
                    this,
                    [this, job, totalBytes](qint64 received, qint64 total) {
                        Q_UNUSED(total);
                        if (!m_cancelledOnce && received >= totalBytes / 3) {
                            m_cancelledOnce = true;
                            qDebug() << "  -> 模拟网络中断，主动取消";
                            if (auto *reply = job->reply()) {
                                reply->cancel();
                            }
                        }
                    });
        }

        connect(job, &QCNetworkTransferJob::finished, this, [this, resume, totalBytes, job]() {
            auto *reply = job->reply();
            QFileInfo info(m_resumableFilePath);
            if (!reply) {
                qWarning() << "  ✗ 断点续传启动失败:" << job->errorString();
                job->deleteLater();
                finishDemo(1);
                return;
            }

            if (!resume && reply->error() != NetworkError::NoError) {
                qDebug() << "  -> 首次下载被取消，已写入" << info.size() << "字节";
                reply->deleteLater();
                job->deleteLater();
                startResumableAttempt(true);
                return;
            }

            if (reply->error() == NetworkError::NoError) {
                qDebug() << "  ✓ 断点续传完成，最终大小" << info.size() << "/" << totalBytes
                         << "字节";
                finishDemo();
            } else {
                qWarning() << "  ✗ 断点续传失败:" << reply->errorString();
                finishDemo(1);
            }
            reply->deleteLater();
            job->deleteLater();
        });

        job->start();
    }

    void finishDemo(int exitCode = 0)
    {
        qDebug() << "演示结束，退出码" << exitCode;
        QCoreApplication::exit(exitCode);
    }

private:
    QCNetworkAccessManager m_manager;
    QString m_streamFilePath;
    QString m_resumableFilePath;
    bool m_cancelledOnce = false;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    FileTransferDemo demo;
    QTimer::singleShot(0, [&demo]() { demo.start(); });

    return app.exec();
}
