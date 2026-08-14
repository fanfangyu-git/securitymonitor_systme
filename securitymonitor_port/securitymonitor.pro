QT       += core gui sql

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET   = securitymonitor
TEMPLATE = app

CONFIG  += c++11
DEFINES += QT_DEPRECATED_WARNINGS

# 项目内目录
INCLUDEPATH += $$PWD $$PWD/core $$PWD/service

# OpenCV
INCLUDEPATH += D:/openCV4.5.5/include
LIBS += -LD:/openCV4.5.5/x64/mingw/bin -lopencv_world455

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    AdminLoginDialog.cpp \
    PlaybackWindow.cpp \
    HistoryDialog.cpp \
    core/ThreadPool.cpp \
    core/FrameBuffer.cpp \
    core/VideoSource.cpp \
    core/CameraSource.cpp \
    core/FileSource.cpp \
    core/SyntheticSource.cpp \
    core/PreviewChannel.cpp \
    core/MotionDetector.cpp \
    core/VideoRecorder.cpp \
    core/StorageManager.cpp \
    core/FaceAnalyzer.cpp \
    service/DatabaseManager.cpp

HEADERS += \
    mainwindow.h \
    AdminLoginDialog.h \
    PlaybackWindow.h \
    HistoryDialog.h \
    core/ThreadPool.h \
    core/FrameBuffer.h \
    core/VideoSource.h \
    core/CameraSource.h \
    core/FileSource.h \
    core/SyntheticSource.h \
    core/PreviewChannel.h \
    core/ImageConvert.h \
    core/MotionDetector.h \
    core/VideoRecorder.h \
    core/StorageManager.h \
    core/FaceAnalyzer.h \
    service/DatabaseManager.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# ---- 构建后自动复制运行库到可执行目录（防止 Clean 重编后缺 DLL 无法启动）----
win32 {
    OPENCV_BIN = D:/openCV4.5.5/x64/mingw/bin
    MINGW_BIN  = D:/Qt/Qt5.14.2/Tools/mingw730_64/bin
    QMAKE_POST_LINK += cmd /c copy /Y $${OPENCV_BIN}/*.dll $${OUT_PWD}/
    QMAKE_POST_LINK += & cmd /c copy /Y $${MINGW_BIN}/libgcc_s_seh-1.dll $${OUT_PWD}/
    QMAKE_POST_LINK += & cmd /c copy /Y $${MINGW_BIN}/libstdc++-6.dll $${OUT_PWD}/
    QMAKE_POST_LINK += & cmd /c copy /Y $${MINGW_BIN}/libwinpthread-1.dll $${OUT_PWD}/
}