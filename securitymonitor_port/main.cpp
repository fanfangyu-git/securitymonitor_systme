#include "mainwindow.h"

#include <QApplication>
#include <QDebug>

#include "service/DatabaseManager.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName("securitymonitor");

    // 初始化数据库（生成在可执行文件同级目录）
    const QString dbPath = QCoreApplication::applicationDirPath() + "/demo.db";
    if (!DatabaseManager::instance().init(dbPath)) {
        qWarning() << "数据库初始化失败，程序继续运行（不影响预览功能）";
    }

    // --test 参数：无摄像头时用测试视频源验证链路
    const bool testMode = a.arguments().contains("--test");

    MainWindow w(testMode);
    w.show();
    const int ret = a.exec();

    DatabaseManager::instance().shutdown();
    return ret;
}