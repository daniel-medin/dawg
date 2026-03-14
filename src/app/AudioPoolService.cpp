#include "app/AudioPoolService.h"

#include <algorithm>

#include <QFileInfo>

#include "core/audio/AudioEngine.h"

void AudioPoolService::clear()
{
    m_assetPaths.clear();
}

bool AudioPoolService::import(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return false;
    }

    if (std::find(m_assetPaths.begin(), m_assetPaths.end(), filePath) != m_assetPaths.end())
    {
        return false;
    }

    m_assetPaths.push_back(filePath);
    return true;
}

bool AudioPoolService::remove(const QString& filePath)
{
    const auto removeIt = std::remove(m_assetPaths.begin(), m_assetPaths.end(), filePath);
    if (removeIt == m_assetPaths.end())
    {
        return false;
    }

    m_assetPaths.erase(removeIt, m_assetPaths.end());
    return true;
}

std::vector<AudioPoolItem> AudioPoolService::items(
    const std::vector<TrackPoint>& tracks,
    const AudioEngine& audioEngine,
    const QString& previewAssetPath) const
{
    std::vector<AudioPoolItem> items;
    items.reserve(m_assetPaths.size());

    for (const auto& assetPath : m_assetPaths)
    {
        const auto isPreviewPlaying = !previewAssetPath.isEmpty() && previewAssetPath == assetPath;

        struct ConnectedTrackInfo
        {
            QUuid id;
            QString label;
            bool isPlaying = false;
        };

        std::vector<ConnectedTrackInfo> connectedTracks;

        for (const auto& track : tracks)
        {
            if (!track.attachedAudio.has_value() || track.attachedAudio->assetPath != assetPath)
            {
                continue;
            }

            connectedTracks.push_back(ConnectedTrackInfo{
                .id = track.id,
                .label = track.label,
                .isPlaying = audioEngine.isTrackPlaying(track.id)
            });
        }

        const auto fileName = QFileInfo(assetPath).fileName();
        const auto connectedNodeCount = static_cast<int>(connectedTracks.size());
        if (connectedTracks.empty())
        {
            items.push_back(AudioPoolItem{
                .key = QStringLiteral("%1#0").arg(assetPath),
                .assetPath = assetPath,
                .trackId = {},
                .displayName = fileName,
                .connectedNodeCount = 0,
                .isPlaying = isPreviewPlaying,
                .connectionSummary = isPreviewPlaying
                    ? QStringLiteral("Previewing")
                    : QStringLiteral("Not connected")
            });
            continue;
        }

        for (int index = 0; index < connectedNodeCount; ++index)
        {
            const auto& connectedTrack = connectedTracks[static_cast<std::size_t>(index)];
            const auto displayName = index == 0
                ? fileName
                : (index == 1
                    ? QStringLiteral("%1 (duplicate)").arg(fileName)
                    : QStringLiteral("%1 (duplicate %2)").arg(fileName).arg(index));

            items.push_back(AudioPoolItem{
                .key = QStringLiteral("%1#%2").arg(assetPath).arg(index),
                .assetPath = assetPath,
                .trackId = connectedTrack.id,
                .displayName = displayName,
                .connectedNodeCount = connectedNodeCount,
                .isPlaying = connectedTrack.isPlaying || isPreviewPlaying,
                .connectionSummary = connectedTrack.isPlaying
                    ? QStringLiteral("Playing on %1").arg(connectedTrack.label)
                    : (isPreviewPlaying
                        ? QStringLiteral("Previewing")
                        : QStringLiteral("Connected to %1").arg(connectedTrack.label))
            });
        }
    }

    return items;
}
