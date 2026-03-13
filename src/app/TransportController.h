#pragma once

#include <QObject>
#include <QTimer>

class TransportController final : public QObject
{
    Q_OBJECT

public:
    explicit TransportController(QObject* parent = nullptr);

    void setPlaybackIntervalMs(int intervalMs);
    void startPlayback(int currentFrame);
    int stopPlayback(int currentFrame, bool restorePlaybackAnchor = true);
    void setInsertionFollowsPlayback(bool enabled);

    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool insertionFollowsPlayback() const;

signals:
    void playbackAdvanceRequested();
    void playbackStateChanged(bool playing);
    void insertionFollowsPlaybackChanged(bool enabled);

private:
    QTimer m_playbackTimer;
    bool m_isPlaying = false;
    bool m_insertionFollowsPlayback = true;
    int m_playbackAnchorFrame = -1;
};
