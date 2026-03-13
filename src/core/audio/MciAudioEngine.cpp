#include "core/audio/MciAudioEngine.h"

#include <algorithm>
#include <vector>

#include <QFileInfo>

#include "core/audio/AudioDurationProbe.h"

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

MciAudioEngine::MciAudioEngine(QObject* parent)
    : AudioEngine(parent)
{
}

QString MciAudioEngine::aliasForTrack(const QUuid& trackId) const
{
    auto alias = QStringLiteral("dawg_%1").arg(trackId.toString(QUuid::WithoutBraces));
    alias.replace(QLatin1Char('-'), QLatin1Char('_'));
    return alias;
}

std::optional<int> MciAudioEngine::durationMs(const QString& filePath) const
{
    return dawg::audio::probeAudioDurationMs(filePath);
}

bool MciAudioEngine::playTrack(const QUuid& trackId, const QString& filePath, const int offsetMs)
{
#ifdef Q_OS_WIN
    const auto clampedOffsetMs = std::max(0, offsetMs);
    const auto activeIt = m_activeTracks.constFind(trackId);
    if (activeIt != m_activeTracks.cend()
        && activeIt->filePath == filePath)
    {
        return true;
    }

    stopTrack(trackId);

    const auto alias = aliasForTrack(trackId);
    const auto openCommand = QStringLiteral("open \"%1\" alias %2").arg(filePath, alias);
    if (mciSendStringW(reinterpret_cast<LPCWSTR>(openCommand.utf16()), nullptr, 0, nullptr) != 0)
    {
        emit statusChanged(
            QStringLiteral("Failed to open %1 for playback.")
                .arg(QFileInfo(filePath).fileName()));
        return false;
    }

    const auto formatCommand = QStringLiteral("set %1 time format milliseconds").arg(alias);
    mciSendStringW(reinterpret_cast<LPCWSTR>(formatCommand.utf16()), nullptr, 0, nullptr);

    const auto playCommand = QStringLiteral("play %1 from %2").arg(alias).arg(clampedOffsetMs);
    if (mciSendStringW(reinterpret_cast<LPCWSTR>(playCommand.utf16()), nullptr, 0, nullptr) != 0)
    {
        const auto closeCommand = QStringLiteral("close %1").arg(alias);
        mciSendStringW(reinterpret_cast<LPCWSTR>(closeCommand.utf16()), nullptr, 0, nullptr);
        emit statusChanged(
            QStringLiteral("Failed to play %1.")
                .arg(QFileInfo(filePath).fileName()));
        return false;
    }

    m_activeTracks.insert(
        trackId,
        ActiveTrackPlayback{
            .alias = alias,
            .filePath = filePath,
            .offsetMs = clampedOffsetMs
        });
    return true;
#else
    Q_UNUSED(trackId);
    Q_UNUSED(offsetMs);
    Q_UNUSED(filePath);
    emit statusChanged(QStringLiteral("Sound playback is currently only implemented on Windows."));
    return false;
#endif
}

void MciAudioEngine::stopTrack(const QUuid& trackId)
{
#ifdef Q_OS_WIN
    const auto activeIt = m_activeTracks.find(trackId);
    if (activeIt == m_activeTracks.end())
    {
        return;
    }

    const auto stopCommand = QStringLiteral("stop %1").arg(activeIt->alias);
    mciSendStringW(reinterpret_cast<LPCWSTR>(stopCommand.utf16()), nullptr, 0, nullptr);
    const auto closeCommand = QStringLiteral("close %1").arg(activeIt->alias);
    mciSendStringW(reinterpret_cast<LPCWSTR>(closeCommand.utf16()), nullptr, 0, nullptr);
    m_activeTracks.erase(activeIt);
#else
    Q_UNUSED(trackId);
#endif
}

void MciAudioEngine::stopAll()
{
    std::vector<QUuid> trackIds;
    trackIds.reserve(m_activeTracks.size());
    for (auto it = m_activeTracks.cbegin(); it != m_activeTracks.cend(); ++it)
    {
        trackIds.push_back(it.key());
    }

    for (const auto& trackId : trackIds)
    {
        stopTrack(trackId);
    }
}

bool MciAudioEngine::isTrackPlaying(const QUuid& trackId) const
{
    return m_activeTracks.contains(trackId);
}
