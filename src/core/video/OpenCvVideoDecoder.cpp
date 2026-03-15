#include "core/video/OpenCvVideoDecoder.h"

#include <algorithm>
#include <cmath>

#include <QImage>

#include <opencv2/imgproc.hpp>

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

bool OpenCvVideoDecoder::seekTimestampSeconds(const double timestampSeconds)
{
    if (!isOpen())
    {
        return false;
    }

    return m_capture.set(cv::CAP_PROP_POS_MSEC, std::max(0.0, timestampSeconds) * 1000.0);
}

std::optional<VideoFrame> OpenCvVideoDecoder::readFrame()
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
    const auto timestampMs = m_capture.get(cv::CAP_PROP_POS_MSEC);
    const auto derivedTimestampSeconds =
        fps() > 0.0 ? static_cast<double>(std::max(0, nextPosition - 1)) / fps() : 0.0;
    const auto timestampSeconds = timestampMs >= 0.0 ? (timestampMs / 1000.0) : derivedTimestampSeconds;
    const auto durationSeconds = fps() > 0.0 ? (1.0 / fps()) : 0.0;

    cv::Mat outputFrame = frame;
    if (m_outputScale < 0.999)
    {
        cv::resize(
            frame,
            outputFrame,
            cv::Size{
                std::max(1, static_cast<int>(std::lround(frame.cols * m_outputScale))),
                std::max(1, static_cast<int>(std::lround(frame.rows * m_outputScale)))
            },
            0.0,
            0.0,
            cv::INTER_AREA);
    }

    cv::Mat rgbFrame;
    cv::cvtColor(outputFrame, rgbFrame, cv::COLOR_BGR2RGB);
    QImage image(
        rgbFrame.data,
        rgbFrame.cols,
        rgbFrame.rows,
        static_cast<int>(rgbFrame.step[0]),
        QImage::Format_RGB888);

    return VideoFrame{
        .index = std::max(0, nextPosition - 1),
        .timestampSeconds = timestampSeconds,
        .durationSeconds = durationSeconds,
        .frameSize = cv::Size{frame.cols, frame.rows},
        .pixelFormatName = QStringLiteral("BGR24"),
        .nativeHandle = 0,
        .nativeSubresourceIndex = 0,
        .rotationDegrees = 0,
        .hardwareBacked = false,
        .cpuBgr = frame,
        .cpuImage = image.copy()
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

void OpenCvVideoDecoder::setCpuFrameExtractionEnabled(const bool enabled)
{
    Q_UNUSED(enabled);
}

bool OpenCvVideoDecoder::cpuFrameExtractionEnabled() const
{
    return true;
}

void OpenCvVideoDecoder::setOutputScale(const double scale)
{
    m_outputScale = std::clamp(scale, 0.1, 1.0);
}

double OpenCvVideoDecoder::outputScale() const
{
    return m_outputScale;
}

QString OpenCvVideoDecoder::backendName() const
{
    return QStringLiteral("OpenCV VideoCapture");
}

bool OpenCvVideoDecoder::isHardwareAccelerated() const
{
    return false;
}
