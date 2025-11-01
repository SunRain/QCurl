#include <QCoreApplication>
#include <QTextStream>
#include <QDebug>
#include "PoolDemo.h"
#include "PerformanceTest.h"

void printWelcome()
{
    qDebug() << "";
    qDebug() << "==========================================";
    qDebug() << "   QCurl WebSocket 连接池演示程序";
    qDebug() << "              v2.5.0";
    qDebug() << "==========================================";
    qDebug() << "";
    qDebug() << "本程序展示 QCWebSocketPool 的功能和性能优势";
    qDebug() << "";
}

void printMenu()
{
    qDebug() << "";
    qDebug() << "========== 主菜单 ==========";
    qDebug() << "1. 基本使用演示";
    qDebug() << "2. 预热连接演示";
    qDebug() << "3. 统计信息演示";
    qDebug() << "4. 多 URL 管理演示";
    qDebug() << "5. 性能测试（连接建立时间）";
    qDebug() << "6. 性能测试（吞吐量）";
    qDebug() << "7. 性能测试（TLS 握手）";
    qDebug() << "8. 运行所有性能测试";
    qDebug() << "0. 退出";
    qDebug() << "============================";
    qDebug() << "";
}

int getChoice()
{
    QTextStream in(stdin);
    QString input;
    
    qDebug().noquote() << "请选择 (0-8): ";
    in.readLineInto(&input);
    
    bool ok = false;
    int choice = input.toInt(&ok);
    
    if (!ok || choice < 0 || choice > 8) {
        qWarning() << "❌ 无效选择，请输入 0-8 之间的数字";
        return -1;
    }
    
    return choice;
}

void waitForEnter()
{
    qDebug() << "";
    qDebug() << "按 Enter 键返回主菜单...";
    QTextStream in(stdin);
    QString dummy;
    in.readLineInto(&dummy);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    printWelcome();
    
    PoolDemo demo;
    PerformanceTest perfTest;
    
    bool running = true;
    while (running) {
        printMenu();
        int choice = getChoice();
        
        if (choice < 0) {
            continue;
        }
        
        qDebug() << "";
        
        switch (choice) {
        case 0:
            qDebug() << "👋 感谢使用！再见！";
            running = false;
            break;
            
        case 1:
            demo.demoBasicUsage();
            waitForEnter();
            break;
            
        case 2:
            demo.demoPreWarm();
            waitForEnter();
            break;
            
        case 3:
            demo.demoStatistics();
            waitForEnter();
            break;
            
        case 4:
            demo.demoMultipleUrls();
            waitForEnter();
            break;
            
        case 5:
            perfTest.testConnectionTime();
            waitForEnter();
            break;
            
        case 6:
            perfTest.testThroughput();
            waitForEnter();
            break;
            
        case 7:
            perfTest.testTlsHandshakes();
            waitForEnter();
            break;
            
        case 8:
            perfTest.runAllTests();
            waitForEnter();
            break;
            
        default:
            qWarning() << "❌ 未实现的选项";
            break;
        }
    }
    
    return 0;
}
