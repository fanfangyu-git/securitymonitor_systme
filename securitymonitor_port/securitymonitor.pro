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
    core/ThreadPool.cpp \
    core/FrameBuffer.cpp \
    core/VideoSource.cpp \
    core/CameraSource.cpp \
    core/FileSource.cpp \
    core/SyntheticSource.cpp \
    service/DatabaseManager.cpp

HEADERS += \
    mainwindow.h \
    core/ThreadPool.h \
    core/FrameBuffer.h \
    core/VideoSource.h \
    core/CameraSource.h \
    core/FileSource.h \
    core/SyntheticSource.h \
    service/DatabaseManager.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target