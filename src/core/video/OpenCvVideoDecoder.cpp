#include "core/video/OpenCvVideoDecoder.h"

#include <algorithm>

bool OpenCvVideoDecoder::open(const std::string& filePath)
{
    m_capture.release();
    return m_capture.open(filePath);
}

bool OpenCvVideoDecoder::isOpen() const
{
    return m_capture.isOpened();
}

bool OpenCvVideoDecoder::seekFrame(const int frameIndex)
{
    if (!isOpen())
    {
        return false;
    }

    return m_capture.set(cv::CAP_PROP_POS_FRAMES, frameIndex);
}

std::optional<DecodedFrame> OpenCvVideoDecoder::readFrame()
{
    if (!isOpen())
    {
        return std::nullopt;
    }

    cv::Mat frame;
    if (!m_capture.read(frame))
    {
        return std::nullopt;
    }

    const auto nextPosition = static_cast<int>(m_capture.get(cv::CAP_PROP_POS_FRAMES));

    return DecodedFrame{
        .index = std::max(0, nextPosition - 1),
        .timestampSeconds = fps() > 0.0 ? static_cast<double>(std::max(0, nextPosition - 1)) / fps() : 0.0,
        .bgr = frame
    };
}

int OpenCvVideoDecoder::frameCount() const
{
    return isOpen() ? static_cast<int>(m_capture.get(cv::CAP_PROP_FRAME_COUNT)) : 0;
}

double OpenCvVideoDecoder::fps() const
{
    if (!isOpen())
    {
        return 0.0;
    }

    const auto detectedFps = m_capture.get(cv::CAP_PROP_FPS);
    return detectedFps > 0.0 ? detectedFps : 30.0;
}

cv::Size OpenCvVideoDecoder::frameSize() const
{
    if (!isOpen())
    {
        return {};
    }

    return cv::Size{
        static_cast<int>(m_capture.get(cv::CAP_PROP_FRAME_WIDTH)),
        static_cast<int>(m_capture.get(cv::CAP_PROP_FRAME_HEIGHT))
    };
}

