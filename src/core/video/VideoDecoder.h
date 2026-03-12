#pragma once

#include <optional>
#include <string>

#include <opencv2/core/types.hpp>

#include "DecodedFrame.h"

class VideoDecoder
{
public:
    virtual ~VideoDecoder() = default;

    virtual bool open(const std::string& filePath) = 0;
    virtual bool isOpen() const = 0;
    virtual bool seekFrame(int frameIndex) = 0;
    virtual std::optional<DecodedFrame> readFrame() = 0;
    virtual int frameCount() const = 0;
    virtual double fps() const = 0;
    virtual cv::Size frameSize() const = 0;
};

