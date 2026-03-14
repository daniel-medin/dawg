#include "app/MainWindow.h"

#include <algorithm>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDrag>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScreen>
#include <QSizePolicy>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QWidgetAction>
#include <QVBoxLayout>
#include <QWidget>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dxgi1_4.h>
#include <psapi.h>
#endif

#include "app/PlayerController.h"
#include "ui/DebugOverlayWindow.h"
#include "ui/TimelineView.h"
#include "ui/VideoCanvas.h"

namespace
{
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
#endif

    return QStringLiteral("Memory --");
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
#endif

    return QStringLiteral("VRAM --");
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
#endif

    return QStringLiteral("CPU --");
}

class AudioPoolRow final : public QWidget
{
public:
    explicit AudioPoolRow(const QString& assetPath, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_assetPath(assetPath)
    {
        setCursor(Qt::OpenHandCursor);
        setMouseTracking(true);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            m_dragStartPosition = event->position().toPoint();
            setCursor(Qt::ClosedHandCursor);
        }

        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!(event->buttons() & Qt::LeftButton))
        {
            QWidget::mouseMoveEvent(event);
            return;
        }

        if ((event->position().toPoint() - m_dragStartPosition).manhattanLength() < QApplication::startDragDistance())
        {
            QWidget::mouseMoveEvent(event);
            return;
        }

        auto* mimeData = new QMimeData();
        mimeData->setData("application/x-dawg-audio-path", m_assetPath.toUtf8());
        mimeData->setText(m_assetPath);
        mimeData->setUrls({QUrl::fromLocalFile(m_assetPath)});

        auto* drag = new QDrag(this);
        drag->setMimeData(mimeData);
        drag->exec(Qt::CopyAction, Qt::CopyAction);
        setCursor(Qt::OpenHandCursor);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        setCursor(Qt::OpenHandCursor);
        QWidget::mouseReleaseEvent(event);
    }

