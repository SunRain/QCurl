#include <QCoreApplication>
#include <QTextStream>
#include <QDebug>
#include <QTimer>
#include "BatchRequest.h"

// 工具函数：优先级转字符串
QString priorityToString(BatchRequest::Priority priority)
{
    switch (priority) {
    case BatchRequest::Priority::High:
        return "高";
    case BatchRequest::Priority::Normal:
        return "中";
    case BatchRequest::Priority::Low:
        return "低";
    default:
        return "未知";
    }
}

// 工具函数：请求方法转字符串
QString methodToString(BatchRequest::RequestMethod method)
{
    switch (method) {
    case BatchRequest::RequestMethod::GET:
        return "GET";
    case BatchRequest::RequestMethod::POST:
        return "POST";
    case BatchRequest::RequestMethod::HEAD:
        return "HEAD";
    default:
        return "未知";
    }
}

class BatchRequestDemo : public QObject
{
    Q_OBJECT

public:
    BatchRequestDemo(QObject *parent = nullptr)
        : QObject(parent)
        , m_batchRequest(new BatchRequest(this))
    {
        setupSignals();
        showMenu();
    }

private slots:
    void setupSignals()
    {
        // 批量操作信号
        connect(m_batchRequest, &BatchRequest::started, this, []() {
            qDebug() << "[批量请求开始]";
        });

        connect(m_batchRequest, &BatchRequest::paused, this, []() {
            qDebug() << "[批量请求已暂停]";
        });

        connect(m_batchRequest, &BatchRequest::resumed, this, []() {
            qDebug() << "[批量请求已恢复]";
        });

        connect(m_batchRequest, &BatchRequest::allCompleted, this, []() {
            qDebug() << "\n[✓ 全部请求完成]";
        });

        // 单个请求信号
        connect(m_batchRequest, &BatchRequest::requestStarted, this, [](const QString &id) {
            qDebug() << "[开始请求]" << id;
        });

        connect(m_batchRequest, &BatchRequest::requestCompleted, this, [](const QString &id, bool success) {
            if (success) {
                qDebug() << "[✓ 请求成功]" << id;
            } else {
                qDebug() << "[✗ 请求失败]" << id;
            }
        });

        connect(m_batchRequest, &BatchRequest::error, this, [](const QString &id, const QString &errorString) {
            qDebug() << "[请求错误]" << id << ":" << errorString;
        });

        // 进度信号
        connect(m_batchRequest, &BatchRequest::requestProgress, this,
                [](int completed, int total, double percentage) {
            static int lastPercentage = -1;
            int currentPercentage = static_cast<int>(percentage);

            // 每10%输出一次进度
            if (currentPercentage != lastPercentage && currentPercentage % 10 == 0) {
                qDebug() << QString("[进度] %1/%2 (%3%)")
                                .arg(completed)
                                .arg(total)
                                .arg(QString::number(currentPercentage, 'f', 0));
                lastPercentage = currentPercentage;
            }
        });
    }

