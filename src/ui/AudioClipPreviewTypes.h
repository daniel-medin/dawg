#pragma once

#include <optional>

#include <QColor>
#include <QString>
#include <QUuid>

struct AudioClipPreviewState
{
    QUuid trackId;
    QString label;
    QColor color;
    QString assetPath;
    int nodeStartFrame = 0;
    int nodeEndFrame = 0;
    int clipStartMs = 0;
    int clipEndMs = 0;
    int sourceDurationMs = 0;
    std::optional<int> playheadMs;
    float gainDb = 0.0F;
    float level = 0.0F;
    bool hasAttachedAudio = false;
    bool loopEnabled = false;
};