private:
    QString m_assetPath;
    QPoint m_dragStartPosition;
};
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_controller(new PlayerController(this))
{
    buildUi();
    buildMenus();
    qApp->installEventFilter(this);
    m_clearAllShortcutTimer.setSingleShot(true);
    m_clearAllShortcutTimer.setInterval(1500);
    m_memoryUsageTimer.setInterval(1000);
    connect(&m_clearAllShortcutTimer, &QTimer::timeout, this, &MainWindow::clearPendingClearAllShortcut);
    connect(&m_memoryUsageTimer, &QTimer::timeout, this, &MainWindow::updateMemoryUsage);

    connect(m_openAction, &QAction::triggered, this, &MainWindow::openVideo);
    connect(m_importSoundAction, &QAction::triggered, this, &MainWindow::importSound);
    connect(m_showTimelineAction, &QAction::toggled, this, [this](const bool visible)
    {
        updateTimelineVisibility(visible);
        showStatus(visible ? QStringLiteral("Timeline shown.") : QStringLiteral("Timeline hidden."));
    });
    connect(m_timelineClickSeeksAction, &QAction::toggled, this, [this](const bool enabled)
    {
        m_timeline->setSeekOnClickEnabled(enabled);
        showStatus(
            enabled
                ? QStringLiteral("Timeline click seek enabled.")
                : QStringLiteral("Timeline click seek disabled. Use play or scrub to move."));
    });
    connect(m_audioPoolAction, &QAction::toggled, this, [this](const bool visible)
    {
        updateAudioPoolVisibility(visible);
        showStatus(visible ? QStringLiteral("Audio Pool shown.") : QStringLiteral("Audio Pool hidden."));
    });
    connect(
        m_motionTrackingAction,
        &QAction::toggled,
        m_controller,
        &PlayerController::setMotionTrackingEnabled);
    connect(
        m_insertionFollowsPlaybackAction,
        &QAction::toggled,
        m_controller,
        &PlayerController::setInsertionFollowsPlayback);
    connect(m_goToStartAction, &QAction::triggered, m_controller, &PlayerController::goToStart);
    connect(m_playAction, &QAction::triggered, m_controller, &PlayerController::togglePlayback);
    connect(m_stepForwardAction, &QAction::triggered, m_controller, &PlayerController::stepForward);
    connect(m_stepBackAction, &QAction::triggered, m_controller, &PlayerController::stepBackward);
    connect(m_selectAllAction, &QAction::triggered, m_controller, &PlayerController::selectAllVisibleTracks);
    connect(m_unselectAllAction, &QAction::triggered, m_controller, &PlayerController::clearSelection);
    connect(
        m_setNodeStartAction,
        &QAction::triggered,
        this,
        &MainWindow::handleNodeStartShortcut);
    connect(
        m_setNodeEndAction,
        &QAction::triggered,
        this,
        &MainWindow::handleNodeEndShortcut);
    connect(m_trimNodeAction, &QAction::triggered, this, &MainWindow::trimSelectedNodeToSound);
    connect(m_autoPanAction, &QAction::triggered, this, &MainWindow::toggleSelectedNodeAutoPan);
    connect(m_toggleNodeNameAction, &QAction::triggered, m_controller, &PlayerController::toggleSelectedTrackLabels);
    connect(m_showAllNodeNamesAction, &QAction::toggled, this, [this](const bool enabled)
    {
        m_canvas->setShowAllLabels(enabled);
        showStatus(
            enabled
                ? QStringLiteral("Node names always visible.")
                : QStringLiteral("Node names only show when relevant."));
    });
    connect(m_deleteNodeAction, &QAction::triggered, m_controller, &PlayerController::deleteSelectedTrack);
    connect(m_clearAllAction, &QAction::triggered, m_controller, &PlayerController::clearAllTracks);
    connect(m_canvas, &VideoCanvas::seedPointRequested, m_controller, &PlayerController::seedTrack);
    connect(m_canvas, &VideoCanvas::audioDropped, this, [this](const QString& assetPath, const QPointF& imagePoint)
    {
        m_controller->createTrackWithAudioAtCurrentFrame(assetPath, imagePoint);
    });
    connect(m_canvas, &VideoCanvas::tracksSelected, m_controller, &PlayerController::selectTracks);
    connect(m_canvas, &VideoCanvas::trackSelected, m_controller, &PlayerController::selectTrack);
    connect(m_canvas, &VideoCanvas::trackContextMenuRequested, this, [this](const QUuid& trackId, const QPoint& globalPosition)
    {
        showNodeContextMenu(trackId, globalPosition, true);
    });
    connect(m_canvas, &VideoCanvas::selectedTrackMoved, m_controller, &PlayerController::moveSelectedTrack);
    connect(m_timeline, &TimelineView::frameRequested, m_controller, &PlayerController::seekToFrame);
    connect(m_timeline, &TimelineView::trackSelected, m_controller, &PlayerController::selectTrack);
    connect(m_timeline, &TimelineView::trackStartFrameRequested, m_controller, &PlayerController::setTrackStartFrame);
    connect(m_timeline, &TimelineView::trackEndFrameRequested, m_controller, &PlayerController::setTrackEndFrame);
    connect(m_timeline, &TimelineView::trackSpanMoveRequested, m_controller, &PlayerController::moveTrackFrameSpan);
    connect(m_timeline, &TimelineView::trackContextMenuRequested, this, [this](const QUuid& trackId, const QPoint& globalPosition)
    {
        showNodeContextMenu(trackId, globalPosition, false);
    });
    connect(m_toggleDebugAction, &QAction::toggled, this, [this](const bool enabled)
    {
        updateDebugVisibility(enabled);
        showStatus(enabled ? QStringLiteral("Debug info shown.") : QStringLiteral("Debug info hidden."));
    });
    connect(m_playPauseShortcut, &QShortcut::activated, m_controller, &PlayerController::togglePlayback);
    connect(m_startShortcut, &QShortcut::activated, m_controller, &PlayerController::goToStart);
    connect(m_numpadStartShortcut, &QShortcut::activated, m_controller, &PlayerController::goToStart);
    connect(m_stepBackShortcut, &QShortcut::activated, m_controller, &PlayerController::stepBackward);
    connect(m_stepForwardShortcut, &QShortcut::activated, m_controller, &PlayerController::stepForward);
    connect(
        m_insertionFollowsPlaybackShortcut,
        &QShortcut::activated,
        m_insertionFollowsPlaybackAction,
        &QAction::trigger);
    connect(m_selectAllShortcut, &QShortcut::activated, m_controller, &PlayerController::selectAllVisibleTracks);
    connect(
        m_nodeStartShortcut,
        &QShortcut::activated,
        this,
        &MainWindow::handleNodeStartShortcut);
    connect(
        m_nodeEndShortcut,
        &QShortcut::activated,
        this,
        &MainWindow::handleNodeEndShortcut);
    connect(m_showTimelineShortcut, &QShortcut::activated, m_showTimelineAction, &QAction::trigger);
    connect(m_trimNodeShortcut, &QShortcut::activated, this, &MainWindow::trimSelectedNodeToSound);
    connect(m_autoPanShortcut, &QShortcut::activated, this, &MainWindow::toggleSelectedNodeAutoPan);
    connect(m_audioPoolShortcut, &QShortcut::activated, m_audioPoolAction, &QAction::trigger);
    connect(m_toggleNodeNameShortcut, &QShortcut::activated, m_controller, &PlayerController::toggleSelectedTrackLabels);
    connect(m_deleteShortcut, &QShortcut::activated, m_controller, &PlayerController::deleteSelectedTrack);
    connect(m_unselectAllShortcut, &QShortcut::activated, m_controller, &PlayerController::clearSelection);
    connect(m_controller, &PlayerController::frameReady, this, &MainWindow::updateFrame);
    connect(m_controller, &PlayerController::overlaysChanged, this, &MainWindow::refreshOverlays);
    connect(
        m_controller,
        &PlayerController::insertionFollowsPlaybackChanged,
        this,
        &MainWindow::updateInsertionFollowsPlaybackState);
    connect(m_controller, &PlayerController::playbackStateChanged, this, &MainWindow::updatePlaybackState);
    connect(
        m_controller,
        &PlayerController::motionTrackingChanged,
        this,
        &MainWindow::updateMotionTrackingState);
    connect(m_controller, &PlayerController::selectionChanged, this, &MainWindow::updateSelectionState);
    connect(m_controller, &PlayerController::trackAvailabilityChanged, this, &MainWindow::updateTrackAvailabilityState);
    connect(m_controller, &PlayerController::audioPoolChanged, this, &MainWindow::refreshAudioPool);
    connect(m_controller, &PlayerController::videoLoaded, this, &MainWindow::handleVideoLoaded);
    connect(m_controller, &PlayerController::videoAudioStateChanged, this, &MainWindow::updateVideoAudioRow);
    connect(m_controller, &PlayerController::statusChanged, this, &MainWindow::showStatus);
    connect(m_audioPoolCloseButton, &QPushButton::clicked, this, [this]()
    {
        updateAudioPoolVisibility(false);
        showStatus(QStringLiteral("Audio Pool hidden."));
    });

    updatePlaybackState(false);
    updateInsertionFollowsPlaybackState(m_controller->isInsertionFollowsPlayback());
    syncMotionTrackingUi(m_controller->isMotionTrackingEnabled());
    updateSelectionState(m_controller->hasSelection());
    updateTrackAvailabilityState(m_controller->hasTracks());
    updateMemoryUsage();
    updateDebugVisibility(true);
    updateDebugText();
    refreshAudioPool();
    updateVideoAudioRow();
    m_memoryUsageTimer.start();
    showStatus(QStringLiteral("Open a clip to start adding nodes."));
    tryOpenLocalDevVideo();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress)
    {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (!keyEvent->isAutoRepeat())
        {
            const auto modifiers = keyEvent->modifiers();
            const auto key = keyEvent->key();

            if (key == Qt::Key_A && modifiers == (Qt::ControlModifier | Qt::ShiftModifier))
            {
                armClearAllShortcut();
                return true;
            }

            if (key == Qt::Key_Backspace && m_clearAllShortcutArmed)
            {
                clearPendingClearAllShortcut();
                if (m_controller->hasTracks())
                {
                    m_controller->clearAllTracks();
                }
                return true;
            }

            if (m_clearAllShortcutArmed
                && key != Qt::Key_Control
                && key != Qt::Key_Shift
                && key != Qt::Key_Alt
                && key != Qt::Key_Meta)
            {
                clearPendingClearAllShortcut();
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::openVideo()
{
    const auto filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open Video"),
        {},
        QStringLiteral("Video Files (*.mp4 *.mov *.mkv *.avi);;All Files (*.*)"));

    if (filePath.isEmpty())
    {
        return;
    }

    if (m_controller->openVideo(filePath))
    {
        populateAudioPoolFromLocalDevDirectory();
    }
}

void MainWindow::importSound()
{
    if (!m_controller->hasSelection())
    {
        showStatus(QStringLiteral("Select a node before importing sound."));
        return;
    }

    const auto filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import Sound"),
        {},
        QStringLiteral("Audio Files (*.wav *.mp3 *.flac *.aif *.aiff *.m4a *.aac *.ogg);;All Files (*.*)"));

    if (filePath.isEmpty())
    {
        return;
    }

    m_controller->importSoundForSelectedTrack(filePath);
}

void MainWindow::importAudioToPool()
{
    const auto filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import Audio To Pool"),
        {},
        QStringLiteral("Audio Files (*.wav *.mp3 *.flac *.aif *.aiff *.m4a *.aac *.ogg);;All Files (*.*)"));

    if (filePath.isEmpty())
    {
        return;
    }

    if (m_controller->importAudioToPool(filePath))
    {
        showStatus(QStringLiteral("Imported %1 to the audio pool.").arg(QFileInfo(filePath).fileName()));
    }
}

