#include <QCoreApplication>
#include <QTextStream>
#include <QDebug>
#include <QTimer>
#include "DownloadManager.h"

// 工具函数：格式化文件大小
QString formatSize(qint64 bytes)
{
    const qint64 KB = 1024;
    const qint64 MB = KB * 1024;
    const qint64 GB = MB * 1024;

    if (bytes >= GB) {
        return QString::number(bytes / static_cast<double>(GB), 'f', 2) + " GB";
    } else if (bytes >= MB) {
        return QString::number(bytes / static_cast<double>(MB), 'f', 2) + " MB";
    } else if (bytes >= KB) {
        return QString::number(bytes / static_cast<double>(KB), 'f', 2) + " KB";
    } else {
        return QString::number(bytes) + " B";
    }
}

// 工具函数：格式化速度
QString formatSpeed(double bytesPerSecond)
{
    return formatSize(static_cast<qint64>(bytesPerSecond)) + "/s";
}

// 工具函数：状态转字符串
QString stateToString(DownloadTask::State state)
{
    switch (state) {
    case DownloadTask::State::Idle:
        return "空闲";
    case DownloadTask::State::Downloading:
        return "下载中";
    case DownloadTask::State::Paused:
        return "已暂停";
    case DownloadTask::State::Completed:
        return "已完成";
    case DownloadTask::State::Failed:
        return "失败";
    case DownloadTask::State::Cancelled:
        return "已取消";
    default:
        return "未知";
    }
}

class FileDownloadDemo : public QObject
{
    Q_OBJECT

public:
    FileDownloadDemo(QObject *parent = nullptr)
        : QObject(parent)
        , m_manager(new DownloadManager(this))
        , m_taskCounter(0)
    {
        setupSignals();
        showMenu();
    }

private slots:
    void setupSignals()
    {
        // 任务添加信号
        connect(m_manager, &DownloadManager::taskAdded, this, [](DownloadTask *task) {
            qDebug() << "[任务添加]" << task->url().toString();
        });

        // 任务开始信号
        connect(m_manager, &DownloadManager::taskStarted, this, [](DownloadTask *task) {
            qDebug() << "[开始下载]" << task->url().toString();
        });

        // 任务完成信号
        connect(m_manager, &DownloadManager::taskCompleted, this, [](DownloadTask *task) {
            qDebug() << "\n[✓ 下载完成]" << task->url().toString()
                     << "\n  保存至:" << task->savePath()
                     << "\n  大小:" << formatSize(task->bytesReceived());
        });

        // 任务失败信号
        connect(m_manager, &DownloadManager::taskFailed, this, [](DownloadTask *task) {
            qDebug() << "\n[✗ 下载失败]" << task->url().toString()
                     << "\n  错误:" << task->errorString();
        });
    }

    void showMenu()
    {
        QTextStream out(stdout);
        out << "\n========================================\n";
        out << "    FileDownloadDemo - 文件下载管理器\n";
        out << "========================================\n";
        out << "1. 添加下载任务\n";
        out << "2. 查看所有任务\n";
        out << "3. 暂停所有任务\n";
        out << "4. 恢复所有任务\n";
        out << "5. 取消所有任务\n";
        out << "6. 清理已完成任务\n";
        out << "7. 设置并发数（当前: " << m_manager->maxConcurrentDownloads() << "）\n";
        out << "8. 快速测试（下载3个示例文件）\n";
        out << "0. 退出\n";
        out << "========================================\n";
        out << "请选择操作: ";
        out.flush();

        processInput();
    }

    void processInput()
    {
        QTextStream in(stdin);
        QString input = in.readLine().trimmed();

        if (input == "1") {
            addDownloadTask();
        } else if (input == "2") {
            showAllTasks();
        } else if (input == "3") {
            m_manager->pauseAll();
            qDebug() << "[暂停所有任务]";
            showMenu();
        } else if (input == "4") {
            m_manager->resumeAll();
            qDebug() << "[恢复所有任务]";
            showMenu();
        } else if (input == "5") {
            m_manager->cancelAll();
            qDebug() << "[取消所有任务]";
            showMenu();
        } else if (input == "6") {
            m_manager->clearCompleted();
            qDebug() << "[已清理完成任务]";
            showMenu();
        } else if (input == "7") {
            setMaxConcurrent();
        } else if (input == "8") {
            quickTest();
        } else if (input == "0") {
            qDebug() << "退出程序";
            QCoreApplication::quit();
        } else {
            qDebug() << "无效选项，请重新选择";
            showMenu();
        }
    }

