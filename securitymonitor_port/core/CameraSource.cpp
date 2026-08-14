#include "CameraSource.h"

CameraSource::CameraSource(int deviceIndex)
    : m_deviceIndex(deviceIndex)
{
}

bool CameraSource::open()
{
    if (m_capture.isOpened()) return true;
    if (!m_capture.open(m_deviceIndex)) return false;
    // 适当降低采集分辨率，保证预览流畅
    m_capture.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    m_capture.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    return true;
}

bool CameraSource::read(cv::Mat& frame)
{
    if (!m_capture.isOpened()) return false;
    return m_capture.read(frame);
}

void CameraSource::close()
{
    if (m_capture.isOpened()) m_capture.release();
}

bool CameraSource::isOpen() const
{
    return m_capture.isOpened();
}

QString CameraSource::name() const
{
    return QStringLiteral("摄像头 %1").arg(m_deviceIndex);
}