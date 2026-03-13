#include "app/PlayerController.h"

#include <algorithm>

#include <QImage>

#include <opencv2/imgproc.hpp>

#include "core/video/OpenCvVideoDecoder.h"

PlayerController::PlayerController(QObject* parent)
    : QObject(parent)
{
    connect(&m_playbackTimer, &QTimer::timeout, this, &PlayerController::advancePlayback);
}

bool PlayerController::openVideo(const QString& filePath)
{
    pause();

    auto decoder = std::make_unique<OpenCvVideoDecoder>();
    if (!decoder->open(filePath.toStdString()))
    {
        emit statusChanged(QStringLiteral("Failed to open video: %1").arg(filePath));
        return false;
    }

    const auto firstFrame = decoder->readFrame();
    if (!firstFrame.has_value() || !firstFrame->isValid())
    {
        emit statusChanged(QStringLiteral("The file opened, but no frames were decoded."));
        return false;
    }

    m_tracker.reset();
    m_currentOverlays.clear();
    m_selectedTrackId = {};
    m_loadedPath = filePath;
    m_totalFrames = decoder->frameCount();
    m_fps = decoder->fps();
    m_currentFrame = *firstFrame;
    m_decoder = std::move(decoder);

    cv::cvtColor(m_currentFrame.bgr, m_currentGrayFrame, cv::COLOR_BGR2GRAY);
    m_playbackTimer.setInterval(std::max(1, static_cast<int>(1000.0 / std::max(1.0, m_fps))));

    refreshOverlays();
    emitCurrentFrame();
    emit videoLoaded(m_loadedPath, m_totalFrames, m_fps);
    emit selectionChanged(false);
    emit statusChanged(QStringLiteral("Loaded %1").arg(filePath));

    return true;
}

void PlayerController::goToStart()
{
    if (!hasVideoLoaded())
    {
        return;
    }

    pause();
    if (loadFrameAt(0))
    {
        emit statusChanged(QStringLiteral("Returned to the start of the clip."));
    }
}

void PlayerController::togglePlayback()
{
    if (!hasVideoLoaded())
    {
        return;
    }

    m_isPlaying = !m_isPlaying;
    if (m_isPlaying)
    {
        m_playbackTimer.start();
    }
    else
    {
        m_playbackTimer.stop();
    }

    emit playbackStateChanged(m_isPlaying);
}

void PlayerController::pause()
{
    if (!m_isPlaying)
    {
        return;
    }

    m_isPlaying = false;
    m_playbackTimer.stop();
    emit playbackStateChanged(false);
}

void PlayerController::stepForward()
{
    if (!hasVideoLoaded())
    {
        return;
    }

    const auto previousGrayFrame = m_currentGrayFrame.clone();
    const auto nextFrame = m_decoder->readFrame();
    if (!nextFrame.has_value() || !nextFrame->isValid())
    {
        pause();
        emit statusChanged(QStringLiteral("Reached the end of the clip."));
        return;
    }

    cv::Mat nextGrayFrame;
    cv::cvtColor(nextFrame->bgr, nextGrayFrame, cv::COLOR_BGR2GRAY);
    m_tracker.trackForward(previousGrayFrame, nextGrayFrame, nextFrame->index);

    m_currentFrame = *nextFrame;
    m_currentGrayFrame = nextGrayFrame;

    refreshOverlays();
    emitCurrentFrame();
}

void PlayerController::stepBackward()
{
    if (!hasVideoLoaded())
    {
        return;
    }

    pause();

    const auto targetFrameIndex = std::max(0, m_currentFrame.index - 1);
    if (targetFrameIndex == m_currentFrame.index)
    {
        emit statusChanged(QStringLiteral("Already at the first frame."));
        return;
    }

    if (!loadFrameAt(targetFrameIndex))
    {
        emit statusChanged(QStringLiteral("Failed to step backward."));
    }
}

void PlayerController::seedTrack(const QPointF& imagePoint)
{
    if (!hasVideoLoaded())
    {
        return;
    }

    auto& track = m_tracker.seedTrack(m_currentFrame.index, imagePoint, m_motionTrackingEnabled);
    setSelectedTrackId(track.id);
    refreshOverlays();

    emit statusChanged(
        QStringLiteral("Added %1 at frame %2 (%3)")
            .arg(track.label)
            .arg(m_currentFrame.index)
            .arg(track.motionTracked ? QStringLiteral("tracked") : QStringLiteral("manual")));
}

void PlayerController::selectTrack(const QUuid& trackId)
{
    if (!m_tracker.hasTrack(trackId))
    {
        clearSelection();
        return;
    }

    setSelectedTrackId(trackId);
}

void PlayerController::clearSelection()
{
    setSelectedTrackId({});
}

void PlayerController::moveSelectedTrack(const QPointF& imagePoint)
{
    if (!hasVideoLoaded() || m_selectedTrackId.isNull())
    {
        return;
    }

    if (m_tracker.updateTrackSample(m_selectedTrackId, m_currentFrame.index, imagePoint))
    {
        refreshOverlays();
    }
}

void PlayerController::deleteSelectedTrack()
{
    if (m_selectedTrackId.isNull())
    {
        return;
    }

    if (!m_tracker.removeTrack(m_selectedTrackId))
    {
        setSelectedTrackId({});
        emit statusChanged(QStringLiteral("The selected node no longer exists."));
        return;
    }

    setSelectedTrackId({});
    refreshOverlays();
    emit statusChanged(QStringLiteral("Deleted selected node."));
}

void PlayerController::setMotionTrackingEnabled(const bool enabled)
{
    if (m_motionTrackingEnabled == enabled)
    {
        return;
    }

    m_motionTrackingEnabled = enabled;
    emit motionTrackingChanged(m_motionTrackingEnabled);
    emit statusChanged(
        m_motionTrackingEnabled
            ? QStringLiteral("Motion tracking enabled.")
            : QStringLiteral("Motion tracking disabled. New nodes will stay manual."));
}

bool PlayerController::hasVideoLoaded() const
{
    return m_decoder != nullptr && m_currentFrame.isValid();
}

bool PlayerController::isPlaying() const
{
    return m_isPlaying;
}

bool PlayerController::isMotionTrackingEnabled() const
{
    return m_motionTrackingEnabled;
}

bool PlayerController::hasSelection() const
{
    return !m_selectedTrackId.isNull();
}

int PlayerController::currentFrameIndex() const
{
    return m_currentFrame.index;
}

int PlayerController::totalFrames() const
{
    return m_totalFrames;
}

double PlayerController::fps() const
{
    return m_fps;
}

QString PlayerController::loadedPath() const
{
    return m_loadedPath;
}

const std::vector<TrackOverlay>& PlayerController::currentOverlays() const
{
    return m_currentOverlays;
}

void PlayerController::advancePlayback()
{
    stepForward();
}

bool PlayerController::loadFrameAt(const int frameIndex)
{
    if (!hasVideoLoaded() || !m_decoder->seekFrame(frameIndex))
    {
        return false;
    }

    const auto frame = m_decoder->readFrame();
    if (!frame.has_value() || !frame->isValid())
    {
        return false;
    }

    m_currentFrame = *frame;
    cv::cvtColor(m_currentFrame.bgr, m_currentGrayFrame, cv::COLOR_BGR2GRAY);
    refreshOverlays();
    emitCurrentFrame();
    return true;
}

void PlayerController::refreshOverlays()
{
    m_currentOverlays = m_tracker.overlaysForFrame(m_currentFrame.index, m_selectedTrackId);
    emit overlaysChanged();
}

void PlayerController::setSelectedTrackId(const QUuid& trackId)
{
    if (m_selectedTrackId == trackId)
    {
        return;
    }

    m_selectedTrackId = trackId;
    refreshOverlays();
    emit selectionChanged(!m_selectedTrackId.isNull());
}

void PlayerController::emitCurrentFrame()
{
    emit frameReady(toImage(m_currentFrame.bgr), m_currentFrame.index, m_currentFrame.timestampSeconds);
}

QImage PlayerController::toImage(const cv::Mat& bgrFrame) const
{
    cv::Mat rgbFrame;
    cv::cvtColor(bgrFrame, rgbFrame, cv::COLOR_BGR2RGB);

    return QImage(
               rgbFrame.data,
               rgbFrame.cols,
               rgbFrame.rows,
               static_cast<int>(rgbFrame.step),
               QImage::Format_RGB888)
        .copy();
}
