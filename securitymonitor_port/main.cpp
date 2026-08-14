#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QStringList>
#include <QTextStream>

#include <opencv2/opencv.hpp>

#include "core/FaceAnalyzer.h"
#include "service/DatabaseManager.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName("securitymonitor");

    // faceBoxes 信号（QueuedConnection）需要 QVector<FaceBox> 元类型
    qRegisterMetaType<QVector<FaceBox>>("QVector<FaceBox>");

    const QStringList args = a.arguments();
    const bool testMode = args.contains("--test");

    // --db-path 路径：SQLite 数据库文件位置（默认 exe 同级 demo.db）
    QString dbPath = QCoreApplication::applicationDirPath() + "/demo.db";
    const int dbIdx = args.indexOf(QStringLiteral("--db-path"));
    if (dbIdx >= 0 && dbIdx + 1 < args.size()) {
        dbPath = args.at(dbIdx + 1);
    }
    if (!DatabaseManager::instance().init(dbPath)) {
        qWarning() << "数据库初始化失败，程序继续运行（不影响预览功能）";
    }
    // --history-dump：历史查询自检，输出事件/录像到 history_selftest.txt 后退出
    if (args.contains(QStringLiteral("--history-dump"))) {
        QFile hf(QCoreApplication::applicationDirPath() + "/history_selftest.txt");
        hf.open(QIODevice::WriteOnly | QIODevice::Text);
        QTextStream hs(&hf);
        hs.setCodec("UTF-8");
        const auto evs = DatabaseManager::instance().queryEvents();
        hs << QStringLiteral("事件数: ") << evs.size() << "\n";
        for (const auto& e : evs) {
            hs << e.happenedAt << " | " << e.type << " | " << e.sourceName << " | " << e.detail << "\n";
        }
        const auto vids = DatabaseManager::instance().queryVideoFiles();
        hs << QStringLiteral("录像数: ") << vids.size() << "\n";
        for (const auto& v : vids) {
            hs << v.startTime << " | " << v.filePath << " | " << v.eventType << "\n";
        }
        hs.flush();
        return 0;
    }


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

    // --face-dir 路径：人脸模型目录（含 face_detection_yunet_*.onnx 与 face_recognition_sface_*.onnx）
    // 默认 exe 同级 models/；缺失时人脸识别功能自动禁用
    QString faceDir;
    const int faceIdx = args.indexOf(QStringLiteral("--face-dir"));
    if (faceIdx >= 0 && faceIdx + 1 < args.size()) {
        faceDir = args.at(faceIdx + 1);
    }

    // --face-selftest <图片> [--register <姓名>]：诊断模式——加载模型/人员库，
    // 检测并识别图片中的人脸后退出（可选把第一张人脸登记进人员库）
    const int selftestIdx = args.indexOf(QStringLiteral("--face-selftest"));
    if (selftestIdx >= 0 && selftestIdx + 1 < args.size()) {
        const QString modelDir = faceDir.isEmpty()
            ? QCoreApplication::applicationDirPath() + "/models" : faceDir;
        FaceAnalyzer analyzer;
        if (!analyzer.loadModels(modelDir)) {
            qWarning() << "人脸模型加载失败:" << modelDir;
            return 1;
        }
        // 加载人员库（与主窗口一致）
        {
            std::vector<FaceAnalyzer::Person> persons;
            const auto infos = DatabaseManager::instance().loadPersons();
            for (const auto& info : infos) {
                FaceAnalyzer::Person p;
                p.id = info.id;
                p.name = info.name;
                p.feature = FaceAnalyzer::bytesToFeature(info.feature);
                if (!p.feature.empty()) persons.push_back(p);
            }
            analyzer.loadPersons(persons);
            qDebug() << "人员库人数:" << persons.size();
        }
        cv::Mat img = cv::imread(args.at(selftestIdx + 1).toLocal8Bit().constData());
        if (img.empty()) {
            qWarning() << "图片读取失败:" << args.at(selftestIdx + 1);
            return 1;
        }
        const auto results = analyzer.analyze(img, true, false);
        qDebug() << "人脸检测数:" << results.size();

        // 同时输出到 exe 同级 face_selftest.txt（GUI 程序控制台输出不可见）
        QFile outFile(QCoreApplication::applicationDirPath() + "/face_selftest.txt");
        outFile.open(QIODevice::WriteOnly | QIODevice::Text);
        QTextStream out(&outFile);
        out.setCodec("UTF-8");
        // 注意：QTextStream 的 const char* 重载按 Latin-1 解释，中文必须用 QStringLiteral
        out << QStringLiteral("人员库人数: ") << analyzer.personCount() << "\n";
        out << QStringLiteral("人脸检测数: ") << results.size() << "\n";
        for (const auto& r : results) {
            out << QStringLiteral("  人脸框 ") << r.face.x << "," << r.face.y << " "
                << r.face.width << "x" << r.face.height
                << QStringLiteral(" | ") << (r.known ? (QStringLiteral("识别为: ") + r.name) : QStringLiteral("陌生人"))
                << QStringLiteral(" | 相似度 ") << r.score
                << QStringLiteral(" | 特征字节 ") << r.feature.size() << "\n";
            qDebug() << "  人脸框" << r.face.x << r.face.y << r.face.width << r.face.height
                     << (r.known ? ("识别为: " + r.name) : QStringLiteral("陌生人"))
                     << "相似度" << r.score << "特征字节" << r.feature.size();
        }
        out.flush();
        const int regIdx = args.indexOf(QStringLiteral("--register"));
        if (regIdx >= 0 && regIdx + 1 < args.size() && !results.empty()) {
            const QString regName = args.at(regIdx + 1);
            if (DatabaseManager::instance().addPerson(regName, results.front().feature)) {
                qDebug() << "已登记人员:" << regName;
            }
        }
        return 0;
    }

    MainWindow w(testMode, fileSources, recordDir, faceDir, maxStorageBytes);
    w.show();
    if (args.contains(QStringLiteral("--autostart"))) {
        if (channelCount > 0) w.setChannelCount(channelCount);
        w.autoStart();
    }
    const int ret = a.exec();

    DatabaseManager::instance().shutdown();
    return ret;
}