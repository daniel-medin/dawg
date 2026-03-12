#pragma once

#include <opencv2/videoio.hpp>

#include "VideoDecoder.h"

class OpenCvVideoDecoder final : public VideoDecoder
{
public:
    bool open(const std::string& filePath) override;
    bool isOpen() const override;
    bool seekFrame(int frameIndex) override;
    std::optional<DecodedFrame> readFrame() override;
    int frameCount() const override;
    double fps() const override;
    cv::Size frameSize() const override;

private:
    cv::VideoCapture m_capture;
};

