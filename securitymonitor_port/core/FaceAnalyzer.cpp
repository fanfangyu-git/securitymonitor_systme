#include "FaceAnalyzer.h"

#include <QFileInfo>

#include <opencv2/imgproc.hpp>

bool FaceAnalyzer::loadModels(const QString& dir)
{
    const QString detectModel = dir + "/face_detection_yunet_2022mar.onnx";
    const QString sfaceModel = dir + "/face_recognition_sface_2021dec.onnx";
    if (!QFileInfo::exists(detectModel) || !QFileInfo::exists(sfaceModel)) {
        return false;
    }

    QMutexLocker locker(&m_mutex);
    m_detector = cv::FaceDetectorYN::create(
        detectModel.toLocal8Bit().constData(), "", cv::Size(320, 320));
    m_recognizer = cv::FaceRecognizerSF::create(sfaceModel.toLocal8Bit().constData(), "");
    m_ready = !m_detector.empty() && !m_recognizer.empty();
    m_inputSize = cv::Size();
    return m_ready;
}

bool FaceAnalyzer::isReady() const
{
    return m_ready;
}

bool FaceAnalyzer::ensureInputSize(const cv::Mat& frame)
{
    const cv::Size size(frame.cols, frame.rows);
    if (size != m_inputSize) {
        m_detector->setInputSize(size);
        m_inputSize = size;
    }
    return !frame.empty();
}

std::vector<FaceAnalyzer::Result> FaceAnalyzer::analyze(const cv::Mat& frame,
                                                        bool recognize,
                                                        bool drawOverlay)
{
    std::vector<Result> results;
    if (!m_ready || frame.empty()) return results;

    QMutexLocker locker(&m_mutex);

    // 检测用缩放图：大画面先缩到最长边 <= 640，显著降低 YuNet 耗时；
    // 人脸框坐标按比例映射回原图，识别裁剪仍用原图保证特征质量
    cv::Mat work = frame;
    double scale = 1.0;
    const int maxDim = 640;
    const int longSide = std::max(frame.cols, frame.rows);
    if (longSide > maxDim) {
        scale = static_cast<double>(maxDim) / longSide;
        cv::resize(frame, work, cv::Size(), scale, scale, cv::INTER_AREA);
    }
    if (!ensureInputSize(work)) return results;

    cv::Mat faces;
    m_detector->detect(work, faces);

    const int count = faces.rows;
    for (int i = 0; i < count; ++i) {
        const float* row = faces.ptr<float>(i);
        Result r;
        r.face = cv::Rect(cvRound(row[0] / scale), cvRound(row[1] / scale),
                          cvRound(row[2] / scale), cvRound(row[3] / scale));

        if (recognize) {
            // 关键：alignCrop 必须接收 YuNet 输出的完整 1x15 人脸行
            // （x,y,w,h + 5 个关键点 + score），并与检测使用同一张缩放图，
            // 保证注册与识别特征一致；只传 x,y,w,h 会导致第二次运行无法识别
            const cv::Mat faceBox = faces.row(i);
            cv::Mat aligned;
            m_recognizer->alignCrop(work, faceBox, aligned);
            cv::Mat feature;
            m_recognizer->feature(aligned, feature);
            feature = feature.clone();

            r.feature = featureToBytes(feature);

            // 与人员库逐人比对，取余弦相似度最高者（SFace FR_COSINE：越大越像）
            double best = -1.0;
            for (const Person& p : m_persons) {
                if (p.feature.empty()) continue;
                const double sim = m_recognizer->match(feature, p.feature);
                if (sim > best) {
                    best = sim;
                    r.name = p.name;
                }
            }
            r.score = best;
            r.known = best >= m_threshold;
        }

        if (drawOverlay) {
            const cv::Scalar color = r.known ? cv::Scalar(0, 255, 0)   // 已知：绿框
                                             : cv::Scalar(0, 0, 255);  // 陌生人：红框
            cv::rectangle(frame, r.face, color, 2);
            std::string label = "face";
            if (r.known) label = r.name.toLocal8Bit().constData();
            else if (recognize) label = "stranger";
            cv::putText(frame, label, cv::Point(r.face.x, r.face.y - 6),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
        }

        results.push_back(r);
    }

    return results;
}

void FaceAnalyzer::loadPersons(const std::vector<Person>& persons)
{
    QMutexLocker locker(&m_mutex);
    m_persons = persons;
}

void FaceAnalyzer::addPerson(const Person& person)
{
    QMutexLocker locker(&m_mutex);
    m_persons.push_back(person);
}

int FaceAnalyzer::personCount() const
{
    QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_persons.size());
}

QByteArray FaceAnalyzer::featureToBytes(const cv::Mat& feature)
{
    if (feature.empty()) return QByteArray();
    return QByteArray(reinterpret_cast<const char*>(feature.data),
                      static_cast<int>(feature.total() * feature.elemSize()));
}

cv::Mat FaceAnalyzer::bytesToFeature(const QByteArray& bytes)
{
    if (bytes.isEmpty() || bytes.size() % sizeof(float) != 0) return cv::Mat();
    cv::Mat feature(1, bytes.size() / static_cast<int>(sizeof(float)),
                    CV_32F, const_cast<char*>(bytes.constData()));
    return feature.clone();
}