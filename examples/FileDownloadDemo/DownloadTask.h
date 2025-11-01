#ifndef DOWNLOADTASK_H
#define DOWNLOADTASK_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QFile>
#include <QElapsedTimer>
#include <QCNetworkAccessManager.h>
#include <QCNetworkRequest.h>
#include <QCNetworkReply.h>

/**
 * @brief 单个文件下载任务
 *
 * 功能特性:
 * - 断点续传支持 (使用 HTTP Range 请求)
 * - 实时进度跟踪
 * - 错误处理和自动重试
 * - 下载速度计算
 */
class DownloadTask : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,           ///< 空闲状态
        Downloading,    ///< 正在下载
        Paused,         ///< 已暂停
        Completed,      ///< 下载完成
        Failed,         ///< 下载失败
        Cancelled       ///< 已取消
    };

    explicit DownloadTask(const QUrl &url, const QString &savePath, QObject *parent = nullptr);
    ~DownloadTask();

    // Getters
    QUrl url() const { return m_url; }
    QString savePath() const { return m_savePath; }
    State state() const { return m_state; }
    qint64 bytesReceived() const { return m_bytesReceived; }
    qint64 bytesTotal() const { return m_bytesTotal; }
    double progress() const;
    QString errorString() const { return m_errorString; }
    double downloadSpeed() const { return m_downloadSpeed; }  // bytes per second

    // 操作方法
    void start();           ///< 开始下载
    void pause();           ///< 暂停下载
    void resume();          ///< 恢复下载
    void cancel();          ///< 取消下载

signals:
    void stateChanged(DownloadTask::State newState);
    void progressChanged(qint64 bytesReceived, qint64 bytesTotal, double percentage);
    void speedChanged(double bytesPerSecond);
    void finished();
    void error(const QString &errorString);

private slots:
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onFinished();
    void onError(QCurl::NetworkError errorCode);

private:
    void setState(State newState);
    void cleanup();
    qint64 getExistingFileSize();
    void startDownloadInternal(qint64 rangeStart = 0);
    void updateDownloadSpeed();

private:
    QUrl m_url;
    QString m_savePath;
    State m_state;

    QCurl::QCNetworkAccessManager *m_manager;
    QCurl::QCNetworkReply *m_reply;
    QFile *m_file;

    qint64 m_bytesReceived;
    qint64 m_bytesTotal;
    qint64 m_rangeStart;        ///< 断点续传起始位置

    QString m_errorString;

    // 速度计算
    qint64 m_lastBytesReceived;
    QElapsedTimer m_speedTimer;
    double m_downloadSpeed;
};

#endif // DOWNLOADTASK_H
