#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QStringList>

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

    const QStringList args = a.arguments();
    const bool testMode = args.contains("--test");

    // --file 视频文件路径：用作第2路及之后的视频源（可多个）
    QStringList fileSources;
    for (int i = 0; i < args.size() - 1; ++i) {
        if (args.at(i) == QStringLiteral("--file")) {
            fileSources << args.at(i + 1);
        }
    }

    // --channels N：启动前指定路数（1/2/4）
    int channelCount = 0;
    const int channelIdx = args.indexOf(QStringLiteral("--channels"));
    if (channelIdx >= 0 && channelIdx + 1 < args.size()) {
        bool ok = false;
        const int n = args.at(channelIdx + 1).toInt(&ok);
        if (ok) channelCount = n;
    }

    // --record-dir 路径：录像文件保存目录（默认 exe 同级 records/）
    QString recordDir;
    const int recordIdx = args.indexOf(QStringLiteral("--record-dir"));
    if (recordIdx >= 0 && recordIdx + 1 < args.size()) {
        recordDir = args.at(recordIdx + 1);
    }

    // --max-storage N：录像目录总大小上限(单位 MB)，默认 5000(约5GB)。
    // 超过后从最旧的录像开始自动删除（同步清理数据库记录）。
    qint64 maxStorageMB = 5000;
    const int maxStorageIdx = args.indexOf(QStringLiteral("--max-storage"));
    if (maxStorageIdx >= 0 && maxStorageIdx + 1 < args.size()) {
        bool ok = false;
        const qint64 v = args.at(maxStorageIdx + 1).toLongLong(&ok);
        if (ok && v > 0) maxStorageMB = v;
    }
    const qint64 maxStorageBytes = maxStorageMB * 1024 * 1024;

    MainWindow w(testMode, fileSources, recordDir, maxStorageBytes);
    w.show();
    if (args.contains(QStringLiteral("--autostart"))) {
        if (channelCount > 0) w.setChannelCount(channelCount);
        w.autoStart();
    }
    const int ret = a.exec();

    DatabaseManager::instance().shutdown();
    return ret;
}