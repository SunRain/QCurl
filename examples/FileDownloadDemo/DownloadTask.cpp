#include "DownloadTask.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QElapsedTimer>

DownloadTask::DownloadTask(const QUrl &url, const QString &savePath, QObject *parent)
    : QObject(parent)
    , m_url(url)
    , m_savePath(savePath)
    , m_state(State::Idle)
    , m_manager(new QCurl::QCNetworkAccessManager(this))
    , m_reply(nullptr)
    , m_file(nullptr)
    , m_bytesReceived(0)
    , m_bytesTotal(0)
    , m_rangeStart(0)
    , m_lastBytesReceived(0)
    , m_downloadSpeed(0.0)
{
    m_speedTimer.start();
}

DownloadTask::~DownloadTask()
{
    cleanup();
}

double DownloadTask::progress() const
{
    if (m_bytesTotal <= 0) {
        return 0.0;
    }
    return (static_cast<double>(m_bytesReceived) / m_bytesTotal) * 100.0;
}

void DownloadTask::start()
{
    if (m_state == State::Downloading) {
        qWarning() << "Task already downloading:" << m_url;
        return;
    }

    // 检查已存在的文件大小（用于断点续传）
    qint64 existingSize = getExistingFileSize();
    if (existingSize > 0) {
        qDebug() << "Resuming download from" << existingSize << "bytes";
        m_rangeStart = existingSize;
        m_bytesReceived = existingSize;
    } else {
        m_rangeStart = 0;
        m_bytesReceived = 0;
    }

    startDownloadInternal(m_rangeStart);
}

void DownloadTask::pause()
{
    if (m_state != State::Downloading) {
        return;
    }

    if (m_reply) {
        m_reply->cancel();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }

    setState(State::Paused);
}

void DownloadTask::resume()
{
    if (m_state != State::Paused) {
        return;
    }

    start();  // 断点续传
}

void DownloadTask::cancel()
{
    if (m_reply) {
        m_reply->cancel();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    cleanup();
    setState(State::Cancelled);
}

void DownloadTask::setState(State newState)
{
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(m_state);
    }
}

void DownloadTask::cleanup()
{
    if (m_file) {
        if (m_file->isOpen()) {
            m_file->close();
        }
        delete m_file;
        m_file = nullptr;
    }
}

qint64 DownloadTask::getExistingFileSize()
{
    QFileInfo fileInfo(m_savePath);
    if (fileInfo.exists() && fileInfo.isFile()) {
        return fileInfo.size();
    }
    return 0;
}

void DownloadTask::startDownloadInternal(qint64 rangeStart)
{
    // 确保目标目录存在
    QFileInfo fileInfo(m_savePath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // 创建请求
    QCurl::QCNetworkRequest request(m_url);

    // 设置超时
    request.setTimeout(std::chrono::seconds(30));

    // 如果是断点续传，设置 Range 头
    if (rangeStart > 0) {
        QString rangeHeader = QStringLiteral("bytes=%1-").arg(rangeStart);
        request.setRawHeader("Range", rangeHeader.toUtf8());
    }

    // 发起请求
    m_reply = m_manager->sendGet(request);

    // 连接信号
    connect(m_reply, &QCurl::QCNetworkReply::downloadProgress,
            this, &DownloadTask::onDownloadProgress);
    connect(m_reply, &QCurl::QCNetworkReply::finished,
            this, &DownloadTask::onFinished);
    connect(m_reply, qOverload<QCurl::NetworkError>(&QCurl::QCNetworkReply::error),
            this, &DownloadTask::onError);

    // 打开文件（追加模式用于断点续传）
    m_file = new QFile(m_savePath);
    QIODevice::OpenMode mode = QIODevice::WriteOnly;
    if (rangeStart > 0) {
        mode |= QIODevice::Append;
    } else {
        mode |= QIODevice::Truncate;  // 全新下载，清空文件
    }

    if (!m_file->open(mode)) {
        m_errorString = QStringLiteral("无法打开文件: %1").arg(m_file->errorString());
        emit error(m_errorString);
        setState(State::Failed);
        delete m_file;
        m_file = nullptr;
        return;
    }

    // 执行请求
    m_reply->execute();

    setState(State::Downloading);
    m_speedTimer.restart();
    m_lastBytesReceived = m_bytesReceived;
}

void DownloadTask::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    // 处理断点续传的情况
    if (m_rangeStart > 0) {
        m_bytesReceived = m_rangeStart + bytesReceived;
        m_bytesTotal = m_rangeStart + bytesTotal;
    } else {
        m_bytesReceived = bytesReceived;
        m_bytesTotal = bytesTotal;
    }

    // 写入数据到文件
    if (m_file && m_file->isOpen() && m_reply) {
        auto dataOpt = m_reply->readAll();
        if (dataOpt.has_value()) {
            QByteArray data = dataOpt.value();
            if (!data.isEmpty()) {
                m_file->write(data);
                m_file->flush();
            }
        }
    }

    // 更新下载速度
    updateDownloadSpeed();

    emit progressChanged(m_bytesReceived, m_bytesTotal, progress());
}

void DownloadTask::onFinished()
{
    cleanup();

    if (m_state == State::Downloading) {
        setState(State::Completed);
        emit finished();
    }

    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void DownloadTask::onError(QCurl::NetworkError errorCode)
{
    m_errorString = QStringLiteral("下载错误: %1 (%2)")
                        .arg(m_reply->errorString())
                        .arg(static_cast<int>(errorCode));

    cleanup();
    setState(State::Failed);

    emit error(m_errorString);

    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void DownloadTask::updateDownloadSpeed()
{
    qint64 elapsed = m_speedTimer.elapsed();
    if (elapsed >= 1000) {  // 每秒更新一次速度
        qint64 bytesDownloaded = m_bytesReceived - m_lastBytesReceived;
        double seconds = elapsed / 1000.0;
        m_downloadSpeed = bytesDownloaded / seconds;

        emit speedChanged(m_downloadSpeed);

        m_lastBytesReceived = m_bytesReceived;
        m_speedTimer.restart();
    }
}
