#pragma once

#include <opencv2/videoio.hpp>

#include "VideoDecoder.h"

class OpenCvVideoDecoder final : public VideoDecoder
{
public:
    bool open(const std::string& filePath) override;
    bool isOpen() const override;
    bool seekFrame(int frameIndex) override;
    bool seekTimestampSeconds(double timestampSeconds) override;
    std::optional<VideoFrame> readFrame() override;
    int frameCount() const override;
    double fps() const override;
    cv::Size frameSize() const override;
    void setOutputScale(double scale) override;
    [[nodiscard]] double outputScale() const override;
    [[nodiscard]] QString backendName() const override;
    [[nodiscard]] bool isHardwareAccelerated() const override;

private:
    cv::VideoCapture m_capture;
    double m_outputScale = 1.0;
};
