#pragma once

#include <vector>

#include <opencv2/core/mat.hpp>

#include "TrackTypes.h"

class MotionTracker
{
public:
    TrackPoint& seedTrack(int frameIndex, const QPointF& imagePoint);
    void trackForward(const cv::Mat& previousGrayFrame, const cv::Mat& currentGrayFrame, int currentFrameIndex);
    void reset();

    [[nodiscard]] const std::vector<TrackPoint>& tracks() const;
    [[nodiscard]] std::vector<TrackOverlay> overlaysForFrame(int frameIndex) const;

private:
    [[nodiscard]] QColor nextTrackColor();
    [[nodiscard]] static bool isInsideFrame(const cv::Point2f& point, const cv::Size& frameSize);

    std::vector<TrackPoint> m_tracks;
    int m_nextColorIndex = 0;
};

