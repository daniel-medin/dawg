#pragma once

#include <QColor>
#include <QString>
#include <QUuid>

struct TimelineTrackSpan
{
    QUuid id;
    QString label;
    QColor color;
    int startFrame = 0;
    int endFrame = 0;
    int laneIndex = 0;
    bool hasAttachedAudio = false;
    bool isSelected = false;

    [[nodiscard]] bool operator==(const TimelineTrackSpan& other) const
    {
        return id == other.id
            && label == other.label
            && color == other.color
            && startFrame == other.startFrame
            && endFrame == other.endFrame
            && laneIndex == other.laneIndex
            && hasAttachedAudio == other.hasAttachedAudio
            && isSelected == other.isSelected;
    }
};

struct TimelineLoopRange
{
    int startFrame = 0;
    int endFrame = 0;

    [[nodiscard]] bool operator==(const TimelineLoopRange& other) const
    {
        return startFrame == other.startFrame
            && endFrame == other.endFrame;
    }
};