    void addDownloadTask()
    {
        QTextStream in(stdin);
        QTextStream out(stdout);

        out << "请输入下载 URL: ";
        out.flush();
        QString url = in.readLine().trimmed();

        if (url.isEmpty()) {
            qDebug() << "URL 不能为空";
            showMenu();
            return;
        }

        out << "请输入保存路径（留空使用默认）: ";
        out.flush();
        QString savePath = in.readLine().trimmed();

        if (savePath.isEmpty()) {
            QUrl qurl(url);
            QString fileName = qurl.fileName();
            if (fileName.isEmpty()) {
                fileName = QString("download_%1.bin").arg(++m_taskCounter);
            }
            savePath = "./downloads/" + fileName;
        }

        DownloadTask *task = m_manager->addDownload(QUrl(url), savePath);

        // 连接进度信号
        connect(task, &DownloadTask::progressChanged, this,
                [task](qint64 received, qint64 total, double percentage) {
            Q_UNUSED(received);
            Q_UNUSED(total);
            static int lastPercentage = -1;
            int currentPercentage = static_cast<int>(percentage);

            // 每5%输出一次进度
            if (currentPercentage != lastPercentage && currentPercentage % 5 == 0) {
                qDebug() << "[进度]" << task->url().fileName()
                         << QString::number(currentPercentage, 'f', 1) + "%"
                         << formatSpeed(task->downloadSpeed());
                lastPercentage = currentPercentage;
            }
        });

        qDebug() << "[已添加任务]" << url << "->" << savePath;
        showMenu();
    }

    void showAllTasks()
    {
        QTextStream out(stdout);
        out << "\n========== 任务列表 ==========\n";
        out << QString("总计: %1 | 运行: %2 | 队列: %3 | 完成: %4 | 失败: %5\n")
                .arg(m_manager->taskCount())
                .arg(m_manager->runningCount())
                .arg(m_manager->queuedCount())
                .arg(m_manager->completedCount())
                .arg(m_manager->failedCount());
        out << "==============================\n";

        const auto &tasks = m_manager->allTasks();
        for (int i = 0; i < tasks.size(); ++i) {
            DownloadTask *task = tasks[i];
            out << QString("[%1] %2\n").arg(i + 1).arg(task->url().toString());
            out << QString("    状态: %1 | 进度: %2% | 大小: %3/%4\n")
                    .arg(stateToString(task->state()))
                    .arg(QString::number(task->progress(), 'f', 1))
                    .arg(formatSize(task->bytesReceived()))
                    .arg(formatSize(task->bytesTotal()));

            if (task->state() == DownloadTask::State::Downloading) {
                out << QString("    速度: %1\n").arg(formatSpeed(task->downloadSpeed()));
            } else if (task->state() == DownloadTask::State::Failed) {
                out << QString("    错误: %1\n").arg(task->errorString());
            }
            out << "    保存: " << task->savePath() << "\n";
        }
        out << "==============================\n";
        out.flush();

        showMenu();
    }

    void setMaxConcurrent()
    {
        QTextStream in(stdin);
        QTextStream out(stdout);

        out << "请输入最大并发数 (1-10): ";
        out.flush();

        bool ok;
        int max = in.readLine().trimmed().toInt(&ok);

        if (ok && max >= 1 && max <= 10) {
            m_manager->setMaxConcurrentDownloads(max);
            qDebug() << "[已设置最大并发数为]" << max;
        } else {
            qDebug() << "无效输入，请输入 1-10 之间的数字";
        }

        showMenu();
    }

    void quickTest()
    {
        qDebug() << "\n[快速测试] 添加3个示例下载任务...\n";

        // 小文件测试
        m_manager->addDownload(
            QUrl("https://httpbin.org/bytes/1024"),
            "./downloads/test_1KB.bin"
        );

        // 中等文件测试
        m_manager->addDownload(
            QUrl("https://httpbin.org/bytes/102400"),
            "./downloads/test_100KB.bin"
        );

        // JSON 测试
        m_manager->addDownload(
            QUrl("https://httpbin.org/json"),
            "./downloads/test.json"
        );

        qDebug() << "[提示] 3个测试任务已添加，请在菜单选择'2'查看进度\n";

        showMenu();
    }

private:
    DownloadManager *m_manager;
    int m_taskCounter;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "========================================";
    qDebug() << " QCurl FileDownloadDemo - v1.0";
    qDebug() << " 文件下载管理器示例";
    qDebug() << "========================================\n";

    FileDownloadDemo demo;

    return app.exec();
}

#include "main.moc"
