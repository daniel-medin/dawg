#include "app/VideoPlaybackCoordinator.h"

#include <algorithm>

#include <QFileInfo>

#include "app/PerformanceLogger.h"
#include "app/TransportController.h"
#include "core/audio/VideoAudioExtractor.h"
#include "core/render/RenderService.h"
#include "core/tracking/MotionTracker.h"
#include "core/video/AnalysisFrameProvider.h"

VideoPlaybackCoordinator::VideoPlaybackCoordinator(
    MotionTracker& tracker,
    TransportController& transport,
    PerformanceLogger& perfLogger)
    : m_tracker(tracker)
    , m_transport(transport)
    , m_perfLogger(perfLogger)
{
}

void VideoPlaybackCoordinator::resetState()
{
    m_loadedPath.clear();
    m_embeddedVideoAudioPath.clear();
    m_embeddedVideoAudioDisplayName.clear();
    m_totalFrames = 0;
    m_fps = 0.0;
    m_currentFrame = {};
    m_currentGrayFrame.release();
    m_playbackStartTimestampSeconds = 0.0;
    m_lastLoggedQueueStarvationCount = 0;
}

void VideoPlaybackCoordinator::close()
{
    m_videoPlayback.close();
    m_videoPlayback.setPresentationScale(1.0);
    resetState();
}

VideoPlaybackCoordinator::OpenVideoResult VideoPlaybackCoordinator::openVideo(const QString& filePath)
{
    m_videoPlayback.setPresentationScale(1.0);
    if (!m_videoPlayback.open(filePath))
    {
        return {
            .success = false,
            .errorMessage = QStringLiteral("Failed to open video: %1").arg(filePath)
        };
    }

    resetState();
    m_loadedPath = filePath;
    m_totalFrames = m_videoPlayback.totalFrames();
    m_fps = m_videoPlayback.fps();
    m_currentFrame = m_videoPlayback.currentFrame();
    if (const auto extractedAudioPath = dawg::audio::extractEmbeddedAudioToWave(filePath); extractedAudioPath.has_value())
    {
        m_embeddedVideoAudioPath = *extractedAudioPath;
        m_embeddedVideoAudioDisplayName = QFileInfo(filePath).fileName();
    }

    updateCurrentGrayFrameIfNeeded();
    const auto safeFps = m_fps > 1.0 ? m_fps : 1.0;
    const auto playbackIntervalMs = static_cast<int>(1000.0 / safeFps);
    m_transport.setPlaybackIntervalMs(playbackIntervalMs);
    m_perfLogger.startSession(
        filePath,
        m_videoPlayback.decoderBackendName(),
        m_renderService.backendName(),
        m_fps,
        m_totalFrames);

    return {.success = true};
}

bool VideoPlaybackCoordinator::hasVideoLoaded() const
{
    return m_videoPlayback.hasVideoLoaded() && m_currentFrame.isValid();
}

const QString& VideoPlaybackCoordinator::loadedPath() const
{
    return m_loadedPath;
}

int VideoPlaybackCoordinator::totalFrames() const
{
    return m_totalFrames;
}

double VideoPlaybackCoordinator::fps() const
{
    return m_fps;
}

const VideoFrame& VideoPlaybackCoordinator::currentFrame() const
{
    return m_currentFrame;
}

bool VideoPlaybackCoordinator::hasEmbeddedVideoAudio() const
{
    return !m_embeddedVideoAudioPath.isEmpty();
}

const QString& VideoPlaybackCoordinator::embeddedVideoAudioPath() const
{
    return m_embeddedVideoAudioPath;
}

const QString& VideoPlaybackCoordinator::embeddedVideoAudioDisplayName() const
{
    return m_embeddedVideoAudioDisplayName;
}

QString VideoPlaybackCoordinator::decoderBackendName() const
{
    return m_videoPlayback.decoderBackendName();
}

bool VideoPlaybackCoordinator::videoHardwareAccelerated() const
{
    return m_videoPlayback.isHardwareDecoded();
}

QString VideoPlaybackCoordinator::renderBackendName() const
{
    return m_renderService.backendName();
}

bool VideoPlaybackCoordinator::renderHardwareAccelerated() const
{
    return m_renderService.isHardwareAccelerated();
}

