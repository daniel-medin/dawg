#include "app/DebugUiController.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include <QFileInfo>
#include <QElapsedTimer>
#include <QSignalBlocker>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#endif

#include "app/MainWindow.h"
#include "app/PlayerController.h"
#include "app/WindowChromeController.h"
#include "ui/DebugOverlayWindow.h"
#include "ui/NativeVideoViewport.h"
#include "ui/TimelineQuickController.h"
#include "ui/VideoOverlayQuickItem.h"
#include "ui/VideoViewportQuickController.h"

namespace
{
double elapsedMs(const QElapsedTimer& timer)
{
    return static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
}

void updateSmoothedMs(double& target, const double sampleMs)
{
    constexpr double kBlend = 0.2;
    target = target <= 0.0 ? sampleMs : ((target * (1.0 - kBlend)) + (sampleMs * kBlend));
}

#ifdef Q_OS_WIN
class GpuEngineUsageSampler
{
public:
    GpuEngineUsageSampler()
    {
        if (PdhOpenQueryW(nullptr, 0, &m_query) != ERROR_SUCCESS)
        {
            m_query = nullptr;
            return;
        }

        if (PdhAddEnglishCounterW(
                m_query,
                L"\\GPU Engine(*)\\Utilization Percentage",
                0,
                &m_counter) != ERROR_SUCCESS)
        {
            PdhCloseQuery(m_query);
            m_query = nullptr;
            m_counter = nullptr;
            return;
        }

        m_available = true;
    }

    ~GpuEngineUsageSampler()
    {
        if (m_query)
        {
            PdhCloseQuery(m_query);
        }
    }

    [[nodiscard]] QString sample() const
    {
        if (!m_available || !m_query || !m_counter)
        {
            return QStringLiteral("GPU --");
        }

        if (PdhCollectQueryData(m_query) != ERROR_SUCCESS)
        {
            return QStringLiteral("GPU --");
        }

        DWORD bufferSize = 0;
        DWORD itemCount = 0;
        const auto firstResult = PdhGetFormattedCounterArrayW(
            m_counter,
            PDH_FMT_DOUBLE,
            &bufferSize,
            &itemCount,
            nullptr);
        if (firstResult != PDH_MORE_DATA || bufferSize == 0 || itemCount == 0)
        {
            return m_primed ? QStringLiteral("GPU 0.0%") : QStringLiteral("GPU --");
        }

        std::vector<std::byte> buffer(bufferSize);
        auto* items = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buffer.data());
        if (PdhGetFormattedCounterArrayW(
                m_counter,
                PDH_FMT_DOUBLE,
                &bufferSize,
                &itemCount,
                items) != ERROR_SUCCESS)
        {
            return QStringLiteral("GPU --");
        }

        const auto pidPattern = QStringLiteral("pid_%1_").arg(GetCurrentProcessId());
        double totalPercent = 0.0;
        for (DWORD index = 0; index < itemCount; ++index)
        {
            const QString instanceName = QString::fromWCharArray(items[index].szName ? items[index].szName : L"");
            if (!instanceName.contains(pidPattern, Qt::CaseInsensitive))
            {
                continue;
            }

            if (items[index].FmtValue.CStatus != ERROR_SUCCESS)
            {
                continue;
            }

            totalPercent += items[index].FmtValue.doubleValue;
        }

        m_primed = true;
        totalPercent = std::clamp(totalPercent, 0.0, 999.0);
        return QStringLiteral("GPU %1%").arg(totalPercent, 0, 'f', 1);
    }

private:
    mutable bool m_primed = false;
    bool m_available = false;
    PDH_HQUERY m_query = nullptr;
    PDH_HCOUNTER m_counter = nullptr;
};

QString currentGpuUsageText()
{
    static const GpuEngineUsageSampler sampler;
    return sampler.sample();
}
#else
QString currentGpuUsageText()
{
    return QStringLiteral("GPU --");
}
#endif

