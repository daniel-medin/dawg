#include "core/video/FfmpegVideoDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include <QImage>

#include <opencv2/imgproc.hpp>

#if DAWG_HAS_FFMPEG
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

namespace
{
#if DAWG_HAS_FFMPEG
double rationalToDouble(const AVRational rational)
{
    return rational.num != 0 && rational.den != 0 ? av_q2d(rational) : 0.0;
}

int64_t secondsToStreamTimestamp(const double seconds, const AVRational timeBase)
{
    return static_cast<int64_t>(std::llround(seconds / rationalToDouble(timeBase)));
}

int normalizeRotationDegrees(const double rotationDegrees)
{
    auto normalized = static_cast<int>(std::lround(rotationDegrees)) % 360;
    if (normalized < 0)
    {
        normalized += 360;
    }

    const auto roundedToRightAngle = static_cast<int>(std::lround(normalized / 90.0)) * 90;
    return roundedToRightAngle % 360;
}

int rotationDegreesForStream(const AVStream* stream)
{
    if (!stream || !stream->codecpar)
    {
        return 0;
    }

    if (const auto* sideData = av_packet_side_data_get(
            stream->codecpar->coded_side_data,
            stream->codecpar->nb_coded_side_data,
            AV_PKT_DATA_DISPLAYMATRIX))
    {
        if (sideData->size >= static_cast<int>(sizeof(std::int32_t) * 9))
        {
            const auto rotationDegrees = -av_display_rotation_get(reinterpret_cast<const std::int32_t*>(sideData->data));
            if (!std::isnan(rotationDegrees))
            {
                return normalizeRotationDegrees(rotationDegrees);
            }
        }
    }

    if (const auto* rotateTag = av_dict_get(stream->metadata, "rotate", nullptr, 0))
    {
        bool ok = false;
        const auto metadataRotation = QString::fromUtf8(rotateTag->value).toDouble(&ok);
        if (ok)
        {
            return normalizeRotationDegrees(metadataRotation);
        }
    }

    return 0;
}

cv::Mat rotateFrameBgr(const cv::Mat& inputFrame, const int rotationDegrees)
{
    if (inputFrame.empty() || rotationDegrees == 0)
    {
        return inputFrame;
    }

    cv::Mat rotatedFrame;
    switch (rotationDegrees)
    {
    case 90:
        cv::rotate(inputFrame, rotatedFrame, cv::ROTATE_90_CLOCKWISE);
        break;
    case 180:
        cv::rotate(inputFrame, rotatedFrame, cv::ROTATE_180);
        break;
    case 270:
        cv::rotate(inputFrame, rotatedFrame, cv::ROTATE_90_COUNTERCLOCKWISE);
        break;
    default:
        return inputFrame;
    }

    return rotatedFrame;
}

cv::Mat scaledFrameBgr(const cv::Mat& inputFrame, const double outputScale)
{
    if (inputFrame.empty() || outputScale >= 0.999)
    {
        return inputFrame;
    }

    const auto scaledWidth = std::max(1, static_cast<int>(std::lround(inputFrame.cols * outputScale)));
    const auto scaledHeight = std::max(1, static_cast<int>(std::lround(inputFrame.rows * outputScale)));
    cv::Mat scaledFrame;
    cv::resize(inputFrame, scaledFrame, cv::Size{scaledWidth, scaledHeight}, 0.0, 0.0, cv::INTER_AREA);
    return scaledFrame;
}

struct HardwareFormatSelectionContext
{
    AVPixelFormat hardwarePixelFormat = AV_PIX_FMT_NONE;
};

enum AVPixelFormat selectHardwarePixelFormat(AVCodecContext* codecContext, const enum AVPixelFormat* pixelFormats)
{
    if (!codecContext || !pixelFormats)
    {
        return AV_PIX_FMT_NONE;
    }

    const auto* selectionContext = static_cast<const HardwareFormatSelectionContext*>(codecContext->opaque);
    if (selectionContext)
    {
        for (auto* pixelFormat = pixelFormats; *pixelFormat != AV_PIX_FMT_NONE; ++pixelFormat)
        {
            if (*pixelFormat == selectionContext->hardwarePixelFormat)
            {
                return *pixelFormat;
            }
        }
    }

    return pixelFormats[0];
}
#endif
}

struct FfmpegVideoDecoder::Impl
{
#if DAWG_HAS_FFMPEG
    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
    const AVCodec* codec = nullptr;
    AVFrame* decodedFrame = nullptr;
    AVFrame* transferFrame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* swsContext = nullptr;
    AVBufferRef* hardwareDeviceContext = nullptr;
    int videoStreamIndex = -1;
    AVRational streamTimeBase{0, 1};
    double detectedFps = 0.0;
    int detectedFrameCount = 0;
    cv::Size detectedFrameSize;
    bool endOfStream = false;
    int pendingSeekFrameIndex = -1;
    int lastReturnedFrameIndex = -1;
    AVPixelFormat hardwarePixelFormat = AV_PIX_FMT_NONE;
    bool hardwareDecodeEnabled = false;
    QString activeBackendName = QStringLiteral("FFmpeg");
    HardwareFormatSelectionContext hardwareFormatSelection;
    int presentationRotationDegrees = 0;
    double outputScale = 1.0;

