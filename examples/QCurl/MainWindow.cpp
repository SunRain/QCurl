#include "MainWindow.h"
#include "ui_MainWindow.h"

#include <QDateTime>
#include <QDebug>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>

#include "QCNetworkAccessManager.h"
#include "QCNetworkRequest.h"
#include "QCNetworkAsyncReply.h"
#include "QCNetworkSyncReply.h"

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
//    tst_async();
    tst_sync();
}

void MainWindow::tst_async()
{
    QString cookie = QString("%1/cookie.txt").arg(QApplication::applicationDirPath());
    qDebug()<<Q_FUNC_INFO<<"cookie file "<<cookie;
    QFile *f = new QFile("test.jpg");
    f->open(QIODevice::WriteOnly);
    QStringList list;

//    list.append("https://pan.baidu.com");
    list.append("https://timgsa.baidu.com/timg?image&quality=80&size=b9999_10000&sec=1551629798534&di=600f3fe2a3435642077ae99849a10345&imgtype=0&src=http%3A%2F%2F00.minipic.eastday.com%2F20170523%2F20170523000003_d41d8cd98f00b204e9800998ecf8427e_16.jpeg");

    //    list.append("http://bos.pgzs.com/sjapp91/pcsuite/plugin/91assistant_pc_v6_1_20180416.exe");
    foreach (const QString &url, list) {
        qDebug()<<Q_FUNC_INFO<<"for url "<<url;
        QUrl u(url);
        QCurl::QCNetworkRequest request(u);
        qDebug()<<Q_FUNC_INFO<<"qcnetworkmgr "<<mgr;
        mgr->setCookieFilePath(cookie);
//        QCurl::QCNetworkAsyncReply *reply = mgr->get(request);
        QCurl::QCNetworkAsyncReply *reply = mgr->head(request);
//        reply->request();
        qDebug()<<Q_FUNC_INFO<<reply->url();

        connect(reply, &QCurl::QCNetworkAsyncReply::finished,
                [&, reply, f]() {
            qDebug()<<Q_FUNC_INFO<<"--------- finished ";
            QByteArray ba = reply->readAll();
            qDebug()<<Q_FUNC_INFO<<" ba size "<<ba.size();
            f->write(ba);
            f->close();
            reply->deleteLater();
        });
        connect(reply, &QCurl::QCNetworkAsyncReply::readyRead,
                [&, reply, f]() {
            qDebug()<<Q_FUNC_INFO<<"--------- readyRead ";
            QByteArray ba = reply->readAll();
            qDebug()<<Q_FUNC_INFO<<" ba size "<<ba.size();
            f->write(ba);
//            f->close();
        });

        reply->perform();

        qDebug()<<Q_FUNC_INFO<<"///////////////////////////////////////////////////////////";
    }
}

void MainWindow::tst_sync()
{
    QString cookie = QString("%1/cookie.txt").arg(QApplication::applicationDirPath());
    qDebug()<<Q_FUNC_INFO<<"cookie file "<<cookie;
    QFile *f = new QFile("test.jpg");
    f->open(QIODevice::WriteOnly);
    QStringList list;

    list.append("https://passport.baidu.com/v2/api/");
//    list.append("https://timgsa.baidu.com/timg?image&quality=80&size=b9999_10000&sec=1551629798534&di=600f3fe2a3435642077ae99849a10345&imgtype=0&src=http%3A%2F%2F00.minipic.eastday.com%2F20170523%2F20170523000003_d41d8cd98f00b204e9800998ecf8427e_16.jpeg");
//list.append("https://d10.baidupcs.com/file/823c6ae31470fafa407d6e2ed40163eb?bkt=p3-1400823c6ae31470fafa407d6e2ed40163eba6f0d091000000674160&xcode=088cf1928788519a798e7bf7cc7a76415f1b7244a18ac5f1bb363ff3c7dfbcc4cf06d4bc66d4fa7ef0b2038d3093ee379a7e3ac4ae9d7ad8&fid=120553430-250528-144360809492592&time=1552701007&sign=FDTAXGERLQBHSKf-DCb740ccc5511e5e8fedcff06b081203-6ZhbKjHAFy81J2MUTTCVCXtlowI%3D&to=d10&size=6766944&sta_dx=6766944&sta_cs=4778&sta_ft=apk&sta_ct=7&sta_mt=7&fm2=MH%2CYangquan%2CAnywhere%2C%2Cjiangsu%2Cct&ctime=1468209628&mtime=1468627700&resv0=cdnback&resv1=0&vuk=120553430&iv=1&htype=&newver=1&newfm=1&secfm=1&flow_ver=3&pkey=1400823c6ae31470fafa407d6e2ed40163eba6f0d091000000674160&sl=71958607&expires=8h&rt=pr&r=381151564&mlogid=1725773559289775280&vbdid=778114172&fin=%E5%B0%8F%E8%96%87%E7%9B%B4%E6%92%AD.apk&fn=%E5%B0%8F%E8%96%87%E7%9B%B4%E6%92%AD.apk&rtype=1&dp-logid=1725773559289775280&dp-callid=0.1.1&hps=1&tsl=405&csl=405&csign=F7Us623uHJQ5ym95QLTei5ibWdY%3D&so=0&ut=5&uter=4&serv=0&uc=2938311740&ti=e3357e20d22cf848f731f92f38fe5d567d7a89edab5223f6&by=themis");
    //    list.append("http://bos.pgzs.com/sjapp91/pcsuite/plugin/91assistant_pc_v6_1_20180416.exe");
    foreach (const QString &url, list) {
        qDebug()<<Q_FUNC_INFO<<"for url "<<url;
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
        request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows;U;Windows NT 5.1;zh-CN;rv:1.9.2.9) Gecko/20100101 Firefox/43.0");
        request.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");


        qDebug()<<Q_FUNC_INFO<<"qcnetworkmgr "<<mgr;
        mgr->setCookieFilePath(cookie);
//        QCurl::QCNetworkReply *reply = mgr->get(request);
        QCurl::QCNetworkSyncReply *reply = mgr->create(request);


//        connect(reply, &QCurl::QCNetworkAsyncReply::finished,
////                [&, reply, f]() {
////            qDebug()<<Q_FUNC_INFO<<"--------- finished ";
////            QByteArray ba = reply->readAll();
////            qDebug()<<Q_FUNC_INFO<<" ba size "<<ba.size();
////            f->write(ba);
////            f->close();
////            reply->deleteLater();
////        });
////        connect(reply, &QCurl::QCNetworkAsyncReply::readyRead,
////                [&, reply, f]() {
////            qDebug()<<Q_FUNC_INFO<<"--------- readyRead ";
////            QByteArray ba = reply->readAll();
////            qDebug()<<Q_FUNC_INFO<<" ba size "<<ba.size();
////            f->write(ba);
//////            f->close();
////        });

//        reply->setReadFunction([&](char *buffer, size_t size)->size_t {
//            qDebug()<<Q_FUNC_INFO<<"--------";
//            return size;
//        });

        qint64 wpos = 0;
        QByteArray ba;
        reply->setWriteFunction([&](char *buffer, size_t size) ->size_t {
            qDebug()<<Q_FUNC_INFO<<"write buffer , need "<<size;
////            f->seek(wpos);
//            QByteArray ba (buffer, size);
//            qint64 ret = f->write(buffer, static_cast<qint64>(size));
//            qDebug()<<Q_FUNC_INFO<<"write size"<<ret;
//            wpos += ret;
//            return static_cast<size_t>(ret);
//            qDebug()<<Q_FUNC_INFO<<"------- before ba "<<ba.size();
//            QByteArray a(buffer ,size);
            ba.append(buffer, static_cast<int>(size));

            qDebug()<<Q_FUNC_INFO<<"ba "<<ba;

            qDebug()<<Q_FUNC_INFO<<"--- now ba size"<<ba.size();

            return size;
        });

//        reply->setHeaderFunction([&](char *data, size_t size)->size_t {
//            qDebug()<<Q_FUNC_INFO<<"setHeaderFunction url ["<<url
//                   <<"] header "<<QString::fromUtf8(data, static_cast<int>(size));
//            return size;
//        });

        reply->perform();
        qDebug()<<Q_FUNC_INFO<<"///////////////////////////////////////////////////////////";
        if (reply->error() == QCurl::NetworkNoError) {
            qDebug()<<Q_FUNC_INFO<<"---------- no error "<<reply->error()<<"  "<<reply->errorString();
        }
        qDebug()<<Q_FUNC_INFO<<"ba size "<<ba.size()<< " data  "<<ba;
//        f->write(ba);
//        f->flush();
//        f->close();
    }
}