QString currentMemoryUsageText()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&counters),
            sizeof(counters)))
    {
        const auto workingSetMb = static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
        return QStringLiteral("Memory %1 MB").arg(workingSetMb, 0, 'f', 1);
    }
    return QStringLiteral("Memory --");
#else
    return QStringLiteral("Memory --");
#endif
}

QString currentVideoMemoryUsageText()
{
#ifdef Q_OS_WIN
    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory)
    {
        return QStringLiteral("VRAM --");
    }

    quint64 totalUsageBytes = 0;
    for (UINT adapterIndex = 0;; ++adapterIndex)
    {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }

        IDXGIAdapter3* adapter3 = nullptr;
        if (SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter3))) && adapter3)
        {
            DXGI_QUERY_VIDEO_MEMORY_INFO localMemoryInfo{};
            if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localMemoryInfo)))
            {
                totalUsageBytes += localMemoryInfo.CurrentUsage;
            }
            adapter3->Release();
        }

        adapter->Release();
    }

    factory->Release();

    if (totalUsageBytes > 0)
    {
        const auto totalUsageMb = static_cast<double>(totalUsageBytes) / (1024.0 * 1024.0);
        return QStringLiteral("VRAM %1 MB").arg(totalUsageMb, 0, 'f', 1);
    }
    return QStringLiteral("VRAM --");
#else
    return QStringLiteral("VRAM --");
#endif
}

QString currentProcessorUsageText()
{
#ifdef Q_OS_WIN
    FILETIME creationTime{};
    FILETIME exitTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    FILETIME systemTime{};
    if (!GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime))
    {
        return QStringLiteral("CPU --");
    }

    GetSystemTimeAsFileTime(&systemTime);

    ULARGE_INTEGER kernel{};
    kernel.LowPart = kernelTime.dwLowDateTime;
    kernel.HighPart = kernelTime.dwHighDateTime;

    ULARGE_INTEGER user{};
    user.LowPart = userTime.dwLowDateTime;
    user.HighPart = userTime.dwHighDateTime;

    ULARGE_INTEGER now{};
    now.LowPart = systemTime.dwLowDateTime;
    now.HighPart = systemTime.dwHighDateTime;

    static quint64 previousProcessTicks = 0;
    static quint64 previousWallTicks = 0;
    static const unsigned int processorCount = []() -> unsigned int
    {
        SYSTEM_INFO systemInfo{};
        GetSystemInfo(&systemInfo);
        return std::max<unsigned int>(1u, static_cast<unsigned int>(systemInfo.dwNumberOfProcessors));
    }();

    const auto processTicks = kernel.QuadPart + user.QuadPart;
    const auto wallTicks = now.QuadPart;
    if (previousWallTicks == 0 || wallTicks <= previousWallTicks || processTicks < previousProcessTicks)
    {
        previousProcessTicks = processTicks;
        previousWallTicks = wallTicks;
        return QStringLiteral("CPU --");
    }

    const auto processDelta = static_cast<double>(processTicks - previousProcessTicks);
    const auto wallDelta = static_cast<double>(wallTicks - previousWallTicks);
    previousProcessTicks = processTicks;
    previousWallTicks = wallTicks;

    if (wallDelta <= 0.0)
    {
        return QStringLiteral("CPU --");
    }

    const auto cpuPercent = std::clamp((processDelta / wallDelta) * (100.0 / processorCount), 0.0, 999.0);
    return QStringLiteral("CPU %1%").arg(cpuPercent, 0, 'f', 1);
#else
    return QStringLiteral("CPU --");
#endif
}
}

DebugUiController::DebugUiController(MainWindow& window)
    : m_window(window)
{
}

void DebugUiController::resetOutputFpsTracking()
{
    m_window.m_outputFpsTimer.invalidate();
    m_window.m_outputFpsFrameCount = 0;
    m_window.m_outputFps = 0.0;
}

