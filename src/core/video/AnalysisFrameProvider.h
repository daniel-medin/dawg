#pragma once

#include <opencv2/core/mat.hpp>

#include "core/video/VideoFrame.h"

class AnalysisFrameProvider
{
public:
    [[nodiscard]] cv::Mat grayscaleFrame(const VideoFrame& frame) const;
};
