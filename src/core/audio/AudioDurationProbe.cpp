#include "core/audio/AudioDurationProbe.h"

#include <optional>

#include <QUuid>

#if DAWG_HAS_FFMPEG
extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}
#endif

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

namespace dawg::audio
{
std::optional<int> probeAudioDurationMs(const QString& filePath)
{
#if DAWG_HAS_FFMPEG
    AVFormatContext* formatContext = nullptr;
    const auto utf8Path = filePath.toUtf8();
    if (avformat_open_input(&formatContext, utf8Path.constData(), nullptr, nullptr) == 0)
    {
        const auto closeInput = [&formatContext]()
        {
            if (formatContext)
            {
                avformat_close_input(&formatContext);
            }
        };

        if (avformat_find_stream_info(formatContext, nullptr) >= 0)
        {
            if (formatContext->duration != AV_NOPTS_VALUE)
            {
                const auto duration = av_rescale(formatContext->duration, 1000, AV_TIME_BASE);
                closeInput();
                if (duration > 0)
                {
                    return static_cast<int>(duration);
                }
            }

            const auto audioStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            if (audioStreamIndex >= 0)
            {
                const auto* stream = formatContext->streams[audioStreamIndex];
                if (stream && stream->duration != AV_NOPTS_VALUE)
                {
                    const auto durationSeconds = stream->duration * av_q2d(stream->time_base);
                    closeInput();
                    if (durationSeconds > 0.0)
                    {
                        return static_cast<int>(durationSeconds * 1000.0);
                    }
                }
            }
        }

        closeInput();
    }
#endif

#ifdef Q_OS_WIN
    auto alias = QStringLiteral("dawg_probe_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    alias.replace(QLatin1Char('-'), QLatin1Char('_'));

    const auto openCommand = QStringLiteral("open \"%1\" alias %2").arg(filePath, alias);
    if (mciSendStringW(reinterpret_cast<LPCWSTR>(openCommand.utf16()), nullptr, 0, nullptr) != 0)
    {
        return std::nullopt;
    }

    const auto formatCommand = QStringLiteral("set %1 time format milliseconds").arg(alias);
    mciSendStringW(reinterpret_cast<LPCWSTR>(formatCommand.utf16()), nullptr, 0, nullptr);

    wchar_t lengthBuffer[64]{};
    const auto statusCommand = QStringLiteral("status %1 length").arg(alias);
    const auto statusResult =
        mciSendStringW(reinterpret_cast<LPCWSTR>(statusCommand.utf16()), lengthBuffer, 64, nullptr);

    const auto closeCommand = QStringLiteral("close %1").arg(alias);
    mciSendStringW(reinterpret_cast<LPCWSTR>(closeCommand.utf16()), nullptr, 0, nullptr);

    if (statusResult != 0)
    {
        return std::nullopt;
    }

    bool ok = false;
    const auto parsedLength = QString::fromWCharArray(lengthBuffer).toInt(&ok);
    return ok ? std::optional<int>{parsedLength} : std::nullopt;
#else
    Q_UNUSED(filePath);
    return std::nullopt;
#endif
}
}
