#pragma once

#include <map>
#include <optional>

#include <QColor>
#include <QPointF>
#include <QString>
#include <QUuid>

struct AudioAttachment
{
    QString assetPath;
    float gainDb = 0.0F;
};

struct TrackPoint
{
    QUuid id = QUuid::createUuid();
    QString label;
    QColor color;
    int seedFrameIndex = -1;
    bool motionTracked = false;
    std::map<int, QPointF> samples;
    std::optional<AudioAttachment> attachedAudio;

    [[nodiscard]] bool hasSample(const int frameIndex) const
    {
        return samples.contains(frameIndex);
    }

    [[nodiscard]] QPointF sampleAt(const int frameIndex) const
    {
        const auto it = samples.find(frameIndex);
        return it != samples.end() ? it->second : QPointF{};
    }

    [[nodiscard]] std::optional<QPointF> sampleAtOrBefore(const int frameIndex) const
    {
        const auto it = samples.upper_bound(frameIndex);
        if (it == samples.begin())
        {
            return std::nullopt;
        }

        return std::prev(it)->second;
    }
};

struct TrackOverlay
{
    QUuid id;
    QString label;
    QColor color;
    QPointF imagePoint;
    bool isSelected = false;
    float highlightOpacity = 0.0F;
    bool hasAttachedAudio = false;
};