void DebugUiController::updateFrame(const QImage& image, const int frameIndex, const double timestampSeconds)
{
    constexpr qint64 kPlaybackTimelineUiIntervalMs = 66;

    QElapsedTimer viewportTimer;
    viewportTimer.start();
    m_window.m_lastPresentedFrame = image;
    m_window.m_videoViewportQuickController->setPresentedFrame(image, m_window.m_controller->currentVideoFrame(), m_window.m_controller->videoFrameSize());
    if (m_window.m_detachedVideoViewportQuickController
        && m_window.m_detachedVideoWindow
        && m_window.m_detachedVideoWindow->isVisible())
    {
        m_window.m_detachedVideoViewportQuickController->setPresentedFrame(
            image,
            m_window.m_controller->currentVideoFrame(),
            m_window.m_controller->videoFrameSize());
    }
    if (m_window.m_nativeViewportWindow && m_window.m_nativeViewportWindow->isVisible() && m_window.m_nativeViewport)
    {
        m_window.m_nativeViewport->setPresentedFrame(
            image,
            m_window.m_controller->currentVideoFrame(),
            m_window.m_controller->videoFrameSize());
    }
    updateSmoothedMs(m_window.m_uiViewportUpdateMs, elapsedMs(viewportTimer));

    QElapsedTimer timelineTimer;
    timelineTimer.start();
    const auto displayFrameIndex = std::max(0, frameIndex);
    const auto displayTimestampSeconds = std::max(0.0, timestampSeconds);
    const bool playbackActive = m_window.m_controller->isPlaying();
    const bool timelineUiDue = !playbackActive
        || !m_window.m_timelinePlaybackUiTimer.isValid()
        || m_window.m_timelinePlaybackUiTimer.elapsed() >= kPlaybackTimelineUiIntervalMs
        || displayFrameIndex < m_window.m_lastTimelinePlaybackUiFrame;
    if (timelineUiDue)
    {
        m_window.setTimelineCurrentFrame(displayFrameIndex);
        m_window.m_timelinePlaybackUiTimer.restart();
        m_window.m_lastTimelinePlaybackUiFrame = displayFrameIndex;
        updateSmoothedMs(m_window.m_uiTimelineUpdateMs, elapsedMs(timelineTimer));
    }
    const auto frameText = QStringLiteral("Frame %1  |  %2 s")
        .arg(displayFrameIndex)
        .arg(displayTimestampSeconds, 0, 'f', 2);
    QElapsedTimer chromeTimer;
    chromeTimer.start();
    if (timelineUiDue && m_window.m_windowChromeController)
    {
        m_window.m_windowChromeController->setFrameText(frameText);
        updateSmoothedMs(m_window.m_uiChromeUpdateMs, elapsedMs(chromeTimer));
    }

    if (playbackActive)
    {
        if (!m_window.m_outputFpsTimer.isValid())
        {
            m_window.m_outputFpsTimer.start();
            m_window.m_outputFpsFrameCount = 0;
            m_window.m_outputFps = 0.0;
        }

        ++m_window.m_outputFpsFrameCount;
        const auto elapsedMs = std::max<qint64>(1, m_window.m_outputFpsTimer.elapsed());
        if (elapsedMs >= 1000)
        {
            m_window.m_outputFps =
                static_cast<double>(m_window.m_outputFpsFrameCount) * 1000.0 / static_cast<double>(elapsedMs);
            m_window.m_outputFpsTimer.restart();
            m_window.m_outputFpsFrameCount = 0;
        }
    }

    QElapsedTimer clipEditorTimer;
    clipEditorTimer.start();
    m_window.refreshClipEditor();
    updateSmoothedMs(m_window.m_uiClipEditorUpdateMs, elapsedMs(clipEditorTimer));

    QElapsedTimer debugTextTimer;
    debugTextTimer.start();
    updateDebugText();
    updateSmoothedMs(m_window.m_uiDebugTextUpdateMs, elapsedMs(debugTextTimer));
}

void DebugUiController::updateMemoryUsage()
{
    m_window.m_memoryUsageText = currentMemoryUsageText();
    m_window.m_processorUsageText = currentProcessorUsageText();
    m_window.m_gpuUsageText = currentGpuUsageText();
    m_window.m_videoMemoryUsageText = currentVideoMemoryUsageText();
    updateDebugText();
}

void DebugUiController::updateDebugText()
{
    if (!m_window.m_debugOverlay || !m_window.m_debugVisible)
    {
        return;
    }

    if (m_window.m_controller
        && m_window.m_controller->isPlaying()
        && m_window.m_debugTextTimer.isValid()
        && m_window.m_debugTextTimer.elapsed() < 250)
    {
        return;
    }

    const auto clipText = m_window.m_clipName.isEmpty() ? QStringLiteral("No clip") : m_window.m_clipName;
    const auto fpsText = m_window.m_controller->fps() > 0.0
        ? QString::number(m_window.m_controller->fps(), 'f', 2)
        : QStringLiteral("--");
    const auto outputFpsText = m_window.m_controller->isPlaying()
        ? (m_window.m_outputFps > 0.0
            ? QString::number(m_window.m_outputFps, 'f', 2)
            : (m_window.m_outputFpsTimer.isValid() && m_window.m_outputFpsTimer.elapsed() > 0
                ? QString::number(
                    static_cast<double>(m_window.m_outputFpsFrameCount) * 1000.0
                        / static_cast<double>(std::max<qint64>(1, m_window.m_outputFpsTimer.elapsed())),
                    'f',
                    2)
                : QStringLiteral("--")))
        : QStringLiteral("--");
    const auto insertionText = m_window.m_controller->isInsertionFollowsPlayback()
        ? QStringLiteral("On")
        : QStringLiteral("Off");
    const auto processorText = m_window.m_processorUsageText.isEmpty()
        ? QStringLiteral("CPU --")
        : m_window.m_processorUsageText;
    const auto memoryText = m_window.m_memoryUsageText.isEmpty()
        ? QStringLiteral("Memory --")
        : m_window.m_memoryUsageText;
    const auto gpuText = m_window.m_gpuUsageText.isEmpty()
        ? QStringLiteral("GPU --")
        : m_window.m_gpuUsageText;
    const auto videoMemoryText = m_window.m_videoMemoryUsageText.isEmpty()
        ? QStringLiteral("VRAM --")
        : m_window.m_videoMemoryUsageText;
    const auto decoderText = m_window.m_controller->decoderBackendName().isEmpty()
        ? QStringLiteral("Decode --")
        : QStringLiteral("Decode %1").arg(m_window.m_controller->decoderBackendName());
    const auto renderText = m_window.m_controller->renderBackendName().isEmpty()
        ? QStringLiteral("Render --")
        : QStringLiteral("Render %1").arg(m_window.m_controller->renderBackendName());
    const auto qtQuickText = QStringLiteral("Qt Quick %1")
        .arg(m_window.m_qtQuickGraphicsApiText.isEmpty()
                ? QStringLiteral("Unknown")
                : m_window.m_qtQuickGraphicsApiText);
    const auto qtQuickLoadText = QStringLiteral("QML Load %1")
        .arg(m_window.m_qtQuickLoadText.isEmpty()
                ? QStringLiteral("Unknown")
                : m_window.m_qtQuickLoadText);
    const auto playbackStats = m_window.m_controller->playbackDebugStats();
    const auto playbackPerfText = QStringLiteral(
        "Playback ms tick=%1 cb=%2 overlay=%3 build=%4 present=%5 ui=%6 audio=%7")
                                      .arg(playbackStats.advancePlaybackMs, 0, 'f', 1)
                                      .arg(playbackStats.frameCallbackMs, 0, 'f', 1)
                                      .arg(playbackStats.overlayRefreshMs, 0, 'f', 1)
                                      .arg(playbackStats.overlayBuildMs, 0, 'f', 1)
                                      .arg(playbackStats.presentFrameMs, 0, 'f', 1)
                                      .arg(playbackStats.frameReadyDispatchMs, 0, 'f', 1)
                                      .arg(playbackStats.syncAudioMs, 0, 'f', 1);
    const auto playbackQueueText = QStringLiteral(
        "Playback queue %1/%2 wait=%3ms fallback=%4 starve=%5")
                                       .arg(playbackStats.runtimeStats.queuedFrames)
                                       .arg(playbackStats.runtimeStats.prefetchTargetFrames)
                                       .arg(playbackStats.runtimeStats.lastStepWaitMs)
                                        .arg(playbackStats.runtimeStats.lastStepUsedSynchronousFallback
                                                 ? QStringLiteral("yes")
                                                 : QStringLiteral("no"))
                                       .arg(playbackStats.runtimeStats.queueStarvationCount);
    const auto uiPerfText = QStringLiteral(
        "UI ms viewport=%1 timeline=%2 chrome=%3 clip=%4 debug=%5")
                                .arg(m_window.m_uiViewportUpdateMs, 0, 'f', 1)
                                .arg(m_window.m_uiTimelineUpdateMs, 0, 'f', 1)
                                .arg(m_window.m_uiChromeUpdateMs, 0, 'f', 1)
                                .arg(m_window.m_uiClipEditorUpdateMs, 0, 'f', 1)
                                .arg(m_window.m_uiDebugTextUpdateMs, 0, 'f', 1);
    const auto overlayStats = VideoOverlayQuickItem::debugStats();
    const auto overlayPaintText = QStringLiteral(
        "Overlay ms paint=%1 nodes=%2 labels=%3 model=%4/%5")
                                      .arg(overlayStats.paintMs, 0, 'f', 1)
                                      .arg(overlayStats.overlayCount)
                                      .arg(overlayStats.labelCount)
                                      .arg(playbackStats.overlayCount)
                                      .arg(playbackStats.overlayLabelCount);
    const auto timelineThumbsText = m_window.m_timelineQuickController
        ? QStringLiteral("Timeline thumbs visible=%1 manifest=%2 tiles=%3 frames=%4 scroll=%5")
              .arg(m_window.m_timelineQuickController->thumbnailsVisible() ? QStringLiteral("yes") : QStringLiteral("no"))
              .arg(m_window.m_timelineQuickController->hasThumbnailManifest() ? QStringLiteral("yes") : QStringLiteral("no"))
              .arg(m_window.m_timelineQuickController->thumbnailTileCount())
              .arg(m_window.m_timelineQuickController->thumbnailFrameCount())
              .arg(m_window.m_timelineQuickController->lastCurrentFrameAutoScrolled() ? QStringLiteral("yes") : QStringLiteral("no"))
        : QStringLiteral("Timeline thumbs unavailable");

    m_window.m_debugOverlay->setListText(
        QStringLiteral(
            "Clip: %1\n"
            "Motion: %2\n"
            "Insert Follow: %3\n"
            "Video FPS: %4\n"
            "FPS Output: %5\n"
            "Nodes: %6\n"
            "Selected: %7\n"
            "%8\n"
            "%9\n"
            "%10\n"
            "%11\n"
            "%12\n"
            "%13\n"
            "%14\n"
            "%15\n"
            "%16\n"
            "%17\n"
            "%18\n"
            "%19\n"
            "%20")
            .arg(clipText)
            .arg(m_window.m_controller->isMotionTrackingEnabled() ? QStringLiteral("On") : QStringLiteral("Off"))
            .arg(insertionText)
            .arg(fpsText)
            .arg(outputFpsText)
            .arg(m_window.m_controller->trackCount())
            .arg(m_window.m_controller->hasSelection() ? QStringLiteral("Yes") : QStringLiteral("No"))
            .arg(processorText)
            .arg(gpuText)
            .arg(memoryText)
            .arg(videoMemoryText)
            .arg(decoderText)
            .arg(renderText)
            .arg(qtQuickText)
            .arg(qtQuickLoadText)
            .arg(playbackPerfText)
            .arg(playbackQueueText)
            .arg(uiPerfText)
            .arg(timelineThumbsText)
            .arg(overlayPaintText));
    m_window.m_debugTextTimer.restart();
}

