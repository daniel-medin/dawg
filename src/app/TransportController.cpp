#include "app/TransportController.h"

TransportController::TransportController(QObject* parent)
    : QObject(parent)
{
    m_playbackTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_playbackTimer, &QTimer::timeout, this, &TransportController::playbackAdvanceRequested);
}

void TransportController::setPlaybackIntervalMs(const int intervalMs)
{
    m_playbackTimer.setInterval(intervalMs > 1 ? intervalMs : 1);
}

void TransportController::startPlayback(const int currentFrame)
{
    if (m_isPlaying)
    {
        return;
    }

    m_playbackAnchorFrame = currentFrame;
    m_isPlaying = true;
    m_playbackTimer.start();
    emit playbackStateChanged(true);
}

int TransportController::stopPlayback(const int currentFrame, const bool restorePlaybackAnchor)
{
    if (!m_isPlaying)
    {
        return -1;
    }

    m_isPlaying = false;
    m_playbackTimer.stop();
    emit playbackStateChanged(false);

    if (restorePlaybackAnchor
        && !m_insertionFollowsPlayback
        && m_playbackAnchorFrame >= 0
        && m_playbackAnchorFrame != currentFrame)
    {
        return m_playbackAnchorFrame;
    }

    return -1;
}

void TransportController::setInsertionFollowsPlayback(const bool enabled)
{
    if (m_insertionFollowsPlayback == enabled)
    {
        return;
    }

    m_insertionFollowsPlayback = enabled;
    emit insertionFollowsPlaybackChanged(m_insertionFollowsPlayback);
}

bool TransportController::isPlaying() const
{
    return m_isPlaying;
}

bool TransportController::insertionFollowsPlayback() const
{
    return m_insertionFollowsPlayback;
}
