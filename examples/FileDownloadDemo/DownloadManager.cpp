#include "DownloadManager.h"
#include <QDebug>

DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent)
    , m_maxConcurrentDownloads(3)  // 默认最多3个并发下载
{
}

DownloadManager::~DownloadManager()
{
    clearAll();
}

void DownloadManager::setMaxConcurrentDownloads(int max)
{
    if (max < 1) {
        qWarning() << "Max concurrent downloads must be at least 1, setting to 1";
        max = 1;
    }
    m_maxConcurrentDownloads = max;
    processQueue();
}

DownloadTask* DownloadManager::addDownload(const QUrl &url, const QString &savePath)
{
    auto *task = new DownloadTask(url, savePath, this);

    // 连接信号
    connect(task, &DownloadTask::stateChanged,
            this, &DownloadManager::onTaskStateChanged);
    connect(task, &DownloadTask::finished,
            this, &DownloadManager::onTaskFinished);
    connect(task, &DownloadTask::error,
            this, &DownloadManager::onTaskError);

    m_allTasks.append(task);
    emit taskAdded(task);

    // 如果有空闲槽位,立即开始下载;否则加入队列
    if (m_runningTasks.size() < m_maxConcurrentDownloads) {
        startTask(task);
    } else {
        m_queuedTasks.enqueue(task);
        emit queueChanged();
    }

    return task;
}

void DownloadManager::removeTask(DownloadTask *task)
{
    if (!task) {
        return;
    }

    // 从所有列表中移除
    m_allTasks.removeOne(task);
    m_runningTasks.removeOne(task);
    m_queuedTasks.removeOne(task);

    emit taskRemoved(task);

    // 删除任务
    task->cancel();
    task->deleteLater();

    // 处理队列
    processQueue();
}

void DownloadManager::clearCompleted()
{
    QList<DownloadTask*> toRemove;

    for (DownloadTask *task : m_allTasks) {
        if (task->state() == DownloadTask::State::Completed ||
            task->state() == DownloadTask::State::Cancelled) {
            toRemove.append(task);
        }
    }

    for (DownloadTask *task : toRemove) {
        removeTask(task);
    }
}

void DownloadManager::clearAll()
{
    // 取消所有任务
    for (DownloadTask *task : m_allTasks) {
        task->cancel();
        task->deleteLater();
    }

    m_allTasks.clear();
    m_runningTasks.clear();
    m_queuedTasks.clear();

    emit queueChanged();
}

void DownloadManager::pauseAll()
{
    for (DownloadTask *task : m_runningTasks) {
        task->pause();
    }
}

void DownloadManager::resumeAll()
{
    for (DownloadTask *task : m_allTasks) {
        if (task->state() == DownloadTask::State::Paused) {
            // 如果有空闲槽位,直接恢复;否则加入队列
            if (m_runningTasks.size() < m_maxConcurrentDownloads) {
                task->resume();
            } else if (!m_queuedTasks.contains(task)) {
                m_queuedTasks.enqueue(task);
            }
        }
    }
    emit queueChanged();
}

void DownloadManager::cancelAll()
{
    for (DownloadTask *task : m_allTasks) {
        task->cancel();
    }

    m_runningTasks.clear();
    m_queuedTasks.clear();
    emit queueChanged();
}

int DownloadManager::completedCount() const
{
    int count = 0;
    for (const DownloadTask *task : m_allTasks) {
        if (task->state() == DownloadTask::State::Completed) {
            ++count;
        }
    }
    return count;
}

int DownloadManager::failedCount() const
{
    int count = 0;
    for (const DownloadTask *task : m_allTasks) {
        if (task->state() == DownloadTask::State::Failed) {
            ++count;
        }
    }
    return count;
}

void DownloadManager::onTaskStateChanged(DownloadTask::State newState)
{
    auto *task = qobject_cast<DownloadTask*>(sender());
    if (!task) {
        return;
    }

    if (newState == DownloadTask::State::Downloading) {
        // 任务开始下载
        if (!m_runningTasks.contains(task)) {
            m_runningTasks.append(task);
            m_queuedTasks.removeOne(task);
            emit taskStarted(task);
            emit queueChanged();
        }
    } else if (newState == DownloadTask::State::Completed ||
               newState == DownloadTask::State::Failed ||
               newState == DownloadTask::State::Cancelled) {
        // 任务结束
        m_runningTasks.removeOne(task);
        m_queuedTasks.removeOne(task);
        emit queueChanged();

        // 处理队列中的下一个任务
        processQueue();
    }
}

void DownloadManager::onTaskFinished()
{
    auto *task = qobject_cast<DownloadTask*>(sender());
    if (task) {
        emit taskCompleted(task);
    }
}

void DownloadManager::onTaskError(const QString &errorString)
{
    auto *task = qobject_cast<DownloadTask*>(sender());
    if (task) {
        qWarning() << "Task failed:" << task->url() << "-" << errorString;
        emit taskFailed(task);
    }
}

void DownloadManager::processQueue()
{
    // 从队列中启动任务,直到达到并发限制
    while (m_runningTasks.size() < m_maxConcurrentDownloads && !m_queuedTasks.isEmpty()) {
        DownloadTask *task = m_queuedTasks.dequeue();
        startTask(task);
    }
}

void DownloadManager::startTask(DownloadTask *task)
{
    if (!task) {
        return;
    }

    task->start();
}