void MainWindow::handleNodeStartShortcut()
{
    if (shouldApplyNodeShortcutToAll())
    {
        m_controller->setAllTracksStartToCurrentFrame();
        return;
    }

    m_controller->setSelectedTrackStartToCurrentFrame();
}

void MainWindow::handleNodeEndShortcut()
{
    if (shouldApplyNodeShortcutToAll())
    {
        m_controller->setAllTracksEndToCurrentFrame();
        return;
    }

    m_controller->setSelectedTrackEndToCurrentFrame();
}

void MainWindow::trimSelectedNodeToSound()
{
    m_controller->trimSelectedTracksToAttachedSound();
}

void MainWindow::toggleSelectedNodeAutoPan()
{
    m_controller->toggleSelectedTrackAutoPan();
    if (m_autoPanAction)
    {
        const QSignalBlocker blocker{m_autoPanAction};
        m_autoPanAction->setChecked(m_controller->hasSelection() && m_controller->selectedTracksAutoPanEnabled());
    }
}

void MainWindow::updateFrame(const QImage& image, const int frameIndex, const double timestampSeconds)
{
    m_canvas->setFrame(image);
    m_timeline->setCurrentFrame(frameIndex);
    m_frameLabel->setText(
        QStringLiteral("Frame %1  |  %2 s")
            .arg(frameIndex)
            .arg(timestampSeconds, 0, 'f', 2));
    if (m_audioPoolPanel && m_audioPoolPanel->isVisible())
    {
        updateAudioPoolPlaybackIndicators();
    }
    updateDebugText();
}

void MainWindow::updateMemoryUsage()
{
    m_memoryUsageText = currentMemoryUsageText();
    m_processorUsageText = currentProcessorUsageText();
    m_videoMemoryUsageText = currentVideoMemoryUsageText();
    updateDebugText();
}

void MainWindow::updateDebugText()
{
    if (!m_debugOverlay)
    {
        return;
    }

    const auto totalFrames = std::max(0, m_controller->totalFrames());
    const auto currentFrame = m_controller->hasVideoLoaded() ? m_controller->currentFrameIndex() : 0;
    const auto currentSeconds = m_controller->fps() > 0.0
        ? static_cast<double>(currentFrame) / m_controller->fps()
        : 0.0;
    const auto clipText = m_clipName.isEmpty() ? QStringLiteral("No clip") : m_clipName;
    const auto fpsText = m_controller->fps() > 0.0
        ? QString::number(m_controller->fps(), 'f', 2)
        : QStringLiteral("--");
    const auto insertionText = m_controller->isInsertionFollowsPlayback()
        ? QStringLiteral("On")
        : QStringLiteral("Off");
    const auto processorText = m_processorUsageText.isEmpty() ? QStringLiteral("CPU --") : m_processorUsageText;
    const auto memoryText = m_memoryUsageText.isEmpty() ? QStringLiteral("Memory --") : m_memoryUsageText;
    const auto videoMemoryText = m_videoMemoryUsageText.isEmpty() ? QStringLiteral("VRAM --") : m_videoMemoryUsageText;
    const auto decoderText = m_controller->decoderBackendName().isEmpty()
        ? QStringLiteral("Decode --")
        : QStringLiteral("Decode %1").arg(m_controller->decoderBackendName());
    const auto renderText = m_controller->renderBackendName().isEmpty()
        ? QStringLiteral("Render --")
        : QStringLiteral("Render %1").arg(m_controller->renderBackendName());

    m_debugOverlay->setListText(
        QStringLiteral(
            "Clip: %1\n"
            "Motion: %2\n"
            "Insert Follow: %3\n"
            "Frame: %4 / %5\n"
            "Time: %6 s\n"
            "FPS: %7\n"
            "Nodes: %8\n"
            "Selected: %9\n"
            "%10\n"
            "%11\n"
            "%12\n"
            "%13\n"
            "%14")
            .arg(clipText)
            .arg(m_controller->isMotionTrackingEnabled() ? QStringLiteral("On") : QStringLiteral("Off"))
            .arg(insertionText)
            .arg(currentFrame)
            .arg(totalFrames)
            .arg(currentSeconds, 0, 'f', 2)
            .arg(fpsText)
            .arg(m_controller->trackCount())
            .arg(m_controller->hasSelection() ? QStringLiteral("Yes") : QStringLiteral("No"))
            .arg(processorText)
            .arg(memoryText)
            .arg(videoMemoryText)
            .arg(decoderText)
            .arg(renderText));
}

void MainWindow::refreshOverlays()
{
    m_canvas->setOverlays(m_controller->currentOverlays());
    refreshTimeline();
}

void MainWindow::updateInsertionFollowsPlaybackState(const bool enabled)
{
    if (m_insertionFollowsPlaybackAction && m_insertionFollowsPlaybackAction->isChecked() != enabled)
    {
        m_insertionFollowsPlaybackAction->setChecked(enabled);
    }
    updateDebugText();
}

void MainWindow::updatePlaybackState(const bool playing)
{
    const auto label = playing ? QStringLiteral("Pause (Space)") : QStringLiteral("Play (Space)");
    m_playAction->setText(label);
    if (m_audioPoolPanel && m_audioPoolPanel->isVisible())
    {
        updateAudioPoolPlaybackIndicators();
    }
    updateDebugText();
}

void MainWindow::updateMotionTrackingState(const bool enabled)
{
    syncMotionTrackingUi(enabled);
    m_motionTrackingAction->setChecked(enabled);
    updateDebugText();
}

void MainWindow::updateSelectionState(const bool hasSelection)
{
    m_unselectAllAction->setEnabled(hasSelection);
    m_setNodeStartAction->setEnabled(hasSelection);
    m_setNodeEndAction->setEnabled(hasSelection);
    m_trimNodeAction->setEnabled(hasSelection);
    m_autoPanAction->setEnabled(hasSelection);
    if (m_autoPanAction)
    {
        const QSignalBlocker blocker{m_autoPanAction};
        m_autoPanAction->setChecked(hasSelection && m_controller->selectedTracksAutoPanEnabled());
    }
    m_toggleNodeNameAction->setEnabled(hasSelection);
    m_importSoundAction->setEnabled(hasSelection);
    m_deleteNodeAction->setEnabled(hasSelection);
    updateDebugText();
}

void MainWindow::updateTrackAvailabilityState(const bool hasTracks)
{
    m_selectAllAction->setEnabled(hasTracks);
    m_clearAllAction->setEnabled(hasTracks);
    if (!hasTracks)
    {
        clearPendingClearAllShortcut();
    }
    updateDebugText();
}

