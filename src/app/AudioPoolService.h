#pragma once

#include <cstdint>
#include <vector>

#include <QString>
#include <QUuid>

#include "core/tracking/TrackTypes.h"

class AudioEngine;

struct AudioPoolItem
{
    QString key;
    QString assetPath;
    QUuid trackId;
    QString displayName;
    int connectedNodeCount = 0;
    bool isPlaying = false;
    QString connectionSummary;
    int durationMs = -1;
    std::int64_t fileSizeBytes = -1;
};

class AudioPoolService
{
public:
    void clear();
    bool import(const QString& filePath);
    bool remove(const QString& filePath);
    void setAssetPaths(const std::vector<QString>& assetPaths);
    [[nodiscard]] const std::vector<QString>& assetPaths() const;
    [[nodiscard]] std::vector<AudioPoolItem> items(
        const std::vector<TrackPoint>& tracks,
        const AudioEngine& audioEngine,
        const QString& previewAssetPath = {}) const;

private:
    std::vector<QString> m_assetPaths;
};
