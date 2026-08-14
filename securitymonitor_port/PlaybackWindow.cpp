#include "PlaybackWindow.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

#include <opencv2/imgproc.hpp>

#include "core/ImageConvert.h"

PlaybackWindow::PlaybackWindow(const QString& filePath, const QString& title, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title.isEmpty() ? QStringLiteral("录像回放") : title);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(720, 560);

    m_titleLabel = new QLabel(QFileInfo(filePath).fileName(), this);
    m_titleLabel->setAlignment(Qt::AlignCenter);

    m_videoLabel = new QLabel(this);
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setMinimumSize(320, 240);
    m_videoLabel->setStyleSheet(QStringLiteral("background-color: #101010; color: #888888;"));

    m_slider = new QSlider(Qt::Horizontal, this);
    m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), this);

    m_playButton = new QPushButton(QStringLiteral("暂停"), this);
    m_stopButton = new QPushButton(QStringLiteral("停止"), this);

    auto* sliderRow = new QHBoxLayout;
    sliderRow->addWidget(m_slider, 1);
    sliderRow->addWidget(m_timeLabel);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    btnRow->addWidget(m_playButton);
    btnRow->addWidget(m_stopButton);
    btnRow->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_videoLabel, 1);
    layout->addLayout(sliderRow);
    layout->addLayout(btnRow);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &PlaybackWindow::onNextFrame);
    connect(m_playButton, &QPushButton::clicked, this, &PlaybackWindow::onPlayPauseClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &PlaybackWindow::onStopClicked);
    connect(m_slider, &QSlider::sliderPressed, this, &PlaybackWindow::onSliderPressed);
    connect(m_slider, &QSlider::sliderReleased, this, &PlaybackWindow::onSliderReleased);

    if (!openFile(filePath)) {
        m_playButton->setEnabled(false);
        m_stopButton->setEnabled(false);
        m_slider->setEnabled(false);
        m_videoLabel->setText(QStringLiteral("无法打开录像文件：%1").arg(QFileInfo(filePath).fileName()));
        return;
    }

    m_timer->start(1000 / qMax(1, m_fps));
    m_playing = true;
}

PlaybackWindow::~PlaybackWindow()
{
    if (m_timer) m_timer->stop();
    if (m_cap.isOpened()) m_cap.release();
}

bool PlaybackWindow::openFile(const QString& filePath)
{
    if (!QFileInfo::exists(filePath)) return false;
    if (!m_cap.open(filePath.toLocal8Bit().constData())) return false;

    const double fps = m_cap.get(cv::CAP_PROP_FPS);
    if (fps > 1.0 && fps <= 120.0) m_fps = static_cast<int>(std::lround(fps));
    const double frames = m_cap.get(cv::CAP_PROP_FRAME_COUNT);
    m_durationMs = (frames > 0 && m_fps > 0)
        ? static_cast<int>(frames * 1000.0 / m_fps) : 0;
    m_slider->setRange(0, qMax(1, m_durationMs));
    m_slider->setValue(0);
    updateTimeLabel(0);
    return true;
}

void PlaybackWindow::onNextFrame()
{
    if (!m_cap.isOpened()) return;
    cv::Mat frame;
    if (!m_cap.read(frame)) {
        // 播放到结尾：停在开头方便重播
        m_timer->stop();
        m_playing = false;
        m_playButton->setText(QStringLiteral("播放"));
        m_cap.set(cv::CAP_PROP_POS_FRAMES, 0);
        m_slider->setValue(0);
        updateTimeLabel(0);
        if (m_cap.read(frame)) showFrame(frame);
        return;
    }
    showFrame(frame);
    if (!m_seeking) {
        const int pos = static_cast<int>(m_cap.get(cv::CAP_PROP_POS_MSEC));
        m_slider->setValue(qBound(0, pos, m_durationMs));
        updateTimeLabel(pos);
    }
}

void PlaybackWindow::showFrame(const cv::Mat& frame)
{
    const QImage image = matToQImage(frame);
    if (image.isNull()) return;
    const QSize target = m_videoLabel->size();
    QPixmap pix = QPixmap::fromImage(image).scaled(
        target.isEmpty() ? image.size() : target,
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_videoLabel->setPixmap(pix);
}

void PlaybackWindow::onPlayPauseClicked()
{
    if (!m_cap.isOpened()) return;
    if (m_playing) {
        m_timer->stop();
        m_playing = false;
        m_playButton->setText(QStringLiteral("播放"));
    } else {
        m_timer->start(1000 / qMax(1, m_fps));
        m_playing = true;
        m_playButton->setText(QStringLiteral("暂停"));
    }
}

void PlaybackWindow::onStopClicked()
{
    if (!m_cap.isOpened()) return;
    m_timer->stop();
    m_playing = false;
    m_playButton->setText(QStringLiteral("播放"));
    m_cap.set(cv::CAP_PROP_POS_MSEC, 0);
    m_slider->setValue(0);
    updateTimeLabel(0);
    cv::Mat frame;
    if (m_cap.read(frame)) showFrame(frame);
}

void PlaybackWindow::onSliderPressed()
{
    m_seeking = true;
    if (m_playing) {
        m_timer->stop();
        m_playing = false;
        m_playButton->setText(QStringLiteral("播放"));
    }
}

void PlaybackWindow::onSliderReleased()
{
    m_seeking = false;
    const int pos = m_slider->value();
    if (m_cap.isOpened()) m_cap.set(cv::CAP_PROP_POS_MSEC, pos);
    cv::Mat frame;
    if (m_cap.read(frame)) showFrame(frame);
    updateTimeLabel(pos);
    if (m_playing) m_timer->start(1000 / qMax(1, m_fps));
}

void PlaybackWindow::updateTimeLabel(int ms)
{
    const int total = qMax(0, m_durationMs);
    const QString fmt = QStringLiteral("%1:%2 / %3:%4")
        .arg(ms / 60000, 2, 10, QLatin1Char('0'))
        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(total / 60000, 2, 10, QLatin1Char('0'))
        .arg((total / 1000) % 60, 2, 10, QLatin1Char('0'));
    m_timeLabel->setText(fmt);
}