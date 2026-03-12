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
    emit statusChanged(QStringLiteral("Loaded %1").arg(filePath));

    return true;
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

void PlayerController::seedTrack(const QPointF& imagePoint)
{
    if (!hasVideoLoaded())
    {
        return;
    }

    auto& track = m_tracker.seedTrack(m_currentFrame.index, imagePoint);
    refreshOverlays();

    emit statusChanged(
        QStringLiteral("Seeded %1 at frame %2")
            .arg(track.label)
            .arg(m_currentFrame.index));
}

bool PlayerController::hasVideoLoaded() const
{
    return m_decoder != nullptr && m_currentFrame.isValid();
}

bool PlayerController::isPlaying() const
{
    return m_isPlaying;
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

void PlayerController::refreshOverlays()
{
    m_currentOverlays = m_tracker.overlaysForFrame(m_currentFrame.index);
    emit overlaysChanged();
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