void MainWindow::handleVideoLoaded(const QString& filePath, const int totalFrames, const double fps)
{
    const QFileInfo fileInfo{filePath};
    m_clipName = fileInfo.fileName();
    m_timeline->setTimeline(totalFrames, fps);
    refreshTimeline();
    updateDebugText();
}

void MainWindow::updateDebugVisibility(const bool enabled)
{
    m_debugVisible = enabled;
    if (m_debugOverlay)
    {
        m_debugOverlay->setVisible(enabled);
        if (enabled)
        {
            m_debugOverlay->raise();
        }
    }
    if (m_toggleDebugAction && m_toggleDebugAction->isChecked() != enabled)
    {
        m_toggleDebugAction->setChecked(enabled);
    }
}

void MainWindow::updateAudioPoolVisibility(const bool visible)
{
    if (m_audioPoolPanel)
    {
        if (!visible)
        {
            m_audioPoolPreferredWidth = std::max(240, m_audioPoolPanel->width());
        }
        m_audioPoolPanel->setVisible(visible);
    }

    if (visible && m_contentSplitter && m_audioPoolPanel)
    {
        const auto totalWidth = std::max(800, m_contentSplitter->width());
        const auto poolWidth = std::clamp(m_audioPoolPreferredWidth, 240, std::max(240, totalWidth / 2));
        m_contentSplitter->setSizes({std::max(400, totalWidth - poolWidth), poolWidth});
    }

    if (m_audioPoolAction && m_audioPoolAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_audioPoolAction};
        m_audioPoolAction->setChecked(visible);
    }
}

void MainWindow::updateTimelineVisibility(const bool visible)
{
    if (m_timelinePanel)
    {
        if (!visible)
        {
            m_timelinePreferredHeight = std::max(96, m_timelinePanel->height());
        }
        m_timelinePanel->setVisible(visible);
    }

    if (visible && m_mainVerticalSplitter && m_timelinePanel)
    {
        const auto totalHeight = std::max(320, m_mainVerticalSplitter->height());
        const auto timelineHeight = std::clamp(m_timelinePreferredHeight, 96, std::max(96, totalHeight / 2));
        m_mainVerticalSplitter->setSizes({std::max(200, totalHeight - timelineHeight), timelineHeight});
    }

    if (m_showTimelineAction && m_showTimelineAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_showTimelineAction};
        m_showTimelineAction->setChecked(visible);
    }
}

void MainWindow::refreshAudioPool()
{
    if (!m_audioPoolListLayout)
    {
        return;
    }

    while (auto* item = m_audioPoolListLayout->takeAt(0))
    {
        if (auto* widget = item->widget())
        {
            widget->deleteLater();
        }
        delete item;
    }

    const auto items = m_controller->audioPoolItems();
    if (items.empty())
    {
        auto* emptyLabel = new QLabel(QStringLiteral("No imported sounds yet."), m_audioPoolListContainer);
        emptyLabel->setStyleSheet(QStringLiteral("color: #8d9aae; font-size: 9pt;"));
        m_audioPoolListLayout->addWidget(emptyLabel);
        m_audioPoolListLayout->addStretch(1);
        return;
    }

    for (const auto& item : items)
    {
        auto* row = new AudioPoolRow(item.assetPath, m_audioPoolListContainer);
        row->setProperty("audioPoolItemKey", item.key);
        row->setProperty("assetPath", item.assetPath);
        row->setToolTip(item.connectionSummary);
        row->setStyleSheet(QStringLiteral(
            "QWidget { background: transparent; border: none; }"
            "QWidget:hover { background: rgba(255, 255, 255, 0.03); border-radius: 4px; }"));

        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(2, 0, 2, 0);
        rowLayout->setSpacing(6);

        auto* statusDot = new QLabel(row);
        statusDot->setObjectName(QStringLiteral("audioPoolStatusDot"));
        statusDot->setFixedSize(8, 8);
        statusDot->setAttribute(Qt::WA_TransparentForMouseEvents);
        statusDot->setStyleSheet(
            item.isPlaying
                ? QStringLiteral("background: #63c987; border-radius: 4px;")
                : (item.connectedNodeCount > 0
                    ? QStringLiteral("background: #d88932; border-radius: 4px;")
                    : QStringLiteral("background: #cf5f5f; border-radius: 4px;")));

        auto* nameLabel = new QLabel(item.displayName, row);
        nameLabel->setObjectName(QStringLiteral("audioPoolNameLabel"));
        nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        nameLabel->setStyleSheet(
            item.connectedNodeCount > 0
                ? QStringLiteral("color: #d8e0ea; font-size: 8.5pt; padding: 0; margin: 0;")
                : QStringLiteral("color: #9ea9b7; font-size: 8.5pt; padding: 0; margin: 0;"));
        nameLabel->setToolTip(item.connectionSummary);

        auto* editButton = new QToolButton(row);
        editButton->setCursor(Qt::PointingHandCursor);
        editButton->setAutoRaise(true);
        editButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
        editButton->setToolTip(QStringLiteral("Edit %1").arg(item.displayName));
        editButton->setStyleSheet(QStringLiteral(
            "QToolButton { background: transparent; border: none; padding: 0px; }"
            "QToolButton::menu-indicator { image: none; width: 0px; }"
            "QToolButton:hover { background: rgba(255, 255, 255, 0.06); border-radius: 4px; }"));

        auto* editMenu = new QMenu(editButton);
        editMenu->setStyleSheet(QStringLiteral(
            "QMenu { background: #080a0d; color: #eef2f6; border: 1px solid #1f2935; padding: 4px 0; }"
            "QMenu::item { padding: 6px 14px; }"
            "QMenu::item:selected { background: #1a2028; }"));
        auto* addAction = editMenu->addAction(QStringLiteral("Add to Frame"));
        auto* deleteAudioAction = editMenu->addAction(QStringLiteral("Delete Audio"));
        auto* deleteAudioAndNodesAction = editMenu->addAction(QStringLiteral("Delete Audio + Nodes"));

        connect(addAction, &QAction::triggered, this, [this, assetPath = item.assetPath]()
        {
            m_controller->createTrackWithAudioAtCurrentFrame(assetPath);
        });
        connect(deleteAudioAction, &QAction::triggered, this, [this, assetPath = item.assetPath]()
        {
            m_controller->removeAudioFromPool(assetPath);
        });
        connect(deleteAudioAndNodesAction, &QAction::triggered, this, [this, assetPath = item.assetPath]()
        {
            m_controller->removeAudioAndConnectedNodesFromPool(assetPath);
        });
        connect(editButton, &QToolButton::clicked, this, [editButton, editMenu]()
        {
            const auto menuSize = editMenu->sizeHint();
            auto popupPosition = editButton->mapToGlobal(QPoint(editButton->width() - menuSize.width(), editButton->height()));

            if (auto* screen = QGuiApplication::screenAt(popupPosition))
            {
                const auto available = screen->availableGeometry();
                popupPosition.setX(std::clamp(popupPosition.x(), available.left(), available.right() - menuSize.width()));
                popupPosition.setY(std::clamp(popupPosition.y(), available.top(), available.bottom() - menuSize.height()));
            }

            editMenu->exec(popupPosition);
        });

        rowLayout->addWidget(statusDot, 0, Qt::AlignVCenter);
        rowLayout->addWidget(nameLabel, 1);
        rowLayout->addWidget(editButton, 0, Qt::AlignVCenter);
        m_audioPoolListLayout->addWidget(row);
    }

    m_audioPoolListLayout->addStretch(1);
}