    ~Impl()
    {
        reset();
    }

    void reset()
    {
        if (swsContext)
        {
            sws_freeContext(swsContext);
            swsContext = nullptr;
        }

        if (packet)
        {
            av_packet_free(&packet);
        }

        if (transferFrame)
        {
            av_frame_free(&transferFrame);
        }

        if (decodedFrame)
        {
            av_frame_free(&decodedFrame);
        }

        if (hardwareDeviceContext)
        {
            av_buffer_unref(&hardwareDeviceContext);
        }

        if (codecContext)
        {
            avcodec_free_context(&codecContext);
        }

        if (formatContext)
        {
            avformat_close_input(&formatContext);
        }

        videoStreamIndex = -1;
        streamTimeBase = AVRational{0, 1};
        detectedFps = 0.0;
        detectedFrameCount = 0;
        detectedFrameSize = {};
        endOfStream = false;
        pendingSeekFrameIndex = -1;
        lastReturnedFrameIndex = -1;
        hardwarePixelFormat = AV_PIX_FMT_NONE;
        hardwareDecodeEnabled = false;
        activeBackendName = QStringLiteral("FFmpeg");
        hardwareFormatSelection = {};
        presentationRotationDegrees = 0;
    }
#endif
};

FfmpegVideoDecoder::FfmpegVideoDecoder()
    : m_impl(std::make_unique<Impl>())
{
}

FfmpegVideoDecoder::~FfmpegVideoDecoder() = default;

bool FfmpegVideoDecoder::open(const std::string& filePath)
{
#if !DAWG_HAS_FFMPEG
    Q_UNUSED(filePath);
    return false;
#else
    m_impl->reset();

    if (avformat_open_input(&m_impl->formatContext, filePath.c_str(), nullptr, nullptr) < 0)
    {
        return false;
    }

    if (avformat_find_stream_info(m_impl->formatContext, nullptr) < 0)
    {
        m_impl->reset();
        return false;
    }

    m_impl->videoStreamIndex = av_find_best_stream(m_impl->formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_impl->videoStreamIndex < 0)
    {
        m_impl->reset();
        return false;
    }

    auto* stream = m_impl->formatContext->streams[m_impl->videoStreamIndex];
    m_impl->codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!m_impl->codec)
    {
        m_impl->reset();
        return false;
    }

    for (int configIndex = 0;; ++configIndex)
    {
        const auto* hardwareConfig = avcodec_get_hw_config(m_impl->codec, configIndex);
        if (!hardwareConfig)
        {
            break;
        }

        if ((hardwareConfig->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) == 0
            || hardwareConfig->device_type != AV_HWDEVICE_TYPE_D3D11VA)
        {
            continue;
        }

        m_impl->hardwarePixelFormat = hardwareConfig->pix_fmt;
        if (av_hwdevice_ctx_create(
                &m_impl->hardwareDeviceContext,
                AV_HWDEVICE_TYPE_D3D11VA,
                nullptr,
                nullptr,
                0) == 0)
        {
            m_impl->hardwareDecodeEnabled = true;
            m_impl->activeBackendName = QStringLiteral("FFmpeg D3D11VA");
        }
        else
        {
            m_impl->hardwarePixelFormat = AV_PIX_FMT_NONE;
        }
        break;
    }

    m_impl->codecContext = avcodec_alloc_context3(m_impl->codec);
    if (!m_impl->codecContext)
    {
        m_impl->reset();
        return false;
    }

    m_impl->hardwareFormatSelection.hardwarePixelFormat = m_impl->hardwarePixelFormat;
    m_impl->codecContext->opaque = &m_impl->hardwareFormatSelection;
    if (m_impl->hardwareDecodeEnabled)
    {
        m_impl->codecContext->get_format = &selectHardwarePixelFormat;
        m_impl->codecContext->hw_device_ctx = av_buffer_ref(m_impl->hardwareDeviceContext);
    }

    if (avcodec_parameters_to_context(m_impl->codecContext, stream->codecpar) < 0
        || avcodec_open2(m_impl->codecContext, m_impl->codec, nullptr) < 0)
    {
        m_impl->reset();
        return false;
    }

    m_impl->decodedFrame = av_frame_alloc();
    m_impl->transferFrame = av_frame_alloc();
    m_impl->packet = av_packet_alloc();
    if (!m_impl->decodedFrame || !m_impl->transferFrame || !m_impl->packet)
    {
        m_impl->reset();
        return false;
    }

    m_impl->streamTimeBase = stream->time_base;
    m_impl->presentationRotationDegrees = rotationDegreesForStream(stream);
    m_impl->detectedFps = rationalToDouble(stream->avg_frame_rate);
    if (m_impl->detectedFps <= 0.0)
    {
        m_impl->detectedFps = rationalToDouble(stream->r_frame_rate);
    }
    if (m_impl->detectedFps <= 0.0)
    {
        m_impl->detectedFps = 30.0;
    }

    m_impl->detectedFrameCount = stream->nb_frames > 0
        ? static_cast<int>(stream->nb_frames)
        : (m_impl->formatContext->duration > 0
            ? static_cast<int>(std::llround((m_impl->formatContext->duration / static_cast<double>(AV_TIME_BASE)) * m_impl->detectedFps))
            : 0);
    if (m_impl->presentationRotationDegrees == 90 || m_impl->presentationRotationDegrees == 270)
    {
        m_impl->detectedFrameSize = cv::Size{
            m_impl->codecContext->height,
            m_impl->codecContext->width
        };
    }
    else
    {
        m_impl->detectedFrameSize = cv::Size{
            m_impl->codecContext->width,
            m_impl->codecContext->height
        };
    }

    return true;