void DebugUiController::handleVideoLoaded(const QString& filePath, const int totalFrames, const double fps)
{
    resetOutputFpsTracking();
    m_window.m_debugTextTimer.invalidate();
    const QFileInfo fileInfo{
        m_window.m_controller && !m_window.m_controller->projectVideoPath().isEmpty()
            ? m_window.m_controller->projectVideoPath()
            : filePath};
    m_window.m_clipName = fileInfo.fileName();
    if (filePath.isEmpty())
    {
        m_window.clearTimeline();
    }
    else
    {
        m_window.setTimelineVideoPath(
            m_window.m_controller && !m_window.m_controller->projectVideoPath().isEmpty()
                ? m_window.m_controller->projectVideoPath()
                : filePath);
        m_window.setTimelineState(totalFrames, fps);
    }
    if (m_window.m_nativeViewportWindow)
    {
        m_window.m_nativeViewportWindow->setWindowTitle(
            QStringLiteral("Native Video Viewport Test - %1").arg(m_window.m_clipName));
    }
    if (m_window.m_nativeViewport)
    {
        m_window.m_nativeViewport->setPresentedFrame(
            m_window.m_lastPresentedFrame,
            m_window.m_controller->currentVideoFrame(),
            m_window.m_controller->videoFrameSize());
        m_window.m_nativeViewport->setOverlays(m_window.m_controller->currentOverlays());
    }
    m_window.refreshTimeline();
    m_window.refreshClipEditor();
    if (!filePath.isEmpty())
    {
        m_window.showCanvasTipsOverlay();
    }
    updateDebugText();
    m_window.updateWindowTitle();
}

