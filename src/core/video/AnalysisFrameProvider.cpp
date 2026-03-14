#include "core/video/AnalysisFrameProvider.h"

#include <opencv2/imgproc.hpp>

cv::Mat AnalysisFrameProvider::grayscaleFrame(const VideoFrame& frame) const
{
    cv::Mat grayscale;
    if (!frame.cpuBgr.empty())
    {
        cv::cvtColor(frame.cpuBgr, grayscale, cv::COLOR_BGR2GRAY);
    }

    return grayscale;
}
