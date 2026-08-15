#include "FaceAnalyzer.h"

#include <QFileInfo>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

namespace {

// 统一缩放：大画面先缩到最长边 <= 640，显著降低 YuNet 耗时（scale 为缩放比）
cv::Mat prepareWork(const cv::Mat& frame, double* scale)
{
    *scale = 1.0;
    cv::Mat work = frame;
    const int maxDim = 640;
    const int longSide = std::max(frame.cols, frame.rows);
    if (longSide > maxDim) {
        *scale = static_cast<double>(maxDim) / longSide;
        cv::resize(frame, work, cv::Size(), *scale, *scale, cv::INTER_AREA);
    }
    return work;
}

// 把 YuNet 输出（缩放图坐标系）的人脸框映射回原图坐标
void appendBoxes(const cv::Mat& faces, double scale,
                 std::vector<FaceAnalyzer::Result>& out)
{
    const int count = faces.rows;
    out.reserve(out.size() + static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float* row = faces.ptr<float>(i);
        FaceAnalyzer::Result r;
        r.face = cv::Rect(cvRound(row[0] / scale), cvRound(row[1] / scale),
                          cvRound(row[2] / scale), cvRound(row[3] / scale));
        out.push_back(r);
    }
}

// InsightFace 标准 112x112 对齐模板
// （顺序与 YuNet 输出一致：左眼、右眼、鼻尖、左嘴角、右嘴角）
const cv::Point2f kAlignTemplate[5] = {
    cv::Point2f(38.2946f, 51.6963f),
    cv::Point2f(73.5318f, 51.5014f),
    cv::Point2f(56.0252f, 71.7366f),
    cv::Point2f(41.5493f, 92.3655f),
    cv::Point2f(70.7299f, 92.2041f)
};
const cv::Size kAlignSize(112, 112);
const size_t kFeatureDim = 512;

} // namespace

bool FaceAnalyzer::loadModels(const QString& dir)
{
    const QString detectModel = dir + "/face_detection_yunet_2022mar.onnx";
    const QString arcfaceModel = dir + "/w600k_mbf.onnx";
    if (!QFileInfo::exists(detectModel) || !QFileInfo::exists(arcfaceModel)) {
        return false;
    }

    QMutexLocker locker(&m_mutex);
    m_detector = cv::FaceDetectorYN::create(
        detectModel.toLocal8Bit().constData(), "", cv::Size(320, 320));
    m_net = cv::dnn::readNetFromONNX(arcfaceModel.toLocal8Bit().constData());
    m_ready = !m_detector.empty() && !m_net.empty();
    m_inputSize = cv::Size();
    m_detectorModel = detectModel;
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

    const Detection det = detectBoxesLocked(frame);
    results = det.results;

    for (int i = 0; i < static_cast<int>(results.size()); ++i) {
        Result& r = results[i];
        if (recognize) {
            // 关键：5 点对齐必须接收 YuNet 输出的完整 1x15 人脸行
            // （x,y,w,h + 5 个关键点 + score），并与检测使用同一张缩放图，
            // 保证注册与识别特征一致；只传 x,y,w,h 无法对齐
            const cv::Mat faceBox = det.faces.row(i);
            cv::Mat aligned;
            if (!alignFace(det.work, faceBox, aligned)) continue;

            cv::Mat feature;
            if (!extractFeature(aligned, feature)) continue;
            feature = feature.clone();

            r.feature = featureToBytes(feature);

            // 与人员库逐人比对，取余弦相似度最高者
            // （特征已 L2 归一化，内积即余弦相似度，越大越像）
            double best = -1.0;
            for (const Person& p : m_persons) {
                if (p.feature.empty() || p.feature.total() != feature.total()) continue;
                const double sim = feature.dot(p.feature);
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
    }

    return results;
}

cv::Ptr<cv::FaceDetectorYN> FaceAnalyzer::createDetector() const
{
    if (m_detectorModel.isEmpty()) return nullptr;
    return cv::FaceDetectorYN::create(m_detectorModel.toLocal8Bit().constData(),
                                      "", cv::Size(320, 320));
}

bool FaceAnalyzer::detectBoxes(const cv::Mat& frame,
                               const cv::Ptr<cv::FaceDetectorYN>& detector,
                               std::vector<Result>& out) const
{
    out.clear();
    if (detector.empty() || frame.empty()) return false;

    double scale = 1.0;
    const cv::Mat work = prepareWork(frame, &scale);
    detector->setInputSize(work.size());
    cv::Mat faces;
    detector->detect(work, faces);
    appendBoxes(faces, scale, out);
    return true;
}

FaceAnalyzer::Detection FaceAnalyzer::detectBoxesLocked(const cv::Mat& frame)
{
    Detection det;

    double scale = 1.0;
    det.work = prepareWork(frame, &scale);
    if (!ensureInputSize(det.work)) return det;

    m_detector->detect(det.work, det.faces);
    appendBoxes(det.faces, scale, det.results);
    return det;
}

bool FaceAnalyzer::alignFace(const cv::Mat& src, const cv::Mat& faceRow, cv::Mat& dst)
{
    if (src.empty() || faceRow.empty() || faceRow.rows < 1 || faceRow.cols < 14) {
        return false;
    }

    std::vector<cv::Point2f> srcPts(5);
    const float* row = faceRow.ptr<float>(0);
    for (int i = 0; i < 5; ++i) {
        srcPts[i] = cv::Point2f(row[4 + i * 2], row[5 + i * 2]);
    }
    const std::vector<cv::Point2f> dstPts(kAlignTemplate, kAlignTemplate + 5);

    // 相似变换（旋转+平移+等比缩放），与 InsightFace 官方对齐一致
    const cv::Mat M = cv::estimateAffinePartial2D(srcPts, dstPts);
    if (M.empty()) return false;

    cv::warpAffine(src, dst, M, kAlignSize, cv::INTER_LINEAR,
                   cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    return true;
}

bool FaceAnalyzer::extractFeature(const cv::Mat& aligned, cv::Mat& feature)
{
    if (aligned.empty() || m_net.empty()) return false;

    // InsightFace 预处理：BGR->RGB(swapRB)、scale=1/128、mean=127.5，
    // 即 (pixel - 127.5) / 128，与官方 w600k_mbf 推理保持一致
    const cv::Mat blob = cv::dnn::blobFromImage(
        aligned, 1.0 / 128.0, kAlignSize, cv::Scalar(127.5, 127.5, 127.5),
        true /*swapRB*/, false /*crop*/);

    try {
        m_net.setInput(blob);
        cv::Mat out = m_net.forward();
        out = out.reshape(1, 1);                      // 展平为 1xN
        if (out.type() != CV_32F) out.convertTo(out, CV_32F);
        cv::normalize(out, feature, 1.0, 0.0, cv::NORM_L2); // 归一化后内积即余弦相似度
    } catch (const cv::Exception&) {
        return false;
    }
    return feature.total() == kFeatureDim;
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
