#include <QCoreApplication>
#include <QTextStream>
#include <QDebug>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include "ApiClient.h"

class ApiClientDemo : public QObject
{
    Q_OBJECT

public:
    ApiClientDemo(QObject *parent = nullptr)
        : QObject(parent)
        , m_client(new ApiClient("https://jsonplaceholder.typicode.com", this))
    {
        setupSignals();
        showMenu();
    }

private slots:
    void setupSignals()
    {
        connect(m_client, &ApiClient::requestStarted, this, [](const QString &endpoint) {
            qDebug() << "[请求开始]" << endpoint;
        });

        connect(m_client, &ApiClient::requestCompleted, this, [](const QString &endpoint, bool success) {
            if (success) {
                qDebug() << "[✓ 请求成功]" << endpoint;
            } else {
                qDebug() << "[✗ 请求失败]" << endpoint;
            }
        });
    }

    void showMenu()
    {
        QTextStream out(stdout);
        out << "\n========================================\n";
        out << "    ApiClientDemo - RESTful API 客户端\n";
        out << "========================================\n";
        out << "示例 API: https://jsonplaceholder.typicode.com\n";
        out << "----------------------------------------\n";
        out << "1. GET /posts - 获取所有文章\n";
        out << "2. GET /posts/1 - 获取单个文章\n";
        out << "3. POST /posts - 创建新文章\n";
        out << "4. PUT /posts/1 - 更新文章\n";
        out << "5. DELETE /posts/1 - 删除文章\n";
        out << "6. GET /users - 获取所有用户\n";
        out << "7. 自定义 GET 请求\n";
        out << "8. 自定义 POST 请求\n";
        out << "9. 设置 Bearer Token\n";
        out << "a. 设置基础 URL\n";
        out << "b. 设置超时时间（当前: " << m_client->timeout() << "秒）\n";
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
            getAllPosts();
        } else if (input == "2") {
            getPost();
        } else if (input == "3") {
            createPost();
        } else if (input == "4") {
            updatePost();
        } else if (input == "5") {
            deletePost();
        } else if (input == "6") {
            getAllUsers();
        } else if (input == "7") {
            customGet();
        } else if (input == "8") {
            customPost();
        } else if (input == "9") {
            setBearerToken();
        } else if (input == "a" || input == "A") {
            setBaseUrl();
        } else if (input == "b" || input == "B") {
            setTimeout();
        } else if (input == "0") {
            qDebug() << "退出程序";
            QCoreApplication::quit();
        } else {
            qDebug() << "无效选项，请重新选择";
            showMenu();
        }
    }

    void getAllPosts()
    {
        qDebug() << "\n[GET /posts] 获取所有文章...";

        m_client->get("posts",
            // Success callback
            [this](const QJsonDocument &response) {
                QJsonArray posts = response.array();
                qDebug() << "\n收到" << posts.size() << "篇文章:";

                // 显示前5篇
                int count = qMin(5, posts.size());
                for (int i = 0; i < count; ++i) {
                    QJsonObject post = posts[i].toObject();
                    qDebug() << QString("  [%1] %2")
                                    .arg(post["id"].toInt())
                                    .arg(post["title"].toString());
                }

                if (posts.size() > 5) {
                    qDebug() << "  ... 及其他" << (posts.size() - 5) << "篇";
                }

                showMenu();
            },
            // Error callback
            [this](int code, const QString &error) {
                qDebug() << "错误:" << code << error;
                showMenu();
            }
        );
    }

    void getPost()
    {
        qDebug() << "\n[GET /posts/1] 获取单个文章...";

        m_client->get("posts/1",
            [this](const QJsonDocument &response) {
                QJsonObject post = response.object();
                qDebug() << "\n文章详情:";
                qDebug() << "  ID:" << post["id"].toInt();
                qDebug() << "  用户ID:" << post["userId"].toInt();
                qDebug() << "  标题:" << post["title"].toString();
                qDebug() << "  内容:" << post["body"].toString();

                showMenu();
            },
            [this](int code, const QString &error) {
                qDebug() << "错误:" << code << error;
                showMenu();
            }
        );
    }

    void createPost()
    {
        qDebug() << "\n[POST /posts] 创建新文章...";

        QJsonObject data;
        data["title"] = "QCurl Test Post";
        data["body"] = "This is a test post created by QCurl ApiClient.";
        data["userId"] = 1;

        m_client->post("posts", data,
            [this](const QJsonDocument &response) {
                QJsonObject post = response.object();
                qDebug() << "\n✓ 文章创建成功:";
                qDebug() << "  ID:" << post["id"].toInt();
                qDebug() << "  标题:" << post["title"].toString();
                qDebug() << "  内容:" << post["body"].toString();

                showMenu();
            },
            [this](int code, const QString &error) {
                qDebug() << "错误:" << code << error;
                showMenu();
            }
        );
    }

    void updatePost()
    {
        qDebug() << "\n[PUT /posts/1] 更新文章...";

        QJsonObject data;
        data["id"] = 1;
        data["title"] = "Updated Title";
        data["body"] = "This post has been updated by QCurl ApiClient.";
        data["userId"] = 1;

        m_client->put("posts/1", data,
            [this](const QJsonDocument &response) {
                QJsonObject post = response.object();
                qDebug() << "\n✓ 文章更新成功:";
                qDebug() << "  ID:" << post["id"].toInt();
                qDebug() << "  标题:" << post["title"].toString();
                qDebug() << "  内容:" << post["body"].toString();

                showMenu();
            },
            [this](int code, const QString &error) {
                qDebug() << "错误:" << code << error;
                showMenu();
            }
        );
    }

    void deletePost()
    {
        qDebug() << "\n[DELETE /posts/1] 删除文章...";

        m_client->del("posts/1",
            [this](const QJsonDocument &response) {
                Q_UNUSED(response);
                qDebug() << "\n✓ 文章删除成功";

                showMenu();
            },
            [this](int code, const QString &error) {
                qDebug() << "错误:" << code << error;
                showMenu();
            }
        );
    }

    void getAllUsers()
    {
        qDebug() << "\n[GET /users] 获取所有用户...";

        m_client->get("users",
            [this](const QJsonDocument &response) {
                QJsonArray users = response.array();
                qDebug() << "\n收到" << users.size() << "个用户:";

                for (int i = 0; i < users.size(); ++i) {
                    QJsonObject user = users[i].toObject();
                    qDebug() << QString("  [%1] %2 (%3)")
                                    .arg(user["id"].toInt())
                                    .arg(user["name"].toString())
                                    .arg(user["email"].toString());
                }

                showMenu();
            },
            [this](int code, const QString &error) {
                qDebug() << "错误:" << code << error;
                showMenu();
            }
        );
    }

    void customGet()
    {
        QTextStream in(stdin);
        QTextStream out(stdout);

        out << "请输入端点路径（如 posts/1）: ";
        out.flush();
        QString endpoint = in.readLine().trimmed();

        if (endpoint.isEmpty()) {
            qDebug() << "端点不能为空";
            showMenu();
            return;
        }

        qDebug() << "\n[GET /" + endpoint + "]";

        m_client->get(endpoint,
            [this](const QJsonDocument &response) {
                qDebug() << "\n响应:";
                qDebug() << response.toJson(QJsonDocument::Indented);

                showMenu();
            },
            [this](int code, const QString &error) {
                qDebug() << "错误:" << code << error;
                showMenu();
            }
        );
    }

    void customPost()
    {
        QTextStream in(stdin);
        QTextStream out(stdout);

        out << "请输入端点路径（如 posts）: ";
        out.flush();
        QString endpoint = in.readLine().trimmed();

        if (endpoint.isEmpty()) {
            qDebug() << "端点不能为空";
            showMenu();
            return;
        }

        out << "请输入 JSON 数据（如 {\"title\":\"test\"}）: ";
        out.flush();
        QString jsonStr = in.readLine().trimmed();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseError);

        if (parseError.error != QJsonParseError::NoError) {
            qDebug() << "JSON 格式错误:" << parseError.errorString();
            showMenu();
            return;
        }

        qDebug() << "\n[POST /" + endpoint + "]";

        m_client->post(endpoint, doc.object(),
            [this](const QJsonDocument &response) {
                qDebug() << "\n响应:";
                qDebug() << response.toJson(QJsonDocument::Indented);

                showMenu();
            },
            [this](int code, const QString &error) {
                qDebug() << "错误:" << code << error;
                showMenu();
            }
        );
    }

    void setBearerToken()
    {
        QTextStream in(stdin);
        QTextStream out(stdout);

        out << "请输入 Bearer Token（留空清除）: ";
        out.flush();
        QString token = in.readLine().trimmed();

        if (token.isEmpty()) {
            m_client->clearBearerToken();
            qDebug() << "[已清除 Bearer Token]";
        } else {
            m_client->setBearerToken(token);
            qDebug() << "[已设置 Bearer Token]" << token.left(20) + "...";
        }

        showMenu();
    }

    void setBaseUrl()
    {
        QTextStream in(stdin);
        QTextStream out(stdout);

        out << "请输入基础 URL: ";
        out.flush();
        QString url = in.readLine().trimmed();

        if (url.isEmpty()) {
            qDebug() << "URL 不能为空";
        } else {
            m_client->setBaseUrl(url);
            qDebug() << "[已设置基础 URL]" << url;
        }

        showMenu();
    }

    void setTimeout()
    {
        QTextStream in(stdin);
        QTextStream out(stdout);

        out << "请输入超时时间（秒，1-300）: ";
        out.flush();

        bool ok;
        int seconds = in.readLine().trimmed().toInt(&ok);

        if (ok && seconds >= 1 && seconds <= 300) {
            m_client->setTimeout(seconds);
            qDebug() << "[已设置超时时间为]" << seconds << "秒";
        } else {
            qDebug() << "无效输入，请输入 1-300 之间的数字";
        }

        showMenu();
    }

private:
    ApiClient *m_client;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "========================================";
    qDebug() << " QCurl ApiClientDemo - v1.0";
    qDebug() << " RESTful API 客户端示例";
    qDebug() << "========================================\n";

    ApiClientDemo demo;

    return app.exec();
}

#include "main.moc"