RenderService& VideoPlaybackCoordinator::renderService()
{
    return m_renderService;
}

const RenderService& VideoPlaybackCoordinator::renderService() const
{
    return m_renderService;
}

void VideoPlaybackCoordinator::setPreferredD3D11Device(void* device)
{
    m_videoPlayback.setPreferredD3D11Device(device);
}

void VideoPlaybackCoordinator::setNativePresentationEnabled(const bool enabled)
{
    if (m_nativePresentationEnabled == enabled)
    {
        return;
    }

    m_nativePresentationEnabled = enabled;
    updateCpuFrameExtractionMode();
}

double VideoPlaybackCoordinator::frameTimestampSeconds(const int frameIndex) const
{
    return m_videoPlayback.frameTimestampSeconds(frameIndex);
}

VideoPlaybackRuntimeStats VideoPlaybackCoordinator::runtimeStats() const
{
    return m_videoPlayback.runtimeStats();
}

VideoPlaybackCoordinator::PresentedFrame VideoPlaybackCoordinator::presentCurrentFrame(const bool playbackActive)
{
    return PresentedFrame{
        .image = m_renderService.presentFrame(m_currentFrame, playbackActive),
        .frameIndex = m_currentFrame.index,
        .timestampSeconds = m_currentFrame.timestampSeconds
    };
}

void VideoPlaybackCoordinator::restartPlaybackTiming()
{
    m_playbackStartTimestampSeconds = frameTimestampSeconds(m_currentFrame.index);
    m_playbackElapsedTimer.restart();
    m_perfPlaybackTickTimer.restart();
}

bool VideoPlaybackCoordinator::loadFrameAt(const int frameIndex, const std::function<void()>& onFrameChanged)
{
    if (!hasVideoLoaded() || !m_videoPlayback.seekFrame(frameIndex))
    {
        return false;
    }

    m_currentFrame = m_videoPlayback.currentFrame();
    updateCurrentGrayFrameIfNeeded();
    if (onFrameChanged)
    {
        onFrameChanged();
    }
    return true;
}

bool VideoPlaybackCoordinator::stepForward(const bool syncAudio, const FrameCallbacks& callbacks)
{
    return advanceOneFrame(true, syncAudio, callbacks);
}

VideoPlaybackCoordinator::RelativeSeekOutcome VideoPlaybackCoordinator::seekRelativeFrames(
    const int deltaFrames,
    const std::function<void()>& onFrameChanged)
{
    if (!hasVideoLoaded())
    {
        return {};
    }

    const auto maxFrameIndex = std::max(0, m_totalFrames - 1);
    const auto targetFrameIndex = std::clamp(m_currentFrame.index + deltaFrames, 0, maxFrameIndex);
    if (targetFrameIndex == m_currentFrame.index)
    {
        return {
            .status = RelativeSeekOutcome::Status::Boundary,
            .targetFrameIndex = targetFrameIndex
        };
    }

    if (!loadFrameAt(targetFrameIndex, onFrameChanged))
    {
        return {
            .status = RelativeSeekOutcome::Status::Failed,
            .targetFrameIndex = targetFrameIndex
        };
    }

    return {
        .status = RelativeSeekOutcome::Status::Success,
        .targetFrameIndex = targetFrameIndex
    };
}

VideoPlaybackCoordinator::SeekOutcome VideoPlaybackCoordinator::seekToFrame(
    const int frameIndex,
    const bool isPlaying,
    const std::function<void()>& onFrameChanged,
    const std::function<void()>& onSyncAudio)
{
    SeekOutcome outcome;
    if (!hasVideoLoaded())
    {
        return outcome;
    }

    const auto maxFrameIndex = m_totalFrames > 0 ? (m_totalFrames - 1) : 0;
    const auto clampedFrameIndex = std::clamp(frameIndex, 0, maxFrameIndex);
    outcome.targetFrameIndex = clampedFrameIndex;
    if (clampedFrameIndex == m_currentFrame.index)
    {
        outcome.status = SeekOutcome::Status::Unchanged;
        return outcome;
    }

    QElapsedTimer seekTimer;
    seekTimer.start();
    if (!loadFrameAt(clampedFrameIndex, onFrameChanged))
    {
        outcome.status = SeekOutcome::Status::Failed;
        return outcome;
    }

    outcome.elapsedMs = seekTimer.elapsed();
    outcome.runtimeStats = m_videoPlayback.runtimeStats();
    outcome.status = SeekOutcome::Status::Success;
    m_perfLogger.logEvent(
        outcome.elapsedMs >= 12 ? QStringLiteral("seek_slow") : QStringLiteral("seek"),
        QStringLiteral("targetFrame=%1 elapsedMs=%2 queue=%3/%4 fallbackStarvations=%5")
            .arg(clampedFrameIndex)
            .arg(outcome.elapsedMs)
            .arg(outcome.runtimeStats.queuedFrames)
            .arg(outcome.runtimeStats.prefetchTargetFrames)
            .arg(outcome.runtimeStats.queueStarvationCount));

    if (isPlaying)
    {
        m_transport.setPlaybackAnchorFrame(clampedFrameIndex);
        restartPlaybackTiming();
        if (onSyncAudio)
        {
            onSyncAudio();
        }
    }

    return outcome;
}

void VideoPlaybackCoordinator::advancePlayback(const PlaybackCallbacks& callbacks)
{
    if (!hasVideoLoaded() || !m_transport.isPlaying())
    {
        return;
    }

    const auto loopRange = callbacks.activeLoopRange ? callbacks.activeLoopRange() : std::nullopt;
    if (loopRange.has_value() && m_currentFrame.index == loopRange->second)
    {
        if (!loadFrameAt(loopRange->first, callbacks.onFrameChanged))
        {
            if (callbacks.onPausePlayback)
            {
                callbacks.onPausePlayback(false);
            }
            return;
        }

        restartPlaybackTiming();
        if (callbacks.onSyncAudio)
        {
            callbacks.onSyncAudio();
        }
        return;
    }

    if (!m_playbackElapsedTimer.isValid())
    {
        restartPlaybackTiming();
        static_cast<void>(advanceOneFrame(true, true, callbacks));
        return;
    }

    const auto targetTimestampSeconds =
        m_playbackStartTimestampSeconds + (static_cast<double>(m_playbackElapsedTimer.elapsed()) / 1000.0);

    auto targetFrameIndex = m_videoPlayback.frameIndexForPresentationTime(targetTimestampSeconds, m_currentFrame.index);
    if (loopRange.has_value()
        && m_currentFrame.index >= loopRange->first
        && m_currentFrame.index <= loopRange->second)
    {
        targetFrameIndex = std::clamp(targetFrameIndex, m_currentFrame.index, loopRange->second);
    }
    else
    {
        targetFrameIndex = std::clamp(targetFrameIndex, m_currentFrame.index, std::max(0, m_totalFrames - 1));
    }
    if (targetFrameIndex == m_currentFrame.index)
    {
        return;
    }

    const auto previousFrameIndex = m_currentFrame.index;
    int advancedFrames = 0;
    while (m_transport.isPlaying() && m_currentFrame.index < targetFrameIndex)
    {
        const auto previousStepFrameIndex = m_currentFrame.index;
        const auto shouldPresentFrame = previousStepFrameIndex >= (targetFrameIndex - 1);
        if (!advanceOneFrame(shouldPresentFrame, false, callbacks))
        {
            break;
        }
        ++advancedFrames;
    }

    if (advancedFrames > 0 && m_transport.isPlaying())
    {
        if (m_currentFrame.index != targetFrameIndex && callbacks.onFrameChanged)
        {
            callbacks.onFrameChanged();
        }
        if (callbacks.onSyncAudio)
        {
            callbacks.onSyncAudio();
        }
    }

    logPlaybackHitchIfNeeded(targetFrameIndex, previousFrameIndex, advancedFrames);
}

bool VideoPlaybackCoordinator::applyPresentationScaleForPlaybackState(
    const bool playbackActive,
    const std::function<void()>& onFrameChanged)
{
    const auto targetScale = (m_renderService.fastPlaybackEnabled() && playbackActive) ? 0.5 : 1.0;
    if (!m_videoPlayback.setPresentationScale(targetScale))
    {
        return false;
    }

    m_currentFrame = m_videoPlayback.currentFrame();
    updateCurrentGrayFrameIfNeeded();
    if (onFrameChanged)
    {
        onFrameChanged();
    }
    return true;
}

