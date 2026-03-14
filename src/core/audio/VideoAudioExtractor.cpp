#include "core/audio/VideoAudioExtractor.h"

#include <cstdint>
#include <optional>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#if DAWG_HAS_FFMPEG
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
}
#endif

namespace
{
void writeLe16(QFile& file, const std::uint16_t value)
{
    char bytes[2];
    bytes[0] = static_cast<char>(value & 0xff);
    bytes[1] = static_cast<char>((value >> 8) & 0xff);
    file.write(bytes, 2);
}

void writeLe32(QFile& file, const std::uint32_t value)
{
    char bytes[4];
    bytes[0] = static_cast<char>(value & 0xff);
    bytes[1] = static_cast<char>((value >> 8) & 0xff);
    bytes[2] = static_cast<char>((value >> 16) & 0xff);
    bytes[3] = static_cast<char>((value >> 24) & 0xff);
    file.write(bytes, 4);
}

bool writeWaveHeader(
    QFile& file,
    const int sampleRate,
    const int channelCount,
    const int bitsPerSample,
    const std::uint32_t dataSizeBytes)
{
    if (!file.seek(0))
    {
        return false;
    }

    const auto bytesPerSample = static_cast<std::uint16_t>((channelCount * bitsPerSample) / 8);
    const auto byteRate = static_cast<std::uint32_t>(sampleRate * bytesPerSample);

    file.write("RIFF", 4);
    writeLe32(file, 36u + dataSizeBytes);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    writeLe32(file, 16);
    writeLe16(file, 1);
    writeLe16(file, static_cast<std::uint16_t>(channelCount));
    writeLe32(file, static_cast<std::uint32_t>(sampleRate));
    writeLe32(file, byteRate);
    writeLe16(file, bytesPerSample);
    writeLe16(file, static_cast<std::uint16_t>(bitsPerSample));
    file.write("data", 4);
    writeLe32(file, dataSizeBytes);
    return true;
}

QString cachedWavePathForVideo(const QString& videoFilePath)
{
    const QFileInfo fileInfo(videoFilePath);
    const auto cacheRoot =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/dawg/video-audio-cache");
    QDir().mkpath(cacheRoot);

    const auto fingerprintSource =
        fileInfo.absoluteFilePath().toUtf8() + QByteArray::number(fileInfo.lastModified().toMSecsSinceEpoch());
    const auto fingerprint = QCryptographicHash::hash(fingerprintSource, QCryptographicHash::Sha1).toHex();
    return cacheRoot + QStringLiteral("/") + QString::fromLatin1(fingerprint) + QStringLiteral(".wav");
}
}

namespace dawg::audio
{
std::optional<QString> extractEmbeddedAudioToWave(const QString& videoFilePath)
{
#if !DAWG_HAS_FFMPEG
    Q_UNUSED(videoFilePath);
    return std::nullopt;
#else
    if (videoFilePath.isEmpty())
    {
        return std::nullopt;
    }

    const auto cachePath = cachedWavePathForVideo(videoFilePath);
    if (QFileInfo::exists(cachePath))
    {
        return cachePath;
    }

    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
    SwrContext* resampler = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* decodedFrame = nullptr;
    int audioStreamIndex = -1;
    QFile outputFile(cachePath);
    AVChannelLayout outputLayout{};
    AVChannelLayout inputLayout{};

    const auto cleanup = [&]()
    {
        av_channel_layout_uninit(&outputLayout);
        av_channel_layout_uninit(&inputLayout);
        if (decodedFrame)
        {
            av_frame_free(&decodedFrame);
        }
        if (packet)
        {
            av_packet_free(&packet);
        }
        if (resampler)
        {
            swr_free(&resampler);
        }
        if (codecContext)
        {
            avcodec_free_context(&codecContext);
        }
        if (formatContext)
        {
            avformat_close_input(&formatContext);
        }
        if (outputFile.isOpen())
        {
            outputFile.close();
        }
    };

    if (avformat_open_input(&formatContext, videoFilePath.toUtf8().constData(), nullptr, nullptr) < 0)
    {
        cleanup();
        return std::nullopt;
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0)
    {
        cleanup();
        return std::nullopt;
    }

    audioStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioStreamIndex < 0)
    {
        cleanup();
        return std::nullopt;
    }