    void showMenu()
    {
        QTextStream out(stdout);
        out << "\n========================================\n";
        out << "    BatchRequestDemo - 批量请求管理器\n";
        out << "========================================\n";
        out << "1. 添加请求\n";
        out << "2. 查看所有请求\n";
        out << "3. 开始执行\n";
        out << "4. 暂停执行\n";
        out << "5. 恢复执行\n";
        out << "6. 取消所有请求\n";
        out << "7. 清除所有请求\n";
        out << "8. 设置并发数（当前: " << m_batchRequest->maxConcurrent() << "）\n";
        out << "9. 设置重试次数（当前: " << m_batchRequest->maxRetries() << "）\n";
        out << "a. 快速测试（添加10个示例请求）\n";
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
            addRequest();
        } else if (input == "2") {
            showAllRequests();
        } else if (input == "3") {
            m_batchRequest->start();
            qDebug() << "[开始执行批量请求]";
            showMenu();
        } else if (input == "4") {
            m_batchRequest->pause();
            showMenu();
        } else if (input == "5") {
            m_batchRequest->resume();
            showMenu();
        } else if (input == "6") {
            m_batchRequest->cancel();
            qDebug() << "[已取消所有请求]";
            showMenu();
        } else if (input == "7") {
            m_batchRequest->clear();
            qDebug() << "[已清除所有请求]";
            showMenu();
        } else if (input == "8") {
            setMaxConcurrent();
        } else if (input == "9") {
            setMaxRetries();
        } else if (input == "a" || input == "A") {
            quickTest();
        } else if (input == "0") {
            qDebug() << "退出程序";
            QCoreApplication::quit();
        } else {
            qDebug() << "无效选项，请重新选择";
            showMenu();
        }
    }

    void addRequest()
    {
        QTextStream in(stdin);
        QTextStream out(stdout);

        out << "请输入 URL: ";
        out.flush();
        QString urlStr = in.readLine().trimmed();

        if (urlStr.isEmpty()) {
            qDebug() << "URL 不能为空";
            showMenu();
            return;
        }

        out << "请选择请求方法 (1=GET, 2=POST, 3=HEAD, 默认=GET): ";
        out.flush();
        QString methodStr = in.readLine().trimmed();

        BatchRequest::RequestMethod method = BatchRequest::RequestMethod::GET;
        if (methodStr == "2") {
            method = BatchRequest::RequestMethod::POST;
        } else if (methodStr == "3") {
            method = BatchRequest::RequestMethod::HEAD;
        }

        out << "请选择优先级 (1=高, 2=中, 3=低, 默认=中): ";
        out.flush();
        QString priorityStr = in.readLine().trimmed();

        BatchRequest::Priority priority = BatchRequest::Priority::Normal;
        if (priorityStr == "1") {
            priority = BatchRequest::Priority::High;
        } else if (priorityStr == "3") {
            priority = BatchRequest::Priority::Low;
        }

        QByteArray postData;
        if (method == BatchRequest::RequestMethod::POST) {
            out << "请输入 POST 数据（留空跳过）: ";
            out.flush();
            postData = in.readLine().trimmed().toUtf8();
        }

        QString id = m_batchRequest->addRequest(QUrl(urlStr), method, priority, postData);
        qDebug() << QString("[已添加请求] ID:%1 URL:%2 方法:%3 优先级:%4")
                        .arg(id)
                        .arg(urlStr)
                        .arg(methodToString(method))
                        .arg(priorityToString(priority));

        showMenu();
    }

    void showAllRequests()
    {
        QTextStream out(stdout);
        out << "\n========== 请求列表 ==========\n";
        out << QString("总计: %1 | 运行: %2 | 待处理: %3 | 完成: %4 | 成功: %5 | 失败: %6\n")
                .arg(m_batchRequest->totalCount())
                .arg(m_batchRequest->runningCount())
                .arg(m_batchRequest->pendingCount())
                .arg(m_batchRequest->completedCount())
                .arg(m_batchRequest->successCount())
                .arg(m_batchRequest->failedCount());
        out << "==============================\n";

        const auto &requests = m_batchRequest->allRequests();
        for (int i = 0; i < requests.size(); ++i) {
            const BatchRequest::RequestInfo &info = requests[i];
            out << QString("[%1] ID:%2\n").arg(i + 1).arg(info.id);
            out << QString("    URL: %1\n").arg(info.url.toString());
            out << QString("    方法: %1 | 优先级: %2 | 重试: %3/%4\n")
                    .arg(methodToString(info.method))
                    .arg(priorityToString(info.priority))
                    .arg(info.retryCount)
                    .arg(info.maxRetries);

            if (info.completed) {
                if (info.success) {
                    out << QString("    状态: ✓ 成功 | 响应大小: %1 字节\n")
                            .arg(info.responseData.size());
                } else {
                    out << QString("    状态: ✗ 失败 | 错误: %1\n")
                            .arg(info.errorString);
                }
            } else {
                out << "    状态: 待处理\n";
            }
        }
        out << "==============================\n";
        out.flush();

        showMenu();
    }

    void setMaxConcurrent()
    {
        QTextStream in(stdin);
        QTextStream out(stdout);

        out << "请输入最大并发数 (1-20): ";
        out.flush();

        bool ok;
        int max = in.readLine().trimmed().toInt(&ok);

        if (ok && max >= 1 && max <= 20) {
            m_batchRequest->setMaxConcurrent(max);
            qDebug() << "[已设置最大并发数为]" << max;
        } else {
            qDebug() << "无效输入，请输入 1-20 之间的数字";
        }

        showMenu();
    }

    void setMaxRetries()
    {
        QTextStream in(stdin);
        QTextStream out(stdout);

        out << "请输入最大重试次数 (0-10): ";
        out.flush();

        bool ok;
        int retries = in.readLine().trimmed().toInt(&ok);

        if (ok && retries >= 0 && retries <= 10) {
            m_batchRequest->setMaxRetries(retries);
            qDebug() << "[已设置最大重试次数为]" << retries;
        } else {
            qDebug() << "无效输入，请输入 0-10 之间的数字";
        }

        showMenu();
    }

    void quickTest()
    {
        qDebug() << "\n[快速测试] 添加10个示例请求...\n";

        // 高优先级请求
        m_batchRequest->addRequest(
            QUrl("https://httpbin.org/get"),
            BatchRequest::RequestMethod::GET,
            BatchRequest::Priority::High
        );

        m_batchRequest->addRequest(
            QUrl("https://httpbin.org/headers"),
            BatchRequest::RequestMethod::GET,
            BatchRequest::Priority::High
        );

        // 普通优先级请求
        for (int i = 0; i < 5; ++i) {
            m_batchRequest->addRequest(
                QUrl(QString("https://httpbin.org/delay/%1").arg(i)),
                BatchRequest::RequestMethod::GET,
                BatchRequest::Priority::Normal
            );
        }

        // 低优先级请求
        m_batchRequest->addRequest(
            QUrl("https://httpbin.org/status/200"),
            BatchRequest::RequestMethod::HEAD,
            BatchRequest::Priority::Low
        );

        m_batchRequest->addRequest(
            QUrl("https://httpbin.org/json"),
            BatchRequest::RequestMethod::GET,
            BatchRequest::Priority::Low
        );

        // POST 请求
        m_batchRequest->addRequest(
            QUrl("https://httpbin.org/post"),
            BatchRequest::RequestMethod::POST,
            BatchRequest::Priority::Normal,
            "{\"test\":\"data\"}"
        );

        qDebug() << "[提示] 10个测试请求已添加，请选择'3'开始执行\n";

        showMenu();
    }

private:
    BatchRequest *m_batchRequest;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "========================================";
    qDebug() << " QCurl BatchRequestDemo - v1.0";
    qDebug() << " 批量请求管理器示例";
    qDebug() << "========================================\n";

    BatchRequestDemo demo;

    return app.exec();
}

#include "main.moc"
