#pragma once

#include <map>
#include <optional>
#include <vector>

#include <QColor>
#include <QPointF>
#include <QString>
#include <QUuid>

struct AudioAttachment
{
    QString assetPath;
    float gainDb = 0.0F;
    int clipStartMs = 0;
    std::optional<int> clipEndMs;
};

struct TrackPoint
{
    QUuid id = QUuid::createUuid();
    QString label;
    QColor color;
    int seedFrameIndex = -1;
    int startFrame = -1;
    std::optional<int> endFrame;
    bool motionTracked = false;
    bool showLabel = false;
    bool autoPanEnabled = true;
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

    [[nodiscard]] std::optional<QPointF> interpolatedSampleAt(const int frameIndex) const
    {
        if (samples.empty())
        {
            return std::nullopt;
        }

        const auto next = samples.lower_bound(frameIndex);
        if (next != samples.end() && next->first == frameIndex)
        {
            return next->second;
        }

        if (next == samples.begin())
        {
            return next->second;
        }

        if (next == samples.end())
        {
            return std::prev(next)->second;
        }

        const auto previous = std::prev(next);
        const auto frameSpan = next->first - previous->first;
        if (frameSpan <= 0)
        {
            return previous->second;
        }

        const auto progress = static_cast<double>(frameIndex - previous->first) / static_cast<double>(frameSpan);
        return QPointF{
            previous->second.x() + (next->second.x() - previous->second.x()) * progress,
            previous->second.y() + (next->second.y() - previous->second.y()) * progress
        };
    }

    [[nodiscard]] bool isVisibleAt(const int frameIndex) const
    {
        if (startFrame >= 0 && frameIndex < startFrame)
        {
            return false;
        }

        return !endFrame.has_value() || frameIndex <= *endFrame;
    }
};

[[nodiscard]] inline QColor inactiveTrackDisplayColor()
{
    return QColor{122, 128, 136};
}

[[nodiscard]] inline QColor trackDisplayColor(const TrackPoint& track)
{
    return track.attachedAudio.has_value() ? track.color : inactiveTrackDisplayColor();
}

struct TrackOverlay
{
    QUuid id;
    QString label;
    QColor color;
    QPointF imagePoint;
    bool isSelected = false;
    float highlightOpacity = 0.0F;
    bool showLabel = false;
    bool hasAttachedAudio = false;
    bool autoPanEnabled = false;
};

struct MotionTrackerState
{
    std::vector<TrackPoint> tracks;
    int nextColorIndex = 0;
};