    auto* stream = formatContext->streams[audioStreamIndex];
    const auto* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder)
    {
        cleanup();
        return std::nullopt;
    }

    codecContext = avcodec_alloc_context3(decoder);
    if (!codecContext
        || avcodec_parameters_to_context(codecContext, stream->codecpar) < 0
        || avcodec_open2(codecContext, decoder, nullptr) < 0)
    {
        cleanup();
        return std::nullopt;
    }

    packet = av_packet_alloc();
    decodedFrame = av_frame_alloc();
    if (!packet || !decodedFrame)
    {
        cleanup();
        return std::nullopt;
    }

    const auto outputSampleRate = codecContext->sample_rate > 0 ? codecContext->sample_rate : 48000;
    av_channel_layout_default(&outputLayout, 2);
    if (codecContext->ch_layout.nb_channels > 0)
    {
        av_channel_layout_copy(&inputLayout, &codecContext->ch_layout);
    }
    else
    {
        av_channel_layout_default(&inputLayout, std::max(1, codecContext->ch_layout.nb_channels));
    }

    if (swr_alloc_set_opts2(
            &resampler,
            &outputLayout,
            AV_SAMPLE_FMT_S16,
            outputSampleRate,
            &inputLayout,
            codecContext->sample_fmt,
            codecContext->sample_rate > 0 ? codecContext->sample_rate : outputSampleRate,
            0,
            nullptr) < 0
        || !resampler
        || swr_init(resampler) < 0)
    {
        cleanup();
        return std::nullopt;
    }

    if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        cleanup();
        return std::nullopt;
    }

    if (!writeWaveHeader(outputFile, outputSampleRate, outputLayout.nb_channels, 16, 0))
    {
        cleanup();
        return std::nullopt;
    }

    std::uint32_t totalDataBytes = 0;
    auto writeDecodedFrames = [&]() -> bool
    {
        while (true)
        {
            const auto receiveResult = avcodec_receive_frame(codecContext, decodedFrame);
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF)
            {
                return true;
            }

            if (receiveResult < 0)
            {
                return false;
            }

            const auto outputSamples = swr_get_out_samples(resampler, decodedFrame->nb_samples);
            if (outputSamples <= 0)
            {
                av_frame_unref(decodedFrame);
                return true;
            }

            const auto bufferSize = av_samples_get_buffer_size(
                nullptr,
                outputLayout.nb_channels,
                outputSamples,
                AV_SAMPLE_FMT_S16,
                0);
            if (bufferSize <= 0)
            {
                av_frame_unref(decodedFrame);
                return false;
            }

            QByteArray pcmBuffer;
            pcmBuffer.resize(bufferSize);
            uint8_t* outputData[] = {
                reinterpret_cast<uint8_t*>(pcmBuffer.data()),
                nullptr
            };

            const auto convertedSamples = swr_convert(
                resampler,
                outputData,
                outputSamples,
                const_cast<const uint8_t**>(decodedFrame->extended_data),
                decodedFrame->nb_samples);
            av_frame_unref(decodedFrame);

            if (convertedSamples < 0)
            {
                return false;
            }

            const auto convertedBytes = av_samples_get_buffer_size(
                nullptr,
                outputLayout.nb_channels,
                convertedSamples,
                AV_SAMPLE_FMT_S16,
                0);
            if (convertedBytes <= 0)
            {
                continue;
            }

            if (outputFile.write(pcmBuffer.constData(), convertedBytes) != convertedBytes)
            {
                return false;
            }

            totalDataBytes += static_cast<std::uint32_t>(convertedBytes);
        }
    };

    while (av_read_frame(formatContext, packet) >= 0)
    {
        if (packet->stream_index != audioStreamIndex)
        {
            av_packet_unref(packet);
            continue;
        }

        if (avcodec_send_packet(codecContext, packet) < 0)
        {
            av_packet_unref(packet);
            cleanup();
            QFile::remove(cachePath);
            return std::nullopt;
        }

        av_packet_unref(packet);
        if (!writeDecodedFrames())
        {
            cleanup();
            QFile::remove(cachePath);
            return std::nullopt;
        }
    }

    avcodec_send_packet(codecContext, nullptr);
    if (!writeDecodedFrames())
    {
        cleanup();
        QFile::remove(cachePath);
        return std::nullopt;
    }

    if (!writeWaveHeader(outputFile, outputSampleRate, outputLayout.nb_channels, 16, totalDataBytes))
    {
        cleanup();
        QFile::remove(cachePath);
        return std::nullopt;
    }

    cleanup();
    return totalDataBytes > 0 ? std::optional<QString>{cachePath} : std::nullopt;
#endif
}
}
