#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <QObject>
#include <QList>
#include <QQueue>
#include "DownloadTask.h"

/**
 * @brief 下载管理器 - 管理多个下载任务的队列
 *
 * 功能特性:
 * - 并发下载控制 (同时最多N个任务)
 * - 任务队列管理 (FIFO)
 * - 全局进度追踪
 * - 批量操作 (暂停全部/恢复全部/取消全部)
 */
class DownloadManager : public QObject
{
    Q_OBJECT

public:
    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager();

    // 配置
    void setMaxConcurrentDownloads(int max);
    int maxConcurrentDownloads() const { return m_maxConcurrentDownloads; }

    // 任务管理
    DownloadTask* addDownload(const QUrl &url, const QString &savePath);
    void removeTask(DownloadTask *task);
    void clearCompleted();
    void clearAll();

    // 批量操作
    void pauseAll();
    void resumeAll();
    void cancelAll();

    // 统计信息
    int taskCount() const { return m_allTasks.size(); }
    int runningCount() const { return m_runningTasks.size(); }
    int queuedCount() const { return m_queuedTasks.size(); }
    int completedCount() const;
    int failedCount() const;

    QList<DownloadTask*> allTasks() const { return m_allTasks; }

signals:
    void taskAdded(DownloadTask *task);
    void taskRemoved(DownloadTask *task);
    void taskStarted(DownloadTask *task);
    void taskCompleted(DownloadTask *task);
    void taskFailed(DownloadTask *task);
    void queueChanged();

private slots:
    void onTaskStateChanged(DownloadTask::State newState);
    void onTaskFinished();
    void onTaskError(const QString &errorString);

private:
    void processQueue();
    void startTask(DownloadTask *task);

private:
    int m_maxConcurrentDownloads;

    QList<DownloadTask*> m_allTasks;       ///< 所有任务
    QList<DownloadTask*> m_runningTasks;   ///< 正在下载的任务
    QQueue<DownloadTask*> m_queuedTasks;   ///< 等待队列
};

#endif // DOWNLOADMANAGER_H