void MainWindow::updateAudioPoolPlaybackIndicators()
{
    if (!m_audioPoolListContainer)
    {
        return;
    }

    const auto items = m_controller->audioPoolItems();
    const auto rowObjects = m_audioPoolListContainer->children();

    for (QObject* child : rowObjects)
    {
        auto* row = qobject_cast<QWidget*>(child);
        if (!row || !row->property("assetPath").isValid())
        {
            continue;
        }

        const auto itemKey = row->property("audioPoolItemKey").toString();
        const auto itemIt = std::find_if(
            items.begin(),
            items.end(),
            [&itemKey](const AudioPoolItem& item)
            {
                return item.key == itemKey;
            });
        if (itemIt == items.end())
        {
            continue;
        }

        row->setToolTip(itemIt->connectionSummary);

        if (auto* statusDot = row->findChild<QLabel*>(QStringLiteral("audioPoolStatusDot"), Qt::FindDirectChildrenOnly))
        {
            statusDot->setStyleSheet(
                itemIt->isPlaying
                    ? QStringLiteral("background: #63c987; border-radius: 4px;")
                    : (itemIt->connectedNodeCount > 0
                        ? QStringLiteral("background: #d88932; border-radius: 4px;")
                        : QStringLiteral("background: #cf5f5f; border-radius: 4px;")));
        }

        if (auto* nameLabel = row->findChild<QLabel*>(QStringLiteral("audioPoolNameLabel"), Qt::FindDirectChildrenOnly))
        {
            nameLabel->setToolTip(itemIt->connectionSummary);
        }
    }
}

void MainWindow::updateVideoAudioRow()
{
    if (!m_videoAudioRow || !m_videoAudioLabel || !m_videoAudioMuteButton)
    {
        return;
    }

    const auto hasVideoAudio = m_controller->hasEmbeddedVideoAudio();
    m_videoAudioRow->setVisible(hasVideoAudio);
    if (!hasVideoAudio)
    {
        return;
    }

    const auto displayName = m_controller->embeddedVideoAudioDisplayName();
    m_videoAudioLabel->setText(displayName);
    m_videoAudioLabel->setToolTip(QStringLiteral("Embedded audio from %1").arg(displayName));

    const auto muted = m_controller->isEmbeddedVideoAudioMuted();
    m_videoAudioMuteButton->setIcon(
        style()->standardIcon(muted ? QStyle::SP_MediaVolumeMuted : QStyle::SP_MediaVolume));
    m_videoAudioMuteButton->setToolTip(
        muted
            ? QStringLiteral("Unmute video audio")
            : QStringLiteral("Mute video audio"));
}

void MainWindow::showStatus(const QString& message)
{
    statusBar()->showMessage(message, 5000);
}

void MainWindow::showNodeContextMenu(const QUuid& trackId, const QPoint& globalPosition, const bool includeSoundActions)
{
    if (trackId.isNull())
    {
        return;
    }

    m_controller->selectTrack(trackId);

    const auto nodeLabel = m_controller->trackLabel(trackId).isEmpty()
        ? QStringLiteral("Node")
        : m_controller->trackLabel(trackId);
    const auto hasAttachedAudio = m_controller->trackHasAttachedAudio(trackId);

    QMenu menu(this);
    auto* renameEditor = new QLineEdit(nodeLabel, &menu);
    renameEditor->setPlaceholderText(QStringLiteral("Node name"));
    renameEditor->setFrame(false);
    renameEditor->setFocusPolicy(Qt::ClickFocus);
    renameEditor->setCursorPosition(0);
    renameEditor->deselect();
    renameEditor->setStyleSheet(R"(
        QLineEdit {
            background: transparent;
            color: #f3f5f7;
            border: none;
            padding: 4px 8px;
            selection-background-color: #223146;
            selection-color: #f3f5f7;
        }
        QLineEdit:focus {
            background: #1a2330;
            border: 1px solid #324155;
            border-radius: 4px;
            padding: 3px 7px;
        }
    )");
    auto* renameAction = new QWidgetAction(&menu);
    renameAction->setDefaultWidget(renameEditor);
    menu.addAction(renameAction);
    menu.addSeparator();
    auto* importAudioAction = menu.addAction(QStringLiteral("Import Audio..."));
    QAction* trimAction = nullptr;
    QAction* autoPanAction = nullptr;
    if (hasAttachedAudio)
    {
        trimAction = menu.addAction(QStringLiteral("Trim Node (Shift+T)"));
    }
    if (includeSoundActions)
    {
        autoPanAction = menu.addAction(QStringLiteral("Auto Pan (R)"));
        autoPanAction->setCheckable(true);
        autoPanAction->setChecked(m_controller->trackAutoPanEnabled(trackId));
    }

    const QFontMetrics metrics{renameEditor->font()};
    const auto labelWidth = metrics.horizontalAdvance(nodeLabel);
    const auto menuContentWidth = std::clamp(labelWidth + 48, 220, 720);
    renameEditor->setMinimumWidth(menuContentWidth);
    menu.setMinimumWidth(menuContentWidth + 24);
    menu.setActiveAction(importAudioAction);

    connect(renameEditor, &QLineEdit::returnPressed, &menu, [&menu, this, trackId, renameEditor, nodeLabel]()
    {
        const auto updatedLabel = renameEditor->text().trimmed();
        if (!updatedLabel.isEmpty() && updatedLabel != nodeLabel)
        {
            m_controller->renameTrack(trackId, updatedLabel);
        }
        menu.close();
    });

    const auto* chosenAction = menu.exec(globalPosition);
    if (chosenAction == importAudioAction)
    {
        importSound();
        return;
    }

    if (chosenAction == trimAction)
    {
        trimSelectedNodeToSound();
        return;
    }

    if (chosenAction == autoPanAction)
    {
        toggleSelectedNodeAutoPan();
    }
}

void MainWindow::refreshTimeline()
{
    if (!m_timeline)
    {
        return;
    }

    m_timeline->setTrackSpans(m_controller->timelineTrackSpans());
}

bool MainWindow::shouldApplyNodeShortcutToAll() const
{
    return m_timeline
        && m_timeline->hasFocus()
        && !m_controller->hasSelection()
        && m_controller->hasTracks();
}