void DebugUiController::updateDebugVisibility(const bool enabled)
{
    m_window.m_debugVisible = enabled;
    m_window.m_debugTextTimer.invalidate();
    if (m_window.m_debugOverlay)
    {
        m_window.m_debugOverlay->setVisible(enabled);
    }
    if (m_window.m_toggleDebugAction && m_window.m_toggleDebugAction->isChecked() != enabled)
    {
        m_window.m_toggleDebugAction->setChecked(enabled);
    }
}

void DebugUiController::updateNativeViewportVisibility(const bool visible)
{
    if (!m_window.m_nativeViewportWindow)
    {
        return;
    }

    if (visible)
    {
        if (m_window.m_nativeViewport)
        {
            m_window.m_nativeViewport->setPresentedFrame(
                m_window.m_lastPresentedFrame,
                m_window.m_controller->currentVideoFrame(),
                m_window.m_controller->videoFrameSize());
            m_window.m_nativeViewport->setOverlays(m_window.m_controller->currentOverlays());
            m_window.m_nativeViewport->setShowAllLabels(
                m_window.m_showAllNodeNamesAction && m_window.m_showAllNodeNamesAction->isChecked());
        }

        m_window.m_nativeViewportWindow->show();
        m_window.m_nativeViewportWindow->raise();
        m_window.m_nativeViewportWindow->activateWindow();
    }
    else
    {
        m_window.m_nativeViewportWindow->hide();
    }

    if (m_window.m_showNativeViewportAction && m_window.m_showNativeViewportAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_window.m_showNativeViewportAction};
        m_window.m_showNativeViewportAction->setChecked(visible);
    }
}
