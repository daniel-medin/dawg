#include "core/video/AnalysisFrameProvider.h"

#include <opencv2/imgproc.hpp>

cv::Mat AnalysisFrameProvider::grayscaleFrame(const VideoFrame& frame) const
{
    cv::Mat grayscale;
    if (!frame.cpuBgr.empty())
    {
        cv::cvtColor(frame.cpuBgr, grayscale, cv::COLOR_BGR2GRAY);
    }
    else if (!frame.cpuImage.isNull())
    {
        const auto convertedImage = frame.cpuImage.convertToFormat(QImage::Format_ARGB32);
        cv::Mat bgraFrame(
            convertedImage.height(),
            convertedImage.width(),
            CV_8UC4,
            const_cast<uchar*>(convertedImage.constBits()),
            convertedImage.bytesPerLine());
        cv::cvtColor(bgraFrame, grayscale, cv::COLOR_BGRA2GRAY);
    }

    return grayscale;
}