void MainWindow::buildMenus()
{
    m_openAction = new QAction(QStringLiteral("Open Video"), this);
    m_openAction->setShortcut(QKeySequence::Open);

    m_goToStartAction = new QAction(QStringLiteral("Start (Enter)"), this);
    m_playAction = new QAction(QStringLiteral("Play (Space)"), this);
    m_stepForwardAction = new QAction(QStringLiteral("Step Forward (.)"), this);
    m_stepBackAction = new QAction(QStringLiteral("Step Back (,)"), this);
    m_insertionFollowsPlaybackAction = new QAction(QStringLiteral("Insertion Follows Playback (N)"), this);
    m_selectAllAction = new QAction(QStringLiteral("Select All (Ctrl+A)"), this);
    m_unselectAllAction = new QAction(QStringLiteral("Unselect All (Esc)"), this);

    m_setNodeStartAction = new QAction(QStringLiteral("Set Start (A)"), this);
    m_setNodeEndAction = new QAction(QStringLiteral("Set End (S)"), this);
    m_trimNodeAction = new QAction(QStringLiteral("Trim Node (Shift+T)"), this);
    m_autoPanAction = new QAction(QStringLiteral("Auto Pan (R)"), this);
    m_toggleNodeNameAction = new QAction(QStringLiteral("Toggle Node Name (E)"), this);
    m_showAllNodeNamesAction = new QAction(QStringLiteral("Node Name Always On"), this);
    m_importSoundAction = new QAction(QStringLiteral("Import Sound"), this);
    m_showTimelineAction = new QAction(QStringLiteral("Show Timeline (T)"), this);
    m_timelineClickSeeksAction = new QAction(QStringLiteral("Click Seeks Playhead"), this);
    m_audioPoolAction = new QAction(QStringLiteral("Audio Pool (P)"), this);
    m_deleteNodeAction = new QAction(QStringLiteral("Delete (Backspace)"), this);
    m_clearAllAction = new QAction(QStringLiteral("Clear All (Ctrl+Shift+A, Backspace)"), this);

    m_motionTrackingAction = new QAction(QStringLiteral("Motion Tracking"), this);
    m_motionTrackingAction->setCheckable(true);
    m_autoPanAction->setCheckable(true);
    m_insertionFollowsPlaybackAction->setCheckable(true);
    m_insertionFollowsPlaybackAction->setChecked(true);
    m_showAllNodeNamesAction->setCheckable(true);
    m_showAllNodeNamesAction->setChecked(true);
    m_showTimelineAction->setCheckable(true);
    m_showTimelineAction->setChecked(true);
    m_timelineClickSeeksAction->setCheckable(true);
    m_timelineClickSeeksAction->setChecked(true);
    m_audioPoolAction->setCheckable(true);
    m_audioPoolAction->setChecked(false);
    m_importSoundAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));

    m_toggleDebugAction = new QAction(QStringLiteral("Toggle Debug"), this);
    m_toggleDebugAction->setCheckable(true);
    m_toggleDebugAction->setChecked(true);

    m_setNodeStartAction->setEnabled(false);
    m_setNodeEndAction->setEnabled(false);
    m_trimNodeAction->setEnabled(false);
    m_autoPanAction->setEnabled(false);
    m_toggleNodeNameAction->setEnabled(false);
    m_importSoundAction->setEnabled(false);
    m_deleteNodeAction->setEnabled(false);
    m_selectAllAction->setEnabled(false);
    m_unselectAllAction->setEnabled(false);
    m_clearAllAction->setEnabled(false);

    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(m_openAction);

    auto* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(m_goToStartAction);
    editMenu->addAction(m_playAction);
    editMenu->addAction(m_stepForwardAction);
    editMenu->addAction(m_stepBackAction);
    editMenu->addAction(m_insertionFollowsPlaybackAction);
    editMenu->addAction(m_selectAllAction);
    editMenu->addAction(m_unselectAllAction);

    auto* nodeMenu = menuBar()->addMenu(QStringLiteral("&Node"));
    nodeMenu->addAction(m_setNodeStartAction);
    nodeMenu->addAction(m_setNodeEndAction);
    nodeMenu->addAction(m_trimNodeAction);
    nodeMenu->addSeparator();
    nodeMenu->addAction(m_deleteNodeAction);
    nodeMenu->addAction(m_clearAllAction);

    auto* motionMenu = menuBar()->addMenu(QStringLiteral("&Motion"));
    motionMenu->addAction(m_motionTrackingAction);

    auto* soundMenu = menuBar()->addMenu(QStringLiteral("&Sound"));
    soundMenu->addAction(m_importSoundAction);
    soundMenu->addAction(m_autoPanAction);

    auto* timelineMenu = menuBar()->addMenu(QStringLiteral("&Timeline"));
    timelineMenu->addAction(m_showTimelineAction);
    timelineMenu->addAction(m_timelineClickSeeksAction);

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(m_toggleNodeNameAction);
    viewMenu->addAction(m_showAllNodeNamesAction);
    viewMenu->addAction(m_audioPoolAction);

    auto* debugMenu = menuBar()->addMenu(QStringLiteral("&Debug"));
    debugMenu->addAction(m_toggleDebugAction);
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("dawg"));
    resize(1400, 900);

    auto* root = new QWidget(this);
    auto* outerLayout = new QHBoxLayout(root);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    m_contentSplitter = new QSplitter(Qt::Horizontal, root);
    m_contentSplitter->setChildrenCollapsible(false);
    m_contentSplitter->setHandleWidth(6);

    m_mainContent = new QWidget(root);
    auto* layout = new QVBoxLayout(m_mainContent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_frameLabel = new QLabel(QStringLiteral("Frame 0  |  0.00 s"), m_mainContent);
    m_playPauseShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    m_startShortcut = new QShortcut(QKeySequence(Qt::Key_Return), this);
    m_numpadStartShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), this);
    m_stepBackShortcut = new QShortcut(QKeySequence(Qt::Key_Comma), this);
    m_stepForwardShortcut = new QShortcut(QKeySequence(Qt::Key_Period), this);
    m_insertionFollowsPlaybackShortcut = new QShortcut(QKeySequence(Qt::Key_N), this);
    m_selectAllShortcut = new QShortcut(QKeySequence::SelectAll, this);
    m_nodeStartShortcut = new QShortcut(QKeySequence(Qt::Key_A), this);
    m_nodeEndShortcut = new QShortcut(QKeySequence(Qt::Key_S), this);
    m_showTimelineShortcut = new QShortcut(QKeySequence(Qt::Key_T), this);
    m_trimNodeShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_T), this);
    m_autoPanShortcut = new QShortcut(QKeySequence(Qt::Key_R), this);
    m_audioPoolShortcut = new QShortcut(QKeySequence(Qt::Key_P), this);
    m_toggleNodeNameShortcut = new QShortcut(QKeySequence(Qt::Key_E), this);
    m_deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    m_unselectAllShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    m_playPauseShortcut->setContext(Qt::ApplicationShortcut);
    m_startShortcut->setContext(Qt::ApplicationShortcut);
    m_numpadStartShortcut->setContext(Qt::ApplicationShortcut);
    m_stepBackShortcut->setContext(Qt::ApplicationShortcut);
    m_stepForwardShortcut->setContext(Qt::ApplicationShortcut);
    m_insertionFollowsPlaybackShortcut->setContext(Qt::ApplicationShortcut);
    m_selectAllShortcut->setContext(Qt::ApplicationShortcut);
    m_nodeStartShortcut->setContext(Qt::ApplicationShortcut);
    m_nodeEndShortcut->setContext(Qt::ApplicationShortcut);
    m_showTimelineShortcut->setContext(Qt::ApplicationShortcut);
    m_trimNodeShortcut->setContext(Qt::ApplicationShortcut);
    m_autoPanShortcut->setContext(Qt::ApplicationShortcut);
    m_audioPoolShortcut->setContext(Qt::ApplicationShortcut);
    m_toggleNodeNameShortcut->setContext(Qt::ApplicationShortcut);
    m_deleteShortcut->setContext(Qt::ApplicationShortcut);
    m_unselectAllShortcut->setContext(Qt::ApplicationShortcut);

    m_mainVerticalSplitter = new QSplitter(Qt::Vertical, m_mainContent);
    m_mainVerticalSplitter->setChildrenCollapsible(false);
    m_mainVerticalSplitter->setHandleWidth(6);

    m_canvas = new VideoCanvas(m_mainVerticalSplitter);
    m_timelinePanel = new QFrame(m_mainVerticalSplitter);
    m_timelinePanel->setObjectName(QStringLiteral("timelinePanel"));
    m_timelinePanel->setFrameShape(QFrame::NoFrame);
    m_timelinePanel->setMinimumHeight(72);
    auto* timelinePanelLayout = new QVBoxLayout(m_timelinePanel);
    timelinePanelLayout->setContentsMargins(0, 0, 0, 0);
    timelinePanelLayout->setSpacing(0);

    auto* timelineInfoRow = new QHBoxLayout();
    timelineInfoRow->setContentsMargins(8, 0, 8, 0);
    timelineInfoRow->setSpacing(12);
    timelineInfoRow->addWidget(m_frameLabel);
    timelineInfoRow->addStretch(1);

    m_timeline = new TimelineView(m_timelinePanel);
    timelinePanelLayout->addLayout(timelineInfoRow);
    timelinePanelLayout->addWidget(m_timeline, 1);

    m_audioPoolPanel = new QFrame(m_contentSplitter);
    m_audioPoolPanel->setObjectName(QStringLiteral("audioPoolPanel"));
    m_audioPoolPanel->setVisible(false);
    m_audioPoolPanel->setFrameShape(QFrame::NoFrame);
    m_audioPoolPanel->setMinimumWidth(240);
    m_audioPoolPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* audioPoolLayout = new QVBoxLayout(m_audioPoolPanel);
    audioPoolLayout->setContentsMargins(0, 0, 0, 0);
    audioPoolLayout->setSpacing(0);

    auto* audioPoolHeader = new QHBoxLayout();
    audioPoolHeader->setContentsMargins(8, 0, 8, 0);
    auto* audioPoolTitle = new QLabel(QStringLiteral("Audio Pool"), m_audioPoolPanel);
    auto* audioPoolImportButton = new QPushButton(QStringLiteral("Import"), m_audioPoolPanel);
    m_audioPoolCloseButton = new QPushButton(QStringLiteral("x"), m_audioPoolPanel);
    audioPoolImportButton->setCursor(Qt::PointingHandCursor);
    m_audioPoolCloseButton->setCursor(Qt::PointingHandCursor);
    m_audioPoolCloseButton->setFixedWidth(28);
    audioPoolHeader->addWidget(audioPoolTitle);
    audioPoolHeader->addStretch(1);
    audioPoolHeader->addWidget(audioPoolImportButton);
    audioPoolHeader->addWidget(m_audioPoolCloseButton);
    audioPoolLayout->addLayout(audioPoolHeader);

    m_videoAudioRow = new QWidget(m_audioPoolPanel);
    auto* videoAudioLayout = new QHBoxLayout(m_videoAudioRow);
    videoAudioLayout->setContentsMargins(8, 0, 8, 0);
    videoAudioLayout->setSpacing(6);

    auto* videoAudioIcon = new QLabel(QStringLiteral("\u266B"), m_videoAudioRow);
    videoAudioIcon->setStyleSheet(QStringLiteral("color: #c7d0da; font-size: 9pt;"));

    m_videoAudioLabel = new QLabel(m_videoAudioRow);
    m_videoAudioLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_videoAudioLabel->setStyleSheet(QStringLiteral("color: #c7d0da; font-size: 8.5pt;"));

    m_videoAudioMuteButton = new QToolButton(m_videoAudioRow);
    m_videoAudioMuteButton->setAutoRaise(true);
    m_videoAudioMuteButton->setCursor(Qt::PointingHandCursor);
    m_videoAudioMuteButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; padding: 2px; }"
        "QToolButton:hover { background: rgba(255, 255, 255, 0.06); border-radius: 4px; }"));
    connect(m_videoAudioMuteButton, &QToolButton::clicked, m_controller, &PlayerController::toggleEmbeddedVideoAudioMuted);

    videoAudioLayout->addWidget(videoAudioIcon, 0, Qt::AlignVCenter);
    videoAudioLayout->addWidget(m_videoAudioLabel, 1);
    videoAudioLayout->addWidget(m_videoAudioMuteButton, 0, Qt::AlignVCenter);
    audioPoolLayout->addWidget(m_videoAudioRow);

    auto* audioPoolScroll = new QScrollArea(m_audioPoolPanel);
    audioPoolScroll->setWidgetResizable(true);
    audioPoolScroll->setFrameShape(QFrame::NoFrame);
    audioPoolScroll->setObjectName(QStringLiteral("audioPoolScroll"));
    audioPoolScroll->setContentsMargins(0, 0, 0, 0);
    m_audioPoolListContainer = new QWidget(audioPoolScroll);
    m_audioPoolListContainer->setObjectName(QStringLiteral("audioPoolListContainer"));
    m_audioPoolListLayout = new QVBoxLayout(m_audioPoolListContainer);
    m_audioPoolListLayout->setContentsMargins(0, 0, 0, 0);
    m_audioPoolListLayout->setSpacing(1);
    audioPoolScroll->setWidget(m_audioPoolListContainer);
    audioPoolLayout->addWidget(audioPoolScroll, 1);
    connect(audioPoolImportButton, &QPushButton::clicked, this, &MainWindow::importAudioToPool);

    m_mainVerticalSplitter->addWidget(m_canvas);
    m_mainVerticalSplitter->addWidget(m_timelinePanel);
    m_mainVerticalSplitter->setStretchFactor(0, 1);
    m_mainVerticalSplitter->setStretchFactor(1, 0);
    m_mainVerticalSplitter->setSizes({700, m_timelinePreferredHeight});
    connect(m_mainVerticalSplitter, &QSplitter::splitterMoved, this, [this]()
    {
        if (m_timelinePanel && m_timelinePanel->isVisible())
        {
            m_timelinePreferredHeight = std::max(96, m_timelinePanel->height());
        }
    });

    layout->addWidget(m_mainVerticalSplitter, 1);

    m_contentSplitter->addWidget(m_mainContent);
    m_contentSplitter->addWidget(m_audioPoolPanel);
    m_contentSplitter->setStretchFactor(0, 1);
    m_contentSplitter->setStretchFactor(1, 0);
    m_contentSplitter->setSizes({1040, m_audioPoolPreferredWidth});
    connect(m_contentSplitter, &QSplitter::splitterMoved, this, [this]()
    {
        if (m_audioPoolPanel && m_audioPoolPanel->isVisible())
        {
            m_audioPoolPreferredWidth = std::max(240, m_audioPoolPanel->width());
        }
    });

    outerLayout->addWidget(m_contentSplitter, 1);

    setCentralWidget(root);

    auto* debugOverlay = new DebugOverlayWindow(root);
    m_debugOverlay = debugOverlay;
    m_debugOverlay->move(16, 16);
    m_debugOverlay->setVisible(m_debugVisible);
    m_debugOverlay->raise();
    connect(debugOverlay, &DebugOverlayWindow::closeRequested, this, [this]()
    {
        updateDebugVisibility(false);
        showStatus(QStringLiteral("Debug window hidden."));
    });

    setStyleSheet(R"(
        QMainWindow {
            background: #0d1014;
        }
        QSplitter::handle {
            background: #050608;
        }
        QSplitter::handle:hover {
            background: #0c1015;
        }
        QWidget {
            color: #f3f5f7;
            font-family: "Segoe UI";
            font-size: 10.5pt;
        }
        QMenuBar {
            background: #121720;
            color: #f3f5f7;
        }
        QMenuBar::item {
            background: transparent;
            padding: 6px 10px;
        }
        QMenuBar::item:selected {
            background: #223146;
        }
        QMenuBar QLabel {
            color: #9fb4c8;
            font-size: 9pt;
            padding-right: 4px;
        }
        QMenu {
            background: #121720;
            color: #f3f5f7;
            border: 1px solid #324155;
        }
        QMenu::item:selected {
            background: #223146;
        }
        QLabel {
            color: #d8dde4;
        }
        QStatusBar {
            background: #121720;
        }
        QFrame {
            background: #0f141b;
        }
        QFrame#audioPoolPanel {
            background: #07090c;
        }
        QScrollArea#audioPoolScroll {
            background: #07090c;
            border: none;
        }
        QWidget#audioPoolListContainer {
            background: #07090c;
        }
        QFrame#debugOverlay {
            background: #0b0f14;
            border: 1px solid #253142;
            border-radius: 8px;
        }
        QWidget#debugOverlayTitleBar {
            background: #111821;
            border-top-left-radius: 8px;
            border-top-right-radius: 8px;
        }
        QLabel#debugOverlayTitle {
            color: #f3f5f7;
            font-weight: 600;
        }
        QLabel#debugOverlayText {
            color: #d8dde4;
            font-size: 9pt;
            padding: 10px;
            background: #0b0f14;
        }
        QPushButton#debugOverlayCloseButton {
            background: #18202b;
            color: #ecf1f6;
            border: 1px solid #324155;
            border-radius: 4px;
            padding: 0px;
        }
        QPushButton#debugOverlayCloseButton:hover {
            background: #223146;
        }
        QPushButton {
            background: #18202b;
            color: #ecf1f6;
            border: 1px solid #324155;
            border-radius: 6px;
            padding: 4px 10px;
        }
        QPushButton:hover {
            background: #223146;
        }
    )");
}

