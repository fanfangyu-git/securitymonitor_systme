#ifndef FACEANALYZER_H
#define FACEANALYZER_H

#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>

#include <QByteArray>
#include <QMutex>
#include <QString>

#include <vector>

// 人脸识别模块：YuNet 检测 + SFace 特征提取/比对。
// - analyze(): 检测画面中的人脸，可提取特征并与人员库比对（命中返回姓名，未命中即陌生人）
// - 内部互斥锁保护模型调用，多路采集线程可安全共享同一个实例
class FaceAnalyzer
{
public:
    struct Person
    {
        int id = -1;
        QString name;
        cv::Mat feature; // 1x128 CV_32F
    };

    struct Result
    {
        cv::Rect face;
        QString name;      // 命中人员库则为姓名，否则为空（陌生人）
        bool known = false;
        double score = 0.0; // 余弦相似度（越大越像，>= threshold 判定为同一人）
        QByteArray feature; // 该人脸 128 维特征（512 字节），供登记用
    };

    // 模型目录需包含 face_detection_yunet_2022mar.onnx 与 face_recognition_sface_2021dec.onnx
    bool loadModels(const QString& dir);
    bool isReady() const;

    // 分析一帧：检测所有人脸；recognize=true 时额外提取特征做比对（较耗时，建议节流调用）。
    std::vector<Result> analyze(const cv::Mat& frame, bool recognize, bool drawOverlay);

    // 人员库（内存）
    void loadPersons(const std::vector<Person>& persons);
    void addPerson(const Person& person);
    int personCount() const;

    // 特征序列化（数据库 BLOB 存取：128 个 float = 512 字节）
    static QByteArray featureToBytes(const cv::Mat& feature);
    static cv::Mat bytesToFeature(const QByteArray& bytes);

    // 余弦相似度阈值：大于等于该值判定为同一人（SFace FR_COSINE 默认 0.363）
    void setThreshold(double t) { m_threshold = t; }
    double threshold() const { return m_threshold; }

private:
    bool ensureInputSize(const cv::Mat& frame);

    cv::Ptr<cv::FaceDetectorYN> m_detector;
    cv::Ptr<cv::FaceRecognizerSF> m_recognizer;
    std::vector<Person> m_persons;
    mutable QMutex m_mutex;
    bool m_ready = false;
    double m_threshold = 0.363;
    cv::Size m_inputSize;
};

#endif // FACEANALYZER_H