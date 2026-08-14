#ifndef IMAGECONVERT_H
#define IMAGECONVERT_H

#include <QImage>

#include <opencv2/imgproc.hpp>

// cv::Mat -> QImage 转换（深拷贝），支持灰度/3通道/4通道
inline QImage matToQImage(const cv::Mat& mat)
{
    if (mat.empty()) return QImage();

    switch (mat.channels()) {
    case 1:
        return QImage(mat.data, mat.cols, mat.rows,
                      static_cast<int>(mat.step), QImage::Format_Grayscale8).copy();
    case 3: {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        return QImage(rgb.data, rgb.cols, rgb.rows,
                      static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
    }
    case 4: {
        cv::Mat rgba;
        cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
        return QImage(rgba.data, rgba.cols, rgba.rows,
                      static_cast<int>(rgba.step), QImage::Format_RGBA8888).copy();
    }
    default:
        return QImage();
    }
}

#endif // IMAGECONVERT_H