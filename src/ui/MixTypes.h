#pragma once

#include <vector>

#include <QColor>
#include <QString>

struct MixLaneStrip
{
    int laneIndex = 0;
    QString label;
    QColor color;
    float gainDb = 0.0F;
    float meterLevel = 0.0F;
    float meterLeftLevel = 0.0F;
    float meterRightLevel = 0.0F;
    int clipCount = 0;
    bool muted = false;
    bool soloed = false;
    bool useStereoMeter = false;

    [[nodiscard]] bool operator==(const MixLaneStrip& other) const
    {
        return laneIndex == other.laneIndex
            && label == other.label
            && color == other.color
            && gainDb == other.gainDb
            && clipCount == other.clipCount
            && muted == other.muted
            && soloed == other.soloed
            && useStereoMeter == other.useStereoMeter;
    }
};
