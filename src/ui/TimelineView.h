#pragma once

#include <optional>
#include <vector>

#include <QWidget>

#include "ui/TimelineTypes.h"

class TimelineQuickController;
class QQuickView;

class TimelineView final : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineView(QWidget* parent = nullptr);

    void clear();
    void setTimeline(int totalFrames, double fps);
    void setCurrentFrame(int frameIndex);
    void setLoopRange(std::optional<int> startFrame, std::optional<int> endFrame);
    void setTrackSpans(const std::vector<TimelineTrackSpan>& trackSpans);
    void setSeekOnClickEnabled(bool enabled);
    [[nodiscard]] std::optional<int> loopEditFrame() const;
    [[nodiscard]] std::optional<int> loopShortcutFrame() const;
    [[nodiscard]] bool hasSelectedLoopRange() const;
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

signals:
    void frameRequested(int frameIndex);
    void loopStartFrameRequested(int frameIndex);
    void loopEndFrameRequested(int frameIndex);
    void trackSelected(const QUuid& trackId);
    void trackActivated(const QUuid& trackId);
    void trackStartFrameRequested(const QUuid& trackId, int frameIndex);
    void trackEndFrameRequested(const QUuid& trackId, int frameIndex);
    void trackSpanMoveRequested(const QUuid& trackId, int deltaFrames);
    void trackContextMenuRequested(const QUuid& trackId, const QPoint& globalPosition);
    void trackGainAdjustRequested(const QUuid& trackId, int wheelDelta, const QPoint& globalPosition);
    void trackGainPopupRequested(const QUuid& trackId, const QPoint& globalPosition);
    void loopContextMenuRequested(const QPoint& globalPosition);

private:
    void handleStatusChanged();
    void syncController();
    [[nodiscard]] int laneCount() const;
    [[nodiscard]] int preferredHeight() const;
    void updatePreferredHeight();

    QQuickView* m_quickView = nullptr;
    QWidget* m_container = nullptr;
    TimelineQuickController* m_timelineController = nullptr;
    int m_totalFrames = 0;
    int m_currentFrame = 0;
    double m_fps = 0.0;
    std::optional<int> m_loopStartFrame;
    std::optional<int> m_loopEndFrame;
    bool m_seekOnClickEnabled = true;
    std::vector<TimelineTrackSpan> m_trackSpans;
};