#endif
}

bool FfmpegVideoDecoder::isOpen() const
{
#if !DAWG_HAS_FFMPEG
    return false;
#else
    return m_impl && m_impl->formatContext && m_impl->codecContext && m_impl->videoStreamIndex >= 0;
#endif
}

bool FfmpegVideoDecoder::seekFrame(const int frameIndex)
{
    if (!isOpen())
    {
        return false;
    }

    const auto safeFps = fps() > 0.0 ? fps() : 30.0;
    return seekTimestampSeconds(std::max(0, frameIndex) / safeFps);
}

bool FfmpegVideoDecoder::seekTimestampSeconds(const double timestampSeconds)
{
#if !DAWG_HAS_FFMPEG
    Q_UNUSED(timestampSeconds);
    return false;
#else
    if (!isOpen())
    {
        return false;
    }

    auto* stream = m_impl->formatContext->streams[m_impl->videoStreamIndex];
    const auto seekTarget = secondsToStreamTimestamp(std::max(0.0, timestampSeconds), stream->time_base);
    if (av_seek_frame(m_impl->formatContext, m_impl->videoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD) < 0)
    {
        return false;
    }

    avcodec_flush_buffers(m_impl->codecContext);
    m_impl->endOfStream = false;
    m_impl->pendingSeekFrameIndex = std::max(0, static_cast<int>(std::floor(timestampSeconds * fps())));
    m_impl->lastReturnedFrameIndex = m_impl->pendingSeekFrameIndex - 1;
    return true;
#endif
}

std::optional<VideoFrame> FfmpegVideoDecoder::readFrame()
{
#if !DAWG_HAS_FFMPEG
    return std::nullopt;
#else
    if (!isOpen())
    {
        return std::nullopt;
    }

    auto receiveDecodedFrame = [this]() -> std::optional<VideoFrame>
    {
        while (avcodec_receive_frame(m_impl->codecContext, m_impl->decodedFrame) == 0)
        {
            const auto pts = m_impl->decodedFrame->best_effort_timestamp != AV_NOPTS_VALUE
                ? m_impl->decodedFrame->best_effort_timestamp
                : m_impl->decodedFrame->pts;
            const auto timestampSeconds = pts != AV_NOPTS_VALUE
                ? (pts * rationalToDouble(m_impl->streamTimeBase))
                : (m_impl->lastReturnedFrameIndex + 1) / std::max(1.0, fps());
            const auto derivedIndex = fps() > 0.0
                ? std::max(0, static_cast<int>(std::llround(timestampSeconds * fps())))
                : std::max(0, m_impl->lastReturnedFrameIndex + 1);

            if (m_impl->pendingSeekFrameIndex >= 0 && derivedIndex < m_impl->pendingSeekFrameIndex)
            {
                av_frame_unref(m_impl->decodedFrame);
                continue;
            }

            const auto frameIndex = std::max(derivedIndex, m_impl->lastReturnedFrameIndex + 1);
            const auto durationSeconds = m_impl->decodedFrame->duration > 0
                ? (m_impl->decodedFrame->duration * rationalToDouble(m_impl->streamTimeBase))
                : (fps() > 0.0 ? 1.0 / fps() : 0.0);

            auto* sourceFrame = m_impl->decodedFrame;
            std::uintptr_t nativeHandle = 0;
            bool hardwareBacked = false;
            if (m_impl->hardwareDecodeEnabled && m_impl->decodedFrame->format == m_impl->hardwarePixelFormat)
            {
                av_frame_unref(m_impl->transferFrame);
                if (av_hwframe_transfer_data(m_impl->transferFrame, m_impl->decodedFrame, 0) < 0)
                {
                    av_frame_unref(m_impl->decodedFrame);
                    return std::nullopt;
                }

                sourceFrame = m_impl->transferFrame;
                nativeHandle = reinterpret_cast<std::uintptr_t>(m_impl->decodedFrame->data[0]);
                hardwareBacked = true;
            }

            if (!m_impl->swsContext)
            {
                m_impl->swsContext = sws_getCachedContext(
                    m_impl->swsContext,
                    sourceFrame->width,
                    sourceFrame->height,
                    static_cast<AVPixelFormat>(sourceFrame->format),
                    sourceFrame->width,
                    sourceFrame->height,
                    AV_PIX_FMT_BGR24,
                    SWS_BILINEAR,
                    nullptr,
                    nullptr,
                    nullptr);
            }

            if (!m_impl->swsContext)
            {
                av_frame_unref(m_impl->decodedFrame);
                return std::nullopt;
            }

            cv::Mat cpuBgr(sourceFrame->height, sourceFrame->width, CV_8UC3);
            uint8_t* dstData[4] = {cpuBgr.data, nullptr, nullptr, nullptr};
            int dstLinesize[4] = {static_cast<int>(cpuBgr.step[0]), 0, 0, 0};
            sws_scale(
                m_impl->swsContext,
                sourceFrame->data,
                sourceFrame->linesize,
                0,
                sourceFrame->height,
                dstData,
                dstLinesize);

            const auto presentedBgr = rotateFrameBgr(cpuBgr, m_impl->presentationRotationDegrees);
            const auto outputBgr = scaledFrameBgr(presentedBgr, m_impl->outputScale);
            QImage cpuImage(
                outputBgr.data,
                outputBgr.cols,
                outputBgr.rows,
                static_cast<int>(outputBgr.step[0]),
                QImage::Format_BGR888);

            VideoFrame frame;
            frame.index = frameIndex;
            frame.timestampSeconds = timestampSeconds;
            frame.durationSeconds = durationSeconds;
            frame.frameSize = cv::Size{presentedBgr.cols, presentedBgr.rows};
            frame.pixelFormatName = QStringLiteral("BGR24");
            frame.nativeHandle = nativeHandle;
            frame.hardwareBacked = hardwareBacked;
            frame.cpuBgr = presentedBgr;
            frame.cpuImage = cpuImage.copy();

            m_impl->pendingSeekFrameIndex = -1;
            m_impl->lastReturnedFrameIndex = frameIndex;
            av_frame_unref(m_impl->decodedFrame);
            return frame;
        }

        return std::nullopt;
    };

    while (true)
    {
        if (const auto decoded = receiveDecodedFrame(); decoded.has_value())
        {
            return decoded;
        }

        if (m_impl->endOfStream)
        {
            return std::nullopt;
        }

        const auto readResult = av_read_frame(m_impl->formatContext, m_impl->packet);
        if (readResult < 0)
        {
            m_impl->endOfStream = true;
            avcodec_send_packet(m_impl->codecContext, nullptr);
            av_packet_unref(m_impl->packet);
            continue;
        }

        if (m_impl->packet->stream_index != m_impl->videoStreamIndex)
        {
            av_packet_unref(m_impl->packet);
            continue;
        }

        if (avcodec_send_packet(m_impl->codecContext, m_impl->packet) < 0)
        {
            av_packet_unref(m_impl->packet);
            return std::nullopt;
        }

        av_packet_unref(m_impl->packet);
    }
#endif
}

int FfmpegVideoDecoder::frameCount() const
{
    return isOpen() ? m_impl->detectedFrameCount : 0;
}

double FfmpegVideoDecoder::fps() const
{
    return isOpen() && m_impl->detectedFps > 0.0 ? m_impl->detectedFps : 0.0;
}

cv::Size FfmpegVideoDecoder::frameSize() const
{
    return isOpen() ? m_impl->detectedFrameSize : cv::Size{};
}

void FfmpegVideoDecoder::setOutputScale(const double scale)
{
    if (!m_impl)
    {
        return;
    }

    m_impl->outputScale = std::clamp(scale, 0.1, 1.0);
}

double FfmpegVideoDecoder::outputScale() const
{
    return m_impl ? m_impl->outputScale : 1.0;
}

QString FfmpegVideoDecoder::backendName() const
{
    return isOpen() ? m_impl->activeBackendName : QStringLiteral("FFmpeg");
}

bool FfmpegVideoDecoder::isHardwareAccelerated() const
{
    return isOpen() && m_impl->hardwareDecodeEnabled;
}
