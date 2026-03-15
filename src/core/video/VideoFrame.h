#pragma once

#include <cstdint>
#include <memory>

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
    std::uint32_t nativeSubresourceIndex = 0;
    int rotationDegrees = 0;
    bool hardwareBacked = false;
    std::shared_ptr<void> nativeResource;
    cv::Mat cpuBgr;
    QImage cpuImage;

    [[nodiscard]] bool hasCpuImage() const
    {
        return !cpuImage.isNull();
    }

    [[nodiscard]] bool hasNativeTexture() const
    {
        return hardwareBacked && nativeHandle != 0 && nativeResource;
    }

    [[nodiscard]] bool isValid() const
    {
        return index >= 0 && (hasCpuImage() || hasNativeTexture());
    }
};
