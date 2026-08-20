#ifndef FACEANALYZER_H
#define FACEANALYZER_H

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/objdetect.hpp>

#include <QByteArray>
#include <QMutex>
#include <QString>

#include <vector>

// 人脸识别模块：YuNet 检测 + InsightFace ArcFace(w600k_mbf) 特征提取/比对。
// - analyze(): 检测画面中的人脸，可提取特征并与人员库比对（命中返回姓名，未命中即陌生人）
// - 识别链路：YuNet 5 关键点 -> 相似变换对齐 112x112 -> cv::dnn 前向 -> 512 维 L2 归一化特征
// - 内部互斥锁保护模型调用，多路采集线程可安全共享同一个实例
class FaceAnalyzer
{
public:
    struct Person
    {
        int id = -1;
        QString name;
        cv::Mat feature; // 1x512 CV_32F（L2 归一化，可直接内积求余弦）
    };

    struct Result
    {
        cv::Rect face;
        QString name;      // 命中人员库则为姓名，否则为空（陌生人）
        bool known = false;
        bool decided = false; // 是否完成可靠判决（质量门控通过才算；模糊/过小脸为 false，调用方不应切换身份/上报事件）
        double score = 0.0; // 余弦相似度（越大越像，>= threshold 判定为同一人）
        QByteArray feature; // 该人脸 512 维特征（2048 字节），供登记用；质量门控未通过时为空
    };

    // 模型目录需包含 face_detection_yunet_2022mar.onnx 与 w600k_mbf.onnx
    bool loadModels(const QString& dir);
    bool isReady() const;

    // 分析一帧：检测所有人脸；recognize=true 时额外提取特征做比对（较耗时，建议节流调用）。
    std::vector<Result> analyze(const cv::Mat& frame, bool recognize, bool drawOverlay);

    // 为调用方创建独立的人脸框检测器（YuNet 实例）。
    // 每路预览用一个自己的实例做同步检测，避免与共享的异步识别抢占同一把模型锁。
    cv::Ptr<cv::FaceDetectorYN> createDetector() const;

    // 无锁同步检测：用调用方持有的独立检测器对当前帧检测人脸框（不提取特征/不比对）。
    // 结果写入 out（可能为空=无人脸）；失败返回 false。
    bool detectBoxes(const cv::Mat& frame,
                     const cv::Ptr<cv::FaceDetectorYN>& detector,
                     std::vector<Result>& out) const;

    // 人员库（内存）
    void loadPersons(const std::vector<Person>& persons);
    void addPerson(const Person& person);
    int personCount() const;

    // 特征序列化（数据库 BLOB 存取：512 个 float = 2048 字节）
    static QByteArray featureToBytes(const cv::Mat& feature);
    static cv::Mat bytesToFeature(const QByteArray& bytes);

    // 余弦相似度阈值：大于等于该值判定为同一人（ArcFace w600k_mbf 常用 0.4）
    void setThreshold(double t) { m_threshold = t; }
    double threshold() const { return m_threshold; }

    // 脸质量门控：YuNet 置信度（0~1）低于 m_minDetectScore，
    // 或缩放工作图上的人脸边长小于 m_minFaceSize 时不做识别判决
    // （特征噪声大，判决不可靠；只出框不出结论，由调用方保持上一身份）
    void setQualityGate(double minDetectScore, int minFaceSize)
    {
        m_minDetectScore = minDetectScore;
        m_minFaceSize = minFaceSize;
    }

private:
    struct Detection
    {
        std::vector<Result> results; // 原图坐标的人脸框（仅 face 有效）
        cv::Mat work;                // 检测用缩放图（识别对齐用）
        cv::Mat faces;               // YuNet 原始 1x15 输出（含关键点，识别对齐用）
    };

    // 检测实现（须已持有 m_mutex）：缩放 + YuNet 检测，结果坐标映射回原图
    Detection detectBoxesLocked(const cv::Mat& frame);

    // 用 YuNet 输出的 1x15 人脸行做 5 点相似变换对齐（InsightFace 112x112 模板）
    bool alignFace(const cv::Mat& src, const cv::Mat& faceRow, cv::Mat& dst);
    // 对齐图前向提取 512 维特征并 L2 归一化
    bool extractFeature(const cv::Mat& aligned, cv::Mat& feature);

    bool ensureInputSize(const cv::Mat& frame);

    cv::Ptr<cv::FaceDetectorYN> m_detector;
    cv::dnn::Net m_net; // ArcFace w600k_mbf.onnx
    std::vector<Person> m_persons;
    mutable QMutex m_mutex;
    bool m_ready = false;
    double m_threshold = 0.4;
    double m_minDetectScore = 0.55; // YuNet 检测置信度下限（低于此值不判决）
    int m_minFaceSize = 24;         // 缩放工作图上的最小人脸边长（像素）
    cv::Size m_inputSize;
    QString m_detectorModel; // YuNet 模型路径（供 createDetector 复制实例用）
};

#endif // FACEANALYZER_H
