#include "MainWindow.h"

#include "QCBlockingNetworkClient.h"
#include "QCNetworkAccessManager.h"
#include "QCNetworkError.h"
#include "QCNetworkReply.h"
#include "QCNetworkRequest.h"
#include "ui_MainWindow.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    mgr = new QCurl::QCNetworkAccessManager(this);
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_pushButton_clicked()
{
    tst_async();
    tst_blockingExtras();
}

void MainWindow::tst_async()
{
    QString cookie = QString("%1/cookie.txt").arg(QApplication::applicationDirPath());
    qDebug() << Q_FUNC_INFO << "cookie file " << cookie;
    QStringList list;

    //    list.append("https://pan.baidu.com");
    list.append("https://timgsa.baidu.com/"
                "timg?image&quality=80&size=b9999_10000&sec=1551629798534&di="
                "600f3fe2a3435642077ae99849a10345&imgtype=0&src=http%3A%2F%2F00.minipic.eastday."
                "com%2F20170523%2F20170523000003_d41d8cd98f00b204e9800998ecf8427e_16.jpeg");

    //    list.append("http://bos.pgzs.com/sjapp91/pcsuite/plugin/91assistant_pc_v6_1_20180416.exe");
    foreach (const QString &url, list) {
        qDebug() << Q_FUNC_INFO << "for url " << url;
        QUrl u(url);
        QCurl::QCNetworkRequest request(u);
        qDebug() << Q_FUNC_INFO << "qcnetworkmgr " << mgr;
        mgr->setCookieFilePath(cookie);

        // 使用 sendHead() 获取响应头
        QCurl::QCNetworkReply *reply = mgr->sendHead(request);
        // 或者使用 sendGet() 获取完整响应：
        // QCurl::QCNetworkReply *reply = mgr->sendGet(request);
        auto *f = new QFile(QStringLiteral("test.jpg"), reply);
        if (!f->open(QIODevice::WriteOnly)) {
            qWarning() << Q_FUNC_INFO << "open file failed" << f->errorString();
            reply->deleteLater();
            continue;
        }

        qDebug() << Q_FUNC_INFO << reply->url();

        connect(reply, &QCurl::QCNetworkReply::finished, [&, reply, f]() {
            qDebug() << Q_FUNC_INFO << "--------- finished ";
            auto data = reply->readAll();
            if (data) {
                qDebug() << Q_FUNC_INFO << " data size " << data->size();
                f->write(*data);
            }
            f->close();
            reply->deleteLater();
        });
        connect(reply, &QCurl::QCNetworkReply::readyRead, [&, reply, f]() {
            qDebug() << Q_FUNC_INFO << "--------- readyRead ";
            auto data = reply->readAll();
            if (data) {
                qDebug() << Q_FUNC_INFO << " data size " << data->size();
                f->write(*data);
            }
            //            f->close();
        });

        // Reply 已自动启动，无需手动调用 perform()
        // reply->perform();  // 已移除

        qDebug() << Q_FUNC_INFO << "///////////////////////////////////////////////////////////";
    }
}

void MainWindow::tst_blockingExtras()
{
    QStringList list;

    list.append("https://passport.baidu.com/v2/api/");
    foreach (const QString &url, list) {
        qDebug() << Q_FUNC_INFO << "for url " << url;
        QUrl u(url);
        {
            QUrlQuery query;
            query.addQueryItem("getapi", QString());
            query.addQueryItem("tpl", "netdisk");
            query.addQueryItem("subpro", "netdisk_web");
            query.addQueryItem("apiver", "v3");
            query.addQueryItem("tt", QString::number(QDateTime::currentMSecsSinceEpoch()));
            query.addQueryItem("class", "login");
            query.addQueryItem("gid", QUuid::createUuid().toString());
            query.addQueryItem("loginversion", "v4");
            query.addQueryItem("logintype", "basicLogin");
            query.addQueryItem("traceid", QString());
            query.addQueryItem("callback", QString("bd__cbs__bdpand"));
            u.setQuery(query);
        }
        QCurl::QCNetworkRequest request(u);
        request.setRawHeader(
            "User-Agent",
            "Mozilla/5.0 (Windows;U;Windows NT 5.1;zh-CN;rv:1.9.2.9) Gecko/20100101 Firefox/43.0");
        request.setRawHeader("Accept",
                             "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");

        QCurl::QCBlockingNetworkClient::Options options;
        options.setApplicationThreadPolicy(
            QCurl::QCBlockingNetworkClient::ApplicationThreadPolicy::AllowForCliOrTests);
        QCurl::QCBlockingNetworkClient client(options);

        // Blocking Extras 示例：同步阻塞入口已从 Core 移出，返回 value result。
        // 注意：GUI 应用生产代码应放到 worker thread 中执行，避免冻结 UI。
        const QCurl::QCBlockingNetworkResult result = client.get(request);

        qDebug() << Q_FUNC_INFO << "///////////////////////////////////////////////////////////";

        if (result.error() == QCurl::NetworkError::NoError) {
            qDebug() << Q_FUNC_INFO << "---------- no error " << static_cast<int>(result.error())
                     << "status" << result.statusCode();
        } else {
            qDebug() << Q_FUNC_INFO << "---------- error " << static_cast<int>(result.error())
                     << result.errorMessage();
        }
        const QByteArray body = result.body();
        if (!body.isEmpty()) {
            qDebug() << Q_FUNC_INFO << "body size" << body.size() << "data" << body;
        }
    }
}