void MainWindow::syncMotionTrackingUi(const bool enabled)
{
    Q_UNUSED(enabled);
}

void MainWindow::tryOpenLocalDevVideo()
{
#ifdef QT_DEBUG
    const QDir appDir{QCoreApplication::applicationDirPath()};
    const QStringList candidateRoots{
        QDir::cleanPath(appDir.filePath("../../src")),
        QDir::currentPath()
    };

    for (const auto& rootPath : candidateRoots)
    {
        QDir devDir{QDir(rootPath).filePath(".dev")};
        if (!devDir.exists())
        {
            continue;
        }

        const auto matches = devDir.entryInfoList(
            QStringList{
                QStringLiteral("test-video.mov"),
                QStringLiteral("test-video.MOV")
            },
            QDir::Files,
            QDir::Name);

        if (matches.isEmpty())
        {
            continue;
        }

        if (m_controller->openVideo(matches.front().absoluteFilePath()))
        {
            populateAudioPoolFromLocalDevDirectory();
        }
        return;
    }
#endif
}

void MainWindow::populateAudioPoolFromLocalDevDirectory()
{
#ifdef QT_DEBUG
    const QDir appDir{QCoreApplication::applicationDirPath()};
    const QStringList candidateRoots{
        QDir::cleanPath(appDir.filePath("../../src")),
        QDir::currentPath()
    };

    for (const auto& rootPath : candidateRoots)
    {
        QDir devDir{QDir(rootPath).filePath(".dev")};
        if (!devDir.exists())
        {
            continue;
        }

        const auto matches = devDir.entryInfoList(
            QStringList{
                QStringLiteral("*.wav"),
                QStringLiteral("*.mp3"),
                QStringLiteral("*.flac"),
                QStringLiteral("*.ogg"),
                QStringLiteral("*.m4a"),
                QStringLiteral("*.aac"),
                QStringLiteral("*.aif"),
                QStringLiteral("*.aiff"),
                QStringLiteral("*.WAV"),
                QStringLiteral("*.MP3"),
                QStringLiteral("*.FLAC"),
                QStringLiteral("*.OGG"),
                QStringLiteral("*.M4A"),
                QStringLiteral("*.AAC"),
                QStringLiteral("*.AIF"),
                QStringLiteral("*.AIFF")
            },
            QDir::Files,
            QDir::Name);

        for (const auto& match : matches)
        {
            m_controller->importAudioToPool(match.absoluteFilePath());
        }
        return;
    }
#endif
}

void MainWindow::armClearAllShortcut()
{
    if (!m_controller->hasTracks())
    {
        return;
    }

    m_clearAllShortcutArmed = true;
    m_clearAllShortcutTimer.start();
    showStatus(QStringLiteral("Clear all armed. Press Backspace to remove all nodes."));
}

void MainWindow::clearPendingClearAllShortcut()
{
    m_clearAllShortcutArmed = false;
    m_clearAllShortcutTimer.stop();
}
