#pragma once

#include <cstdint>

#include <QImage>
#include <QString>

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

struct VideoFrame
{
    int index = -1;
    double timestampSeconds = 0.0;
    double durationSeconds = 0.0;
    cv::Size frameSize;
    QString pixelFormatName;
    std::uintptr_t nativeHandle = 0;
    bool hardwareBacked = false;
    cv::Mat cpuBgr;
    QImage cpuImage;

    [[nodiscard]] bool isValid() const
    {
        return index >= 0 && !cpuBgr.empty() && !cpuImage.isNull();
    }
};
