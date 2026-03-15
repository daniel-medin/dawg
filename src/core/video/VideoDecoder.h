#pragma once

#include <optional>
#include <string>

#include <QString>
#include <opencv2/core/types.hpp>

#include "core/video/VideoFrame.h"

class VideoDecoder
{
public:
    virtual ~VideoDecoder() = default;

    virtual bool open(const std::string& filePath) = 0;
    virtual bool isOpen() const = 0;
    virtual bool seekFrame(int frameIndex) = 0;
    virtual bool seekTimestampSeconds(double timestampSeconds) = 0;
    virtual std::optional<VideoFrame> readFrame() = 0;
    virtual int frameCount() const = 0;
    virtual double fps() const = 0;
    virtual cv::Size frameSize() const = 0;
    virtual void setCpuFrameExtractionEnabled(bool enabled) = 0;
    [[nodiscard]] virtual bool cpuFrameExtractionEnabled() const = 0;
    virtual void setOutputScale(double scale) = 0;
    [[nodiscard]] virtual double outputScale() const = 0;
    [[nodiscard]] virtual QString backendName() const = 0;
    [[nodiscard]] virtual bool isHardwareAccelerated() const = 0;
};