bool VideoPlaybackCoordinator::advanceOneFrame(
    const bool presentFrame,
    const bool syncAudio,
    const FrameCallbacks& callbacks)
{
    if (!hasVideoLoaded())
    {
        return false;
    }

    updateCurrentGrayFrameIfNeeded();
    const auto previousGrayFrame = m_currentGrayFrame;
    const auto nextFrame = m_videoPlayback.stepForward();
    if (!nextFrame.has_value() || !nextFrame->isValid())
    {
        if (callbacks.onPausePlayback)
        {
            callbacks.onPausePlayback(true);
        }
        if (callbacks.onStatusChanged)
        {
            callbacks.onStatusChanged(QStringLiteral("Reached the end of the clip."));
        }
        return false;
    }

    cv::Mat nextGrayFrame;
    if (needsTrackingFrameProcessing())
    {
        nextGrayFrame = m_analysisFrameProvider.grayscaleFrame(*nextFrame);
        m_tracker.trackForward(previousGrayFrame, nextGrayFrame, nextFrame->index);
    }

    m_currentFrame = *nextFrame;
    m_currentGrayFrame = nextGrayFrame;

    if (presentFrame && callbacks.onFrameChanged)
    {
        callbacks.onFrameChanged();
    }
    if (syncAudio && callbacks.onSyncAudio)
    {
        callbacks.onSyncAudio();
    }

    return true;
}

bool VideoPlaybackCoordinator::needsTrackingFrameProcessing() const
{
    return m_tracker.hasMotionTrackedTracks();
}

void VideoPlaybackCoordinator::updateCpuFrameExtractionMode()
{
    const auto needsCpuFrames =
        !m_nativePresentationEnabled
        || !m_videoPlayback.isHardwareDecoded()
        || needsTrackingFrameProcessing()
        || m_currentFrame.rotationDegrees != 0;
    static_cast<void>(m_videoPlayback.setCpuFrameExtractionEnabled(needsCpuFrames));
}

void VideoPlaybackCoordinator::updateCurrentGrayFrameIfNeeded()
{
    updateCpuFrameExtractionMode();

    if (!needsTrackingFrameProcessing())
    {
        m_currentGrayFrame.release();
        return;
    }

    m_currentGrayFrame = m_analysisFrameProvider.grayscaleFrame(m_currentFrame);
}

void VideoPlaybackCoordinator::logPlaybackHitchIfNeeded(
    const int targetFrameIndex,
    const int previousFrameIndex,
    const int advancedFrames)
{
    if (!m_transport.isPlaying())
    {
        return;
    }

    const auto stats = m_videoPlayback.runtimeStats();
    const auto tickDeltaMs = m_perfPlaybackTickTimer.isValid() ? m_perfPlaybackTickTimer.restart() : 0;
    const auto skippedFrames = std::max(0, targetFrameIndex - previousFrameIndex);
    const auto shouldLog =
        tickDeltaMs > 40
        || stats.lastStepWaitMs > 4
        || stats.lastStepUsedSynchronousFallback
        || skippedFrames > 1
        || stats.queueStarvationCount > m_lastLoggedQueueStarvationCount;

    if (!shouldLog)
    {
        return;
    }

    m_lastLoggedQueueStarvationCount = stats.queueStarvationCount;
    m_perfLogger.logEvent(
        QStringLiteral("playback_hitch"),
        QStringLiteral(
            "tickMs=%1 currentFrame=%2 targetFrame=%3 advanced=%4 queued=%5/%6 waitMs=%7 syncFallback=%8 starvationCount=%9")
            .arg(tickDeltaMs)
            .arg(m_currentFrame.index)
            .arg(targetFrameIndex)
            .arg(advancedFrames)
            .arg(stats.queuedFrames)
            .arg(stats.prefetchTargetFrames)
            .arg(stats.lastStepWaitMs)
            .arg(stats.lastStepUsedSynchronousFallback ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(stats.queueStarvationCount));
}
