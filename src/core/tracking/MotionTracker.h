#pragma once

#include <vector>

#include <opencv2/core/mat.hpp>

#include "TrackTypes.h"

class MotionTracker
{
public:
    TrackPoint& seedTrack(int frameIndex, const QPointF& imagePoint, bool motionTracked);
    void trackForward(const cv::Mat& previousGrayFrame, const cv::Mat& currentGrayFrame, int currentFrameIndex);
    void reset();
    bool hasTrack(const QUuid& trackId) const;
    bool updateTrackSample(const QUuid& trackId, int frameIndex, const QPointF& imagePoint);
    bool setTrackLabel(const QUuid& trackId, const QString& label);
    bool setTrackStartFrame(const QUuid& trackId, int startFrame);
    bool setTrackEndFrame(const QUuid& trackId, int endFrame);
    bool moveTrackFrameSpan(const QUuid& trackId, int deltaFrames, int maxFrameIndex);
    bool setTrackAudioAttachment(const QUuid& trackId, const QString& assetPath);
    int detachTrackAudioByPath(const QString& assetPath);
    bool isTrackLabelVisible(const QUuid& trackId) const;
    int setTrackLabelsVisible(const std::vector<QUuid>& trackIds, bool visible);
    int setTrackStartFrames(const std::vector<QUuid>& trackIds, int startFrame);
    int setTrackEndFrames(const std::vector<QUuid>& trackIds, int endFrame);
    int setAllTrackStartFrames(int startFrame);
    int setAllTrackEndFrames(int endFrame);
    bool removeTrack(const QUuid& trackId);
    int removeTracks(const std::vector<QUuid>& trackIds);

    [[nodiscard]] const std::vector<TrackPoint>& tracks() const;
    [[nodiscard]] std::vector<TrackOverlay> overlaysForFrame(
        int frameIndex,
        const std::vector<QUuid>& selectedTrackIds = {},
        const QUuid& fadingTrackId = {},
        float fadingTrackOpacity = 0.0F) const;

private:
    [[nodiscard]] QColor nextTrackColor();
    [[nodiscard]] static bool isInsideFrame(const cv::Point2f& point, const cv::Size& frameSize);

    std::vector<TrackPoint> m_tracks;
    int m_nextColorIndex = 0;
};
