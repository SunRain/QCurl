#include "MainWindow.h"
#include "ui_MainWindow.h"

#include <QDebug>
#include <QTimer>

#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkReply.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    mgr = new QCurl::QCNetworkAccessManager();
    ui->setupUi(this);

}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_pushButton_clicked()
{
//    QCurl::QCNetworkAccessManager mgr;
//    QCurl::QCNetworkRequest request(QUrl("http://pro.25pp.com/download/25pp_wdjgw/8009"));
//    QCurl::QCNetworkReply *reply =  mgr->head(request);//mgr.head(request);
//    reply->request();
//    qDebug()<<Q_FUNC_INFO<<reply->url();
//    reply->perform();
    QFile *f = new QFile("test.jpg");
    f->open(QIODevice::WriteOnly);
    QStringList list;
    list.append("https://timgsa.baidu.com/timg?image&quality=80&size=b9999_10000&sec=1551629798534&di=600f3fe2a3435642077ae99849a10345&imgtype=0&src=http%3A%2F%2F00.minipic.eastday.com%2F20170523%2F20170523000003_d41d8cd98f00b204e9800998ecf8427e_16.jpeg");
//    list.append("http://bos.pgzs.com/sjapp91/pcsuite/plugin/91assistant_pc_v6_1_20180416.exe");
    foreach (const QString &url, list) {
        qDebug()<<Q_FUNC_INFO<<"for url "<<url;
        QUrl u(url);
        QCurl::QCNetworkRequest request(u);
        qDebug()<<Q_FUNC_INFO<<"qcnetworkmgr "<<mgr;
        QCurl::QCNetworkReply *reply = mgr->get(request); //mgr->head(request);
        reply->request();
        qDebug()<<Q_FUNC_INFO<<reply->url();

        connect(reply, &QCurl::QCNetworkReply::finished,
                [&, reply, f]() {
            qDebug()<<Q_FUNC_INFO<<"--------- finished ";
            QByteArray ba = reply->readAll();
            qDebug()<<Q_FUNC_INFO<<" ba size "<<ba.size();
            f->write(ba);
            f->close();
        });
        connect(reply, &QCurl::QCNetworkReply::readyRead,
                [&, reply, f]() {
            qDebug()<<Q_FUNC_INFO<<"--------- readyRead ";
            QByteArray ba = reply->readAll();
            qDebug()<<Q_FUNC_INFO<<" ba size "<<ba.size();
            f->write(ba);
//            f->close();
        });

        reply->perform();
    }

}
