#ifndef PLAYBACKWINDOW_H
#define PLAYBACKWINDOW_H

#include <QDialog>
#include <QImage>
#include <QString>

#include <opencv2/videoio.hpp>

class QLabel;
class QPushButton;
class QSlider;
class QTimer;

// 录像回放窗口：OpenCV VideoCapture 解码 + QTimer 逐帧显示
// 支持播放 / 暂停 / 停止 / 拖动进度条
class PlaybackWindow : public QDialog
{
    Q_OBJECT

public:
    explicit PlaybackWindow(const QString& filePath, const QString& title = QString(),
                            QWidget* parent = nullptr);
    ~PlaybackWindow() override;

private slots:
    void onNextFrame();
    void onPlayPauseClicked();
    void onStopClicked();
    void onSliderPressed();
    void onSliderReleased();

private:
    bool openFile(const QString& filePath);
    void showFrame(const cv::Mat& frame);
    void updateTimeLabel(int ms);

    cv::VideoCapture m_cap;
    QTimer* m_timer = nullptr;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_videoLabel = nullptr;
    QSlider* m_slider = nullptr;
    QLabel* m_timeLabel = nullptr;
    QPushButton* m_playButton = nullptr;
    QPushButton* m_stopButton = nullptr;

    int m_durationMs = 0;
    int m_fps = 30;
    bool m_playing = false;
    bool m_seeking = false;
};

#endif // PLAYBACKWINDOW_H