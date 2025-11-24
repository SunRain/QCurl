#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

namespace Ui {
class MainWindow;
}

namespace QCurl {
class QCNetworkAccessManager;

}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void on_pushButton_clicked();

private:
    Ui::MainWindow *ui;
    QCurl::QCNetworkAccessManager *mgr;

    void tst_async();

    void tst_sync();
};

#endif // MAINWINDOW_H
