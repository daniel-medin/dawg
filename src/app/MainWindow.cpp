#include "app/MainWindow.h"

#include <algorithm>
#include <array>
#include <functional>

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QDrag>
#include <QEnterEvent>
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
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScrollArea>
#include <QScreen>
#include <QSizePolicy>
#include <QShortcut>
#include <QSignalBlocker>
#include <QResizeEvent>
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
#include "ui/ClipEditorView.h"
#include "ui/DebugOverlayWindow.h"
#include "ui/MixView.h"
#include "ui/NativeVideoViewport.h"
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

QCursor audioPoolPreviewCursor()
{
    static const QCursor cursor = []()
    {
        QPixmap pixmap(20, 20);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);

        QPainterPath triangle;
        triangle.moveTo(6.0, 4.0);
        triangle.lineTo(16.0, 10.0);
        triangle.lineTo(6.0, 16.0);
        triangle.closeSubpath();

        painter.fillPath(triangle, Qt::white);
        return QCursor(pixmap, 10, 10);
    }();
    return cursor;
}

class AudioPoolRow final : public QWidget
{
public:
    explicit AudioPoolRow(
        const QString& assetPath,
        std::function<bool(const QString&)> startPreview,
        std::function<void()> stopPreview,
        std::function<void()> activateItem,
        std::function<void()> doubleActivateItem,
        QWidget* parent = nullptr)
        : QWidget(parent)
        , m_assetPath(assetPath)
        , m_startPreview(std::move(startPreview))
        , m_stopPreview(std::move(stopPreview))
        , m_activateItem(std::move(activateItem))
        , m_doubleActivateItem(std::move(doubleActivateItem))
    {
        setCursor(Qt::OpenHandCursor);
        setMouseTracking(true);
        qApp->installEventFilter(this);
    }

    ~AudioPoolRow() override
    {
        qApp->removeEventFilter(this);
        stopPreviewIfNeeded();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched);

        switch (event->type())
        {
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
            updateCursorState();
            break;
        case QEvent::MouseButtonRelease:
            if (m_previewHeld)
            {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent->button() == Qt::LeftButton)
                {
                    stopPreviewIfNeeded();
                    updateCursorState();
                }
            }
            break;
        default:
            break;
        }

        return QWidget::eventFilter(watched, event);
    }

    void enterEvent(QEnterEvent* event) override
    {
        m_hovered = true;
        updateCursorState();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        m_hovered = false;
        updateCursorState();
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            m_dragStartPosition = event->position().toPoint();
            m_dragPerformed = false;

            if (previewModifierActive(event->modifiers()))
            {
                if (m_startPreview)
                {
                    m_previewHeld = m_startPreview(m_assetPath);
                }
                updateCursorState();
                event->accept();
                return;
            }

            setCursor(Qt::ClosedHandCursor);
        }

        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_previewHeld)
        {
            event->accept();
            return;
        }

        if (!(event->buttons() & Qt::LeftButton))
        {
            updateCursorState();
            QWidget::mouseMoveEvent(event);
            return;
        }

        if (previewModifierActive(event->modifiers()))
        {
            event->accept();
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
        m_dragPerformed = true;
        drag->exec(Qt::CopyAction, Qt::CopyAction);
        updateCursorState();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && m_skipNextReleaseActivation)
        {
            m_skipNextReleaseActivation = false;
            updateCursorState();
            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton && m_previewHeld)
        {
            stopPreviewIfNeeded();
            updateCursorState();
            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton
            && !m_dragPerformed
            && !previewModifierActive(event->modifiers())
            && rect().contains(event->position().toPoint()))
        {
            if (m_activateItem)
            {
                m_activateItem();
            }
            updateCursorState();
            event->accept();
            return;
        }

        updateCursorState();
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton
            && !previewModifierActive(event->modifiers())
            && rect().contains(event->position().toPoint()))
        {
            m_skipNextReleaseActivation = true;
            if (m_doubleActivateItem)
            {
                m_doubleActivateItem();
            }
            updateCursorState();
            event->accept();
            return;
        }

        QWidget::mouseDoubleClickEvent(event);
    }

private:
    [[nodiscard]] bool previewModifierActive(const Qt::KeyboardModifiers modifiers) const
    {
        return modifiers & Qt::ControlModifier;
    }

    void stopPreviewIfNeeded()
    {
        if (!m_previewHeld)
        {
            return;
        }

        m_previewHeld = false;
        if (m_stopPreview)
        {
            m_stopPreview();
        }
    }

    void updateCursorState()
    {
        if (m_previewHeld || (m_hovered && previewModifierActive(QGuiApplication::keyboardModifiers())))
        {
            setCursor(audioPoolPreviewCursor());
            return;
        }

        if (QApplication::mouseButtons() & Qt::LeftButton)
        {
            setCursor(Qt::ClosedHandCursor);
            return;
        }

        setCursor(Qt::OpenHandCursor);
    }

    QString m_assetPath;
    std::function<bool(const QString&)> m_startPreview;
    std::function<void()> m_stopPreview;
    std::function<void()> m_activateItem;
    std::function<void()> m_doubleActivateItem;
    QPoint m_dragStartPosition;
    bool m_dragPerformed = false;
    bool m_hovered = false;
    bool m_previewHeld = false;
    bool m_skipNextReleaseActivation = false;
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
    m_mixMeterTimer.setInterval(33);
    connect(&m_clearAllShortcutTimer, &QTimer::timeout, this, &MainWindow::clearPendingClearAllShortcut);
    connect(&m_memoryUsageTimer, &QTimer::timeout, this, &MainWindow::updateMemoryUsage);
    connect(&m_mixMeterTimer, &QTimer::timeout, this, &MainWindow::refreshMixView);
    m_statusToastTimer.setSingleShot(true);
    m_statusToastTimer.setInterval(2800);
    connect(&m_statusToastTimer, &QTimer::timeout, this, [this]()
    {
        if (m_statusToast)
        {
            m_statusToast->hide();
        }
    });
    m_canvasTipsTimer.setSingleShot(true);
    m_canvasTipsTimer.setInterval(6000);
    connect(&m_canvasTipsTimer, &QTimer::timeout, this, &MainWindow::hideCanvasTipsOverlay);
    m_nodeNudgeTimer.setSingleShot(false);
    m_nodeNudgeTimer.setInterval(220);
    connect(&m_nodeNudgeTimer, &QTimer::timeout, this, &MainWindow::applyHeldNodeNudge);

    connect(m_openAction, &QAction::triggered, this, &MainWindow::openVideo);
    connect(m_importSoundAction, &QAction::triggered, this, &MainWindow::importSound);
    connect(m_showTimelineAction, &QAction::toggled, this, [this](const bool visible)
    {
        updateTimelineVisibility(visible);
        showStatus(visible ? QStringLiteral("Timeline shown.") : QStringLiteral("Timeline hidden."));
    });
    connect(m_showClipEditorAction, &QAction::toggled, this, [this](const bool visible)
    {
        updateClipEditorVisibility(visible);
        showStatus(visible ? QStringLiteral("Clip editor shown.") : QStringLiteral("Clip editor hidden."));
    });
    connect(m_showMixAction, &QAction::toggled, this, [this](const bool visible)
    {
        updateMixVisibility(visible);
        showStatus(visible ? QStringLiteral("Mix window shown.") : QStringLiteral("Mix window hidden."));
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
    connect(m_showNativeViewportAction, &QAction::toggled, this, [this](const bool visible)
    {
        updateNativeViewportVisibility(visible);
        showStatus(
            visible
                ? QStringLiteral("Native video viewport test shown.")
                : QStringLiteral("Native video viewport test hidden."));
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
    connect(m_stepFastForwardAction, &QAction::triggered, m_controller, &PlayerController::stepFastForward);
    connect(m_stepFastBackAction, &QAction::triggered, m_controller, &PlayerController::stepFastBackward);
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::copySelectedNode);
    connect(m_pasteAction, &QAction::triggered, this, &MainWindow::pasteNode);
    connect(m_cutAction, &QAction::triggered, this, &MainWindow::cutSelectedNode);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undoNodeEdit);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redoNodeEdit);
    connect(m_selectNextNodeAction, &QAction::triggered, this, &MainWindow::selectNextVisibleNode);
    connect(m_moveNodeUpAction, &QAction::triggered, this, &MainWindow::moveSelectedNodeUp);
    connect(m_moveNodeDownAction, &QAction::triggered, this, &MainWindow::moveSelectedNodeDown);
    connect(m_moveNodeLeftAction, &QAction::triggered, this, &MainWindow::moveSelectedNodeLeft);
    connect(m_moveNodeRightAction, &QAction::triggered, this, &MainWindow::moveSelectedNodeRight);
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
    connect(m_setLoopStartAction, &QAction::triggered, this, &MainWindow::handleLoopStartShortcut);
    connect(m_setLoopEndAction, &QAction::triggered, this, &MainWindow::handleLoopEndShortcut);
    connect(m_clearLoopRangeAction, &QAction::triggered, this, &MainWindow::clearLoopRange);
    connect(m_trimNodeAction, &QAction::triggered, this, &MainWindow::trimSelectedNodeToSound);
    connect(m_autoPanAction, &QAction::triggered, this, &MainWindow::toggleSelectedNodeAutoPan);
    connect(m_toggleNodeNameAction, &QAction::triggered, m_controller, &PlayerController::toggleSelectedTrackLabels);
    connect(m_showAllNodeNamesAction, &QAction::toggled, this, [this](const bool enabled)
    {
        m_canvas->setShowAllLabels(enabled);
        if (m_nativeViewport)
        {
            m_nativeViewport->setShowAllLabels(enabled);
        }
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
    connect(m_canvas, &VideoCanvas::trackActivated, this, [this](const QUuid& trackId)
    {
        m_controller->selectTrack(trackId);
        updateClipEditorVisibility(true);
    });
    connect(m_canvas, &VideoCanvas::trackContextMenuRequested, this, [this](const QUuid& trackId, const QPoint& globalPosition)
    {
        showNodeContextMenu(trackId, globalPosition, true);
    });
    connect(m_canvas, &VideoCanvas::selectedTrackMoved, m_controller, &PlayerController::moveSelectedTrack);
    connect(m_timeline, &TimelineView::frameRequested, m_controller, &PlayerController::seekToFrame);
    connect(m_timeline, &TimelineView::loopStartFrameRequested, m_controller, &PlayerController::setLoopStartFrame);
    connect(m_timeline, &TimelineView::loopEndFrameRequested, m_controller, &PlayerController::setLoopEndFrame);
    connect(m_timeline, &TimelineView::trackSelected, m_controller, &PlayerController::selectTrack);
    connect(m_timeline, &TimelineView::trackActivated, this, [this](const QUuid& trackId)
    {
        m_controller->selectTrack(trackId);
        updateClipEditorVisibility(true);
    });
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
    connect(m_stepFastForwardShortcut, &QShortcut::activated, m_controller, &PlayerController::stepFastForward);
    connect(m_stepFastBackShortcut, &QShortcut::activated, m_controller, &PlayerController::stepFastBackward);
    connect(m_copyShortcut, &QShortcut::activated, this, &MainWindow::copySelectedNode);
    connect(m_pasteShortcut, &QShortcut::activated, this, &MainWindow::pasteNode);
    connect(m_cutShortcut, &QShortcut::activated, this, &MainWindow::cutSelectedNode);
    connect(m_undoShortcut, &QShortcut::activated, this, &MainWindow::undoNodeEdit);
    connect(m_redoShortcut, &QShortcut::activated, this, &MainWindow::redoNodeEdit);
    connect(m_selectNextNodeShortcut, &QShortcut::activated, this, &MainWindow::selectNextVisibleNode);
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
    connect(m_showClipEditorShortcut, &QShortcut::activated, m_showClipEditorAction, &QAction::trigger);
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
    connect(m_controller, &PlayerController::loopRangeChanged, this, &MainWindow::refreshTimeline);
    connect(m_controller, &PlayerController::selectionChanged, this, &MainWindow::updateSelectionState);
    connect(m_controller, &PlayerController::trackAvailabilityChanged, this, &MainWindow::updateTrackAvailabilityState);
    connect(m_controller, &PlayerController::editStateChanged, this, &MainWindow::updateEditActionState);
    connect(m_controller, &PlayerController::audioPoolChanged, this, &MainWindow::refreshAudioPool);
    connect(m_controller, &PlayerController::audioPoolPlaybackStateChanged, this, &MainWindow::updateAudioPoolPlaybackIndicators);
    connect(m_controller, &PlayerController::videoLoaded, this, &MainWindow::handleVideoLoaded);
    connect(m_controller, &PlayerController::videoAudioStateChanged, this, &MainWindow::updateVideoAudioRow);
    connect(m_controller, &PlayerController::statusChanged, this, &MainWindow::showStatus);
    connect(m_showMixShortcut, &QShortcut::activated, m_showMixAction, &QAction::trigger);
    connect(m_clipEditorView, &ClipEditorView::playRequested, m_controller, &PlayerController::startSelectedTrackClipPreview);
    connect(m_clipEditorView, &ClipEditorView::stopRequested, m_controller, &PlayerController::stopSelectedTrackClipPreview);
    connect(m_clipEditorView, &ClipEditorView::clipRangeChanged, m_controller, &PlayerController::setSelectedTrackClipRangeMs);
    connect(m_mixView, &MixView::masterGainChanged, m_controller, &PlayerController::setMasterMixGainDb);
    connect(m_mixView, &MixView::masterMutedChanged, m_controller, &PlayerController::setMasterMixMuted);
    connect(m_mixView, &MixView::laneGainChanged, m_controller, &PlayerController::setMixLaneGainDb);
    connect(m_mixView, &MixView::laneMutedChanged, m_controller, &PlayerController::setMixLaneMuted);
    connect(m_mixView, &MixView::laneSoloChanged, m_controller, &PlayerController::setMixLaneSoloed);

    updatePlaybackState(false);
    updateInsertionFollowsPlaybackState(m_controller->isInsertionFollowsPlayback());
    syncMotionTrackingUi(m_controller->isMotionTrackingEnabled());
    updateSelectionState(m_controller->hasSelection());
    updateTrackAvailabilityState(m_controller->hasTracks());
    updateEditActionState();
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
    if (watched == m_nativeViewportWindow
        && (event->type() == QEvent::Hide || event->type() == QEvent::Close))
    {
        if (m_showNativeViewportAction && m_showNativeViewportAction->isChecked())
        {
            const QSignalBlocker blocker{m_showNativeViewportAction};
            m_showNativeViewportAction->setChecked(false);
        }
    }

    if (m_canvasTipsOverlay && m_canvasTipsOverlay->isVisible())
    {
        const auto eventType = event->type();
        if (eventType == QEvent::KeyPress
            || eventType == QEvent::MouseButtonPress
            || eventType == QEvent::Wheel)
        {
            if (auto* widget = qobject_cast<QWidget*>(watched))
            {
                if (widget == this || isAncestorOf(widget))
                {
                    hideCanvasTipsOverlay();
                }
            }
        }
    }

    if (!shouldIgnoreNodeMovementShortcuts())
    {
        if (event->type() == QEvent::KeyPress)
        {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (!keyEvent->isAutoRepeat() && keyEvent->modifiers() == Qt::NoModifier)
            {
                switch (keyEvent->key())
                {
                case Qt::Key_Up:
                case Qt::Key_Left:
                case Qt::Key_Down:
                case Qt::Key_Right:
                    beginHeldNodeNudge(keyEvent->key());
                    return true;
                default:
                    break;
                }
            }
        }
        else if (event->type() == QEvent::KeyRelease)
        {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (!keyEvent->isAutoRepeat() && keyEvent->modifiers() == Qt::NoModifier)
            {
                switch (keyEvent->key())
                {
                case Qt::Key_Up:
                case Qt::Key_Left:
                case Qt::Key_Down:
                case Qt::Key_Right:
                    endHeldNodeNudge(keyEvent->key());
                    return true;
                default:
                    break;
                }
            }
        }
    }

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

            if (key == Qt::Key_Backspace
                && m_timeline
                && m_timeline->loopShortcutFrame().has_value()
                && (m_controller->loopStartFrame().has_value() || m_controller->loopEndFrame().has_value()))
            {
                clearLoopRange();
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

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateOverlayPositions();
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

void MainWindow::handleLoopStartShortcut()
{
    if (const auto targetFrame = timelineLoopTargetFrame(); targetFrame.has_value())
    {
        m_controller->setLoopStartFrame(*targetFrame);
    }
}

void MainWindow::handleLoopEndShortcut()
{
    if (const auto targetFrame = timelineLoopTargetFrame(); targetFrame.has_value())
    {
        m_controller->setLoopEndFrame(*targetFrame);
    }
}

void MainWindow::clearLoopRange()
{
    m_controller->clearLoopRange();
}

void MainWindow::handleNodeStartShortcut()
{
    if (m_timeline && m_timeline->loopShortcutFrame().has_value())
    {
        handleLoopStartShortcut();
        return;
    }

    if (shouldApplyNodeShortcutToAll())
    {
        m_controller->setAllTracksStartToCurrentFrame();
        return;
    }

    m_controller->setSelectedTrackStartToCurrentFrame();
}

void MainWindow::handleNodeEndShortcut()
{
    if (m_timeline && m_timeline->loopShortcutFrame().has_value())
    {
        handleLoopEndShortcut();
        return;
    }

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

void MainWindow::copySelectedNode()
{
    m_controller->copySelectedTracks();
    updateEditActionState();
}

void MainWindow::pasteNode()
{
    m_controller->pasteCopiedTracksAtCurrentFrame();
    updateEditActionState();
}

void MainWindow::cutSelectedNode()
{
    m_controller->cutSelectedTracks();
    updateEditActionState();
}

void MainWindow::undoNodeEdit()
{
    m_controller->undoLastTrackEdit();
    updateEditActionState();
}

void MainWindow::redoNodeEdit()
{
    m_controller->redoLastTrackEdit();
    updateEditActionState();
}

void MainWindow::selectNextVisibleNode()
{
    m_controller->selectNextVisibleTrack();
}

void MainWindow::moveSelectedNodeUp()
{
    nudgeSelectedNode(QPointF{0.0, -8.0});
}

void MainWindow::moveSelectedNodeDown()
{
    nudgeSelectedNode(QPointF{0.0, 8.0});
}

void MainWindow::moveSelectedNodeLeft()
{
    nudgeSelectedNode(QPointF{-8.0, 0.0});
}

void MainWindow::moveSelectedNodeRight()
{
    nudgeSelectedNode(QPointF{8.0, 0.0});
}

void MainWindow::updateFrame(const QImage& image, const int frameIndex, const double timestampSeconds)
{
    m_lastPresentedFrame = image;
    m_canvas->setPresentedFrame(image, m_controller->currentVideoFrame(), m_controller->videoFrameSize());
    if (m_nativeViewport)
    {
        m_nativeViewport->setPresentedFrame(image, m_controller->currentVideoFrame(), m_controller->videoFrameSize());
    }
    m_timeline->setCurrentFrame(frameIndex);
    m_frameLabel->setText(
        QStringLiteral("Frame %1  |  %2 s")
            .arg(frameIndex)
            .arg(timestampSeconds, 0, 'f', 2));
    if (m_audioPoolPanel && m_audioPoolPanel->isVisible())
    {
        updateAudioPoolPlaybackIndicators();
    }
    refreshClipEditor();
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
    if (m_nativeViewport)
    {
        m_nativeViewport->setOverlays(m_controller->currentOverlays());
    }
    refreshTimeline();
    refreshClipEditor();
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
    if (playing)
    {
        hideCanvasTipsOverlay();
    }
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
    m_selectNextNodeAction->setEnabled(hasSelection || m_controller->hasTracks());
    m_moveNodeUpAction->setEnabled(hasSelection);
    m_moveNodeDownAction->setEnabled(hasSelection);
    m_moveNodeLeftAction->setEnabled(hasSelection);
    m_moveNodeRightAction->setEnabled(hasSelection);
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
    refreshClipEditor();
    updateEditActionState();
    updateDebugText();
}

void MainWindow::updateTrackAvailabilityState(const bool hasTracks)
{
    m_selectAllAction->setEnabled(hasTracks);
    m_clearAllAction->setEnabled(hasTracks);
    if (m_selectNextNodeAction)
    {
        m_selectNextNodeAction->setEnabled(hasTracks);
    }
    if (!hasTracks)
    {
        clearPendingClearAllShortcut();
    }
    updateEditActionState();
    updateDebugText();
}

void MainWindow::handleVideoLoaded(const QString& filePath, const int totalFrames, const double fps)
{
    const QFileInfo fileInfo{filePath};
    m_clipName = fileInfo.fileName();
    m_timeline->setTimeline(totalFrames, fps);
    if (m_nativeViewportWindow)
    {
        m_nativeViewportWindow->setWindowTitle(QStringLiteral("Native Video Viewport Test - %1").arg(m_clipName));
    }
    if (m_nativeViewport)
    {
        m_nativeViewport->setPresentedFrame(m_lastPresentedFrame, m_controller->currentVideoFrame(), m_controller->videoFrameSize());
        m_nativeViewport->setOverlays(m_controller->currentOverlays());
    }
    refreshTimeline();
    refreshClipEditor();
    showCanvasTipsOverlay();
    updateDebugText();
}

void MainWindow::updateDebugVisibility(const bool enabled)
{
    m_debugVisible = enabled;
    if (m_debugOverlay)
    {
        m_debugOverlay->setVisible(enabled);
    }
    if (m_toggleDebugAction && m_toggleDebugAction->isChecked() != enabled)
    {
        m_toggleDebugAction->setChecked(enabled);
    }
}

void MainWindow::updateNativeViewportVisibility(const bool visible)
{
    if (!m_nativeViewportWindow)
    {
        return;
    }

    if (visible)
    {
        if (m_nativeViewport)
        {
            m_nativeViewport->setPresentedFrame(m_lastPresentedFrame, m_controller->currentVideoFrame(), m_controller->videoFrameSize());
            m_nativeViewport->setOverlays(m_controller->currentOverlays());
            m_nativeViewport->setShowAllLabels(m_showAllNodeNamesAction && m_showAllNodeNamesAction->isChecked());
        }

        m_nativeViewportWindow->show();
        m_nativeViewportWindow->raise();
        m_nativeViewportWindow->activateWindow();
    }
    else
    {
        m_nativeViewportWindow->hide();
    }

    if (m_showNativeViewportAction && m_showNativeViewportAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_showNativeViewportAction};
        m_showNativeViewportAction->setChecked(visible);
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

    syncMainVerticalPanelSizes();

    if (m_showTimelineAction && m_showTimelineAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_showTimelineAction};
        m_showTimelineAction->setChecked(visible);
    }
}

void MainWindow::updateClipEditorVisibility(const bool visible)
{
    if (m_clipEditorPanel)
    {
        if (!visible)
        {
            m_clipEditorPreferredHeight = std::max(148, m_clipEditorPanel->height());
        }
        m_clipEditorPanel->setVisible(visible);
    }

    if (visible)
    {
        refreshClipEditor();
    }

    syncMainVerticalPanelSizes();

    if (m_showClipEditorAction && m_showClipEditorAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_showClipEditorAction};
        m_showClipEditorAction->setChecked(visible);
    }
}

void MainWindow::updateMixVisibility(const bool visible)
{
    if (m_mixPanel)
    {
        if (!visible)
        {
            m_mixPreferredHeight = std::max(132, m_mixPanel->height());
        }
        m_mixPanel->setVisible(visible);
    }

    if (visible)
    {
        if (!m_mixMeterTimer.isActive())
        {
            m_mixMeterTimer.start();
        }
        refreshMixView();
    }
    else
    {
        m_mixMeterTimer.stop();
    }

    syncMainVerticalPanelSizes();

    if (m_showMixAction && m_showMixAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_showMixAction};
        m_showMixAction->setChecked(visible);
    }
}

void MainWindow::syncMainVerticalPanelSizes()
{
    if (!m_mainVerticalSplitter)
    {
        return;
    }

    const auto totalHeight = std::max(320, m_mainVerticalSplitter->height());
    const auto timelineVisible = m_timelinePanel && m_timelinePanel->isVisible();
    const auto clipEditorVisible = m_clipEditorPanel && m_clipEditorPanel->isVisible();
    const auto mixVisible = m_mixPanel && m_mixPanel->isVisible();
    struct PanelTarget
    {
        bool visible = false;
        int preferred = 0;
        int minimum = 0;
        int assigned = 0;
    };

    std::array<PanelTarget, 3> panels{{
        PanelTarget{timelineVisible, m_timelinePreferredHeight, 96, 0},
        PanelTarget{clipEditorVisible, m_clipEditorPreferredHeight, 148, 0},
        PanelTarget{mixVisible, m_mixPreferredHeight, 132, 0}
    }};

    const auto canvasMinimum = 220;
    const auto availableForPanels = std::max(0, totalHeight - canvasMinimum);
    int assignedTotal = 0;
    for (auto& panel : panels)
    {
        if (!panel.visible)
        {
            continue;
        }

        panel.assigned = std::max(panel.minimum, panel.preferred);
        assignedTotal += panel.assigned;
    }

    int overflow = assignedTotal - availableForPanels;
    while (overflow > 0)
    {
        bool reducedAny = false;
        for (auto& panel : panels)
        {
            if (!panel.visible || panel.assigned <= panel.minimum)
            {
                continue;
            }

            --panel.assigned;
            --overflow;
            reducedAny = true;
            if (overflow <= 0)
            {
                break;
            }
        }

        if (!reducedAny)
        {
            break;
        }
    }

    const auto canvasHeight = std::max(
        200,
        totalHeight - panels[0].assigned - panels[1].assigned - panels[2].assigned);
    m_mainVerticalSplitter->setSizes({canvasHeight, panels[0].assigned, panels[1].assigned, panels[2].assigned});
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
        auto* row = new AudioPoolRow(
            item.assetPath,
            [this](const QString& assetPath)
            {
                return m_controller->startAudioPoolPreview(assetPath);
            },
            [this]()
            {
                m_controller->stopAudioPoolPreview();
            },
            [this, trackId = item.trackId]()
            {
                if (!trackId.isNull())
                {
                    m_controller->selectTrackAndJumpToStart(trackId);
                }
            },
            [this, assetPath = item.assetPath]()
            {
                m_controller->createTrackWithAudioAtCurrentFrame(assetPath);
            },
            m_audioPoolListContainer);
        row->setProperty("audioPoolItemKey", item.key);
        row->setProperty("assetPath", item.assetPath);
        row->setProperty("trackId", item.trackId);
        row->setToolTip(item.connectionSummary);
        row->setStyleSheet(QStringLiteral(
            "QWidget { background: transparent; border: none; }"
            "QWidget:hover { background: rgba(255, 255, 255, 0.03); border-radius: 4px; }"));

        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 0, 8, 0);
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
        editButton->setText(QStringLiteral("\u2630"));
        editButton->setFixedWidth(28);
        editButton->setToolTip(QStringLiteral("Edit %1").arg(item.displayName));
        editButton->setStyleSheet(QStringLiteral(
            "QToolButton { background: transparent; color: #d7dee8; border: none; border-radius: 4px; padding: 1px 4px; font-size: 8.5pt; }"
            "QToolButton::menu-indicator { image: none; width: 0px; }"
            "QToolButton:hover { background: rgba(255, 255, 255, 0.08); }"
            "QToolButton:pressed { background: #111821; }"));

        auto* editMenu = new QMenu(editButton);
        auto menuFont = editMenu->font();
        menuFont.setPointSizeF(8.5);
        editMenu->setFont(menuFont);
        editMenu->setStyleSheet(QStringLiteral(
            "QMenu { background: rgba(8, 10, 13, 204); color: #eef2f6; border: 1px solid #1f2935; border-radius: 8px; padding: 4px 0; font-size: 8.5pt; }"
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
    m_videoAudioLabel->setToolTip(
        QStringLiteral("Embedded audio from %1%2")
            .arg(displayName)
            .arg(m_controller->isFastPlaybackEnabled() ? QStringLiteral("\nFast Playback enabled") : QString{}));

    const auto muted = m_controller->isEmbeddedVideoAudioMuted();
    m_videoAudioMuteButton->setText(QStringLiteral("\u2630"));
    m_videoAudioMuteButton->setToolTip(
        muted
            ? QStringLiteral("Video audio options (currently muted)")
            : QStringLiteral("Video audio options"));
}

void MainWindow::showStatus(const QString& message)
{
    if (!m_statusToast || message.trimmed().isEmpty())
    {
        return;
    }

    m_statusToast->setText(message);
    m_statusToast->adjustSize();
    const auto maxWidth = std::min(560, std::max(220, width() - 64));
    m_statusToast->setMaximumWidth(maxWidth);
    m_statusToast->adjustSize();
    updateOverlayPositions();
    m_statusToast->show();
    m_statusToast->raise();
    m_statusToastTimer.start();
}

void MainWindow::nudgeSelectedNode(const QPointF& delta)
{
    if (delta.isNull())
    {
        return;
    }

    m_controller->nudgeSelectedTracks(delta);
}

void MainWindow::beginHeldNodeNudge(const int key)
{
    QPointF delta;
    switch (key)
    {
    case Qt::Key_Up:
        delta = QPointF{0.0, -8.0};
        break;
    case Qt::Key_Left:
        delta = QPointF{-8.0, 0.0};
        break;
    case Qt::Key_Down:
        delta = QPointF{0.0, 8.0};
        break;
    case Qt::Key_Right:
        delta = QPointF{8.0, 0.0};
        break;
    default:
        return;
    }

    m_activeNodeNudgeKey = key;
    m_activeNodeNudgeDelta = delta;
    m_nodeNudgeFastMode = false;
    nudgeSelectedNode(delta);
    m_nodeNudgeTimer.start();
}

void MainWindow::endHeldNodeNudge(const int key)
{
    if (m_activeNodeNudgeKey != key)
    {
        return;
    }

    m_activeNodeNudgeKey = 0;
    m_activeNodeNudgeDelta = {};
    m_nodeNudgeFastMode = false;
    m_nodeNudgeTimer.stop();
    m_nodeNudgeTimer.setInterval(220);
}

void MainWindow::applyHeldNodeNudge()
{
    if (m_activeNodeNudgeKey == 0 || m_activeNodeNudgeDelta.isNull())
    {
        m_nodeNudgeTimer.stop();
        return;
    }

    if (!m_nodeNudgeFastMode)
    {
        m_nodeNudgeFastMode = true;
        m_nodeNudgeTimer.setInterval(24);
    }

    nudgeSelectedNode(m_activeNodeNudgeDelta);
}

bool MainWindow::shouldIgnoreNodeMovementShortcuts() const
{
    if (!m_controller || !m_controller->hasSelection())
    {
        return true;
    }

    const auto* focused = QApplication::focusWidget();
    if (!focused)
    {
        return false;
    }

    return focused->inherits("QLineEdit")
        || focused->inherits("QTextEdit")
        || focused->inherits("QPlainTextEdit")
        || focused->inherits("QAbstractSpinBox")
        || focused->inherits("QComboBox");
}

void MainWindow::updateOverlayPositions()
{
    auto* root = centralWidget();
    if (!root)
    {
        return;
    }

    if (m_statusToast)
    {
        const auto margin = 16;
        const auto x = margin;
        const auto y = std::max(margin, root->height() - m_statusToast->height() - margin);
        m_statusToast->move(x, y);
    }

    if (m_canvasTipsOverlay && m_canvas)
    {
        m_canvasTipsOverlay->move(16, 16);
    }

    if (m_statusToast && m_statusToast->isVisible())
    {
        m_statusToast->raise();
    }
    if (m_canvasTipsOverlay && m_canvasTipsOverlay->isVisible())
    {
        m_canvasTipsOverlay->raise();
    }
}

void MainWindow::showCanvasTipsOverlay()
{
    if (!m_canvasTipsOverlay)
    {
        return;
    }

    m_canvasTipsOverlay->setText(
        QStringLiteral("Left-click to add or select nodes\n"
                       "Right-click a node for options\n"
                       "Drag audio from Audio Pool onto the video\n"
                       "Space plays, , and . step frames"));
    m_canvasTipsOverlay->adjustSize();
    updateOverlayPositions();
    m_canvasTipsOverlay->show();
    m_canvasTipsOverlay->raise();
    m_canvasTipsTimer.start();
}

void MainWindow::hideCanvasTipsOverlay()
{
    m_canvasTipsTimer.stop();
    if (m_canvasTipsOverlay)
    {
        m_canvasTipsOverlay->hide();
    }
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
    auto renameFont = renameEditor->font();
    renameFont.setPointSizeF(8.5);
    renameFont.setBold(true);
    renameEditor->setFont(renameFont);
    auto menuFont = menu.font();
    menuFont.setPointSizeF(8.5);
    menu.setFont(menuFont);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: rgba(18, 23, 32, 204); border: 1px solid #324155; border-radius: 8px; font-size: 8.5pt; }"
        "QMenu::item { padding: 6px 14px; }"
        "QMenu::item:selected { background: #223146; }"));
    renameEditor->setStyleSheet(R"(
        QLineEdit {
            background: rgba(255, 255, 255, 0.03);
            color: #f3f5f7;
            border: 1px solid transparent;
            border-radius: 6px;
            padding: 4px 8px;
            selection-background-color: #223146;
            selection-color: #f3f5f7;
        }
        QLineEdit:focus {
            background: #1a2330;
            border: 1px solid #324155;
            border-radius: 6px;
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
    const auto menuContentWidth = std::clamp(labelWidth + 40, 140, 720);
    renameEditor->setMinimumWidth(menuContentWidth);
    renameEditor->setMaximumWidth(menuContentWidth + 80);
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
    if (!m_timeline && !m_mixView)
    {
        return;
    }

    const auto trackSpans = m_controller->timelineTrackSpans();
    if (m_timeline)
    {
        m_timeline->setTrackSpans(trackSpans);
        m_timeline->setLoopRange(m_controller->loopStartFrame(), m_controller->loopEndFrame());
    }
    if (m_clearLoopRangeAction)
    {
        m_clearLoopRangeAction->setEnabled(
            m_controller->loopStartFrame().has_value() || m_controller->loopEndFrame().has_value());
    }
    refreshMixView();
}

void MainWindow::refreshClipEditor()
{
    if (!m_clipEditorView || !m_clipEditorPanel || !m_clipEditorPanel->isVisible())
    {
        return;
    }

    m_clipEditorView->setState(m_controller->selectedClipEditorState());
}

void MainWindow::refreshMixView()
{
    if (m_mixView && m_mixPanel && m_mixPanel->isVisible())
    {
        const auto laneStrips = m_controller->mixLaneStrips();
        m_mixView->setMixState(
            m_controller->masterMixGainDb(),
            m_controller->masterMixMuted(),
            laneStrips);
        m_mixView->setMeterLevels(m_controller->masterMixLevel(), laneStrips);
    }
}

void MainWindow::updateEditActionState()
{
    if (m_copyAction)
    {
        m_copyAction->setEnabled(m_controller->hasSelection());
    }
    if (m_cutAction)
    {
        m_cutAction->setEnabled(m_controller->hasSelection());
    }
    if (m_pasteAction)
    {
        m_pasteAction->setEnabled(m_controller->canPasteTracks());
    }
    if (m_undoAction)
    {
        m_undoAction->setEnabled(m_controller->canUndoTrackEdit());
    }
    if (m_redoAction)
    {
        m_redoAction->setEnabled(m_controller->canRedoTrackEdit());
    }
}

bool MainWindow::shouldApplyNodeShortcutToAll() const
{
    return m_timeline
        && m_timeline->hasFocus()
        && !m_controller->hasSelection()
        && m_controller->hasTracks();
}

std::optional<int> MainWindow::timelineLoopTargetFrame() const
{
    if (!m_controller || !m_controller->hasVideoLoaded())
    {
        return std::nullopt;
    }

    if (m_timeline && m_timeline->loopShortcutFrame().has_value())
    {
        return m_timeline->loopShortcutFrame();
    }

    return m_controller->currentFrameIndex();
}

void MainWindow::buildMenus()
{
    m_openAction = new QAction(QStringLiteral("Open Video"), this);
    m_openAction->setShortcut(QKeySequence::Open);

    m_goToStartAction = new QAction(QStringLiteral("Jump to Start (Enter)"), this);
    m_playAction = new QAction(QStringLiteral("Play (Space)"), this);
    m_stepForwardAction = new QAction(QStringLiteral("Step Forward (.)"), this);
    m_stepBackAction = new QAction(QStringLiteral("Step Back (,)"), this);
    m_stepFastForwardAction = new QAction(QStringLiteral("Step Fast Forward (-)"), this);
    m_stepFastBackAction = new QAction(QStringLiteral("Step Fast Backward (M)"), this);
    m_insertionFollowsPlaybackAction = new QAction(QStringLiteral("Insertion Follows Playback (N)"), this);
    m_copyAction = new QAction(QStringLiteral("Copy (Ctrl+C)"), this);
    m_pasteAction = new QAction(QStringLiteral("Paste (Ctrl+V)"), this);
    m_cutAction = new QAction(QStringLiteral("Cut (Ctrl+X)"), this);
    m_undoAction = new QAction(QStringLiteral("Undo (Ctrl+Z)"), this);
    m_redoAction = new QAction(QStringLiteral("Redo (Ctrl+Y)"), this);
    m_selectAllAction = new QAction(QStringLiteral("Select All (Ctrl+A)"), this);
    m_unselectAllAction = new QAction(QStringLiteral("Unselect All (Esc)"), this);

    m_setNodeStartAction = new QAction(QStringLiteral("Set Start (A)"), this);
    m_setNodeEndAction = new QAction(QStringLiteral("Set End (S)"), this);
    m_setLoopStartAction = new QAction(QStringLiteral("Set Loop Start (A)"), this);
    m_setLoopEndAction = new QAction(QStringLiteral("Set Loop End (S)"), this);
    m_clearLoopRangeAction = new QAction(QStringLiteral("Clear Loop Range"), this);
    m_selectNextNodeAction = new QAction(QStringLiteral("Select Next (Tab)"), this);
    m_moveNodeUpAction = new QAction(QStringLiteral("Move Up (Up)"), this);
    m_moveNodeDownAction = new QAction(QStringLiteral("Move Down (Down)"), this);
    m_moveNodeLeftAction = new QAction(QStringLiteral("Move Left (Left)"), this);
    m_moveNodeRightAction = new QAction(QStringLiteral("Move Right (Right)"), this);
    m_trimNodeAction = new QAction(QStringLiteral("Trim Node (Shift+T)"), this);
    m_autoPanAction = new QAction(QStringLiteral("Auto Pan (R)"), this);
    m_toggleNodeNameAction = new QAction(QStringLiteral("Toggle Node Name (E)"), this);
    m_showAllNodeNamesAction = new QAction(QStringLiteral("Node Name Always On"), this);
    m_importSoundAction = new QAction(QStringLiteral("Import Sound (Shift+Ctrl+I)"), this);
    m_showClipEditorAction = new QAction(QStringLiteral("Toggle Clip Editor (Ctrl+-)"), this);
    m_showTimelineAction = new QAction(QStringLiteral("Show Timeline (T)"), this);
    m_showMixAction = new QAction(QStringLiteral("Toggle Mix Window (Ctrl++)"), this);
    m_timelineClickSeeksAction = new QAction(QStringLiteral("Click Seeks Playhead"), this);
    m_audioPoolAction = new QAction(QStringLiteral("Audio Pool (P)"), this);
    m_deleteNodeAction = new QAction(QStringLiteral("Delete (Backspace)"), this);
    m_clearAllAction = new QAction(QStringLiteral("Clear All (Ctrl+Shift+A, Backspace)"), this);

    m_motionTrackingAction = new QAction(QStringLiteral("Motion Tracking"), this);
    m_motionTrackingAction->setCheckable(true);
    m_autoPanAction->setCheckable(true);
    m_insertionFollowsPlaybackAction->setCheckable(true);
    m_insertionFollowsPlaybackAction->setChecked(false);
    m_showAllNodeNamesAction->setCheckable(true);
    m_showAllNodeNamesAction->setChecked(true);
    m_showClipEditorAction->setCheckable(true);
    m_showClipEditorAction->setChecked(false);
    m_showTimelineAction->setCheckable(true);
    m_showTimelineAction->setChecked(true);
    m_showMixAction->setCheckable(true);
    m_showMixAction->setChecked(false);
    m_timelineClickSeeksAction->setCheckable(true);
    m_timelineClickSeeksAction->setChecked(true);
    m_audioPoolAction->setCheckable(true);
    m_audioPoolAction->setChecked(false);
    m_importSoundAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    m_toggleDebugAction = new QAction(QStringLiteral("Toggle Debug"), this);
    m_toggleDebugAction->setCheckable(true);
    m_toggleDebugAction->setChecked(true);
    m_showNativeViewportAction = new QAction(QStringLiteral("Native Video Viewport Test"), this);
    m_showNativeViewportAction->setCheckable(true);
    m_showNativeViewportAction->setChecked(false);

    m_setNodeStartAction->setEnabled(false);
    m_setNodeEndAction->setEnabled(false);
    m_clearLoopRangeAction->setEnabled(false);
    m_selectNextNodeAction->setEnabled(false);
    m_moveNodeUpAction->setEnabled(false);
    m_moveNodeDownAction->setEnabled(false);
    m_moveNodeLeftAction->setEnabled(false);
    m_moveNodeRightAction->setEnabled(false);
    m_trimNodeAction->setEnabled(false);
    m_autoPanAction->setEnabled(false);
    m_toggleNodeNameAction->setEnabled(false);
    m_importSoundAction->setEnabled(false);
    m_copyAction->setEnabled(false);
    m_pasteAction->setEnabled(false);
    m_cutAction->setEnabled(false);
    m_undoAction->setEnabled(false);
    m_redoAction->setEnabled(false);
    m_deleteNodeAction->setEnabled(false);
    m_selectAllAction->setEnabled(false);
    m_unselectAllAction->setEnabled(false);
    m_clearAllAction->setEnabled(false);

    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(m_openAction);

    auto* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(m_copyAction);
    editMenu->addAction(m_pasteAction);
    editMenu->addAction(m_cutAction);
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_insertionFollowsPlaybackAction);
    editMenu->addAction(m_selectAllAction);
    editMenu->addAction(m_unselectAllAction);

    auto* nodeMenu = menuBar()->addMenu(QStringLiteral("&Node"));
    nodeMenu->addAction(m_selectNextNodeAction);
    nodeMenu->addSeparator();
    nodeMenu->addAction(m_moveNodeUpAction);
    nodeMenu->addAction(m_moveNodeDownAction);
    nodeMenu->addAction(m_moveNodeLeftAction);
    nodeMenu->addAction(m_moveNodeRightAction);
    nodeMenu->addSeparator();
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
    soundMenu->addAction(m_showClipEditorAction);
    soundMenu->addAction(m_autoPanAction);

    auto* timelineMenu = menuBar()->addMenu(QStringLiteral("&Timeline"));
    timelineMenu->addAction(m_goToStartAction);
    timelineMenu->addAction(m_playAction);
    timelineMenu->addAction(m_stepForwardAction);
    timelineMenu->addAction(m_stepBackAction);
    timelineMenu->addAction(m_stepFastForwardAction);
    timelineMenu->addAction(m_stepFastBackAction);
    timelineMenu->addAction(m_insertionFollowsPlaybackAction);
    timelineMenu->addSeparator();
    timelineMenu->addAction(m_setLoopStartAction);
    timelineMenu->addAction(m_setLoopEndAction);
    timelineMenu->addAction(m_clearLoopRangeAction);
    timelineMenu->addSeparator();
    timelineMenu->addAction(m_showTimelineAction);
    timelineMenu->addAction(m_timelineClickSeeksAction);

    auto* mixMenu = menuBar()->addMenu(QStringLiteral("&Mix"));
    mixMenu->addAction(m_showMixAction);

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(m_toggleNodeNameAction);
    viewMenu->addAction(m_showAllNodeNamesAction);
    viewMenu->addAction(m_audioPoolAction);

    auto* debugMenu = menuBar()->addMenu(QStringLiteral("&Debug"));
    debugMenu->addAction(m_toggleDebugAction);
    debugMenu->addAction(m_showNativeViewportAction);
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
    m_frameLabel = new QLabel(QStringLiteral("Frame 0  |  0.00 s"), menuBar());
    m_frameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_frameLabel->setMinimumWidth(180);
    m_playPauseShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    m_startShortcut = new QShortcut(QKeySequence(Qt::Key_Return), this);
    m_numpadStartShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), this);
    m_stepBackShortcut = new QShortcut(QKeySequence(Qt::Key_Comma), this);
    m_stepForwardShortcut = new QShortcut(QKeySequence(Qt::Key_Period), this);
    m_stepFastForwardShortcut = new QShortcut(QKeySequence(Qt::Key_Minus), this);
    m_stepFastBackShortcut = new QShortcut(QKeySequence(Qt::Key_M), this);
    m_insertionFollowsPlaybackShortcut = new QShortcut(QKeySequence(Qt::Key_N), this);
    m_copyShortcut = new QShortcut(QKeySequence::Copy, this);
    m_pasteShortcut = new QShortcut(QKeySequence::Paste, this);
    m_cutShortcut = new QShortcut(QKeySequence::Cut, this);
    m_undoShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z), this);
    m_redoShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y), this);
    m_selectAllShortcut = new QShortcut(QKeySequence::SelectAll, this);
    m_nodeStartShortcut = new QShortcut(QKeySequence(Qt::Key_A), this);
    m_nodeEndShortcut = new QShortcut(QKeySequence(Qt::Key_S), this);
    m_selectNextNodeShortcut = new QShortcut(QKeySequence(Qt::Key_Tab), this);
    m_showTimelineShortcut = new QShortcut(QKeySequence(Qt::Key_T), this);
    m_showClipEditorShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+-")), this);
    m_showMixShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl++")), this);
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
    m_stepFastForwardShortcut->setContext(Qt::ApplicationShortcut);
    m_stepFastBackShortcut->setContext(Qt::ApplicationShortcut);
    m_insertionFollowsPlaybackShortcut->setContext(Qt::ApplicationShortcut);
    m_copyShortcut->setContext(Qt::ApplicationShortcut);
    m_pasteShortcut->setContext(Qt::ApplicationShortcut);
    m_cutShortcut->setContext(Qt::ApplicationShortcut);
    m_undoShortcut->setContext(Qt::ApplicationShortcut);
    m_redoShortcut->setContext(Qt::ApplicationShortcut);
    m_selectAllShortcut->setContext(Qt::ApplicationShortcut);
    m_nodeStartShortcut->setContext(Qt::ApplicationShortcut);
    m_nodeEndShortcut->setContext(Qt::ApplicationShortcut);
    m_selectNextNodeShortcut->setContext(Qt::ApplicationShortcut);
    m_showTimelineShortcut->setContext(Qt::ApplicationShortcut);
    m_showClipEditorShortcut->setContext(Qt::ApplicationShortcut);
    m_showMixShortcut->setContext(Qt::ApplicationShortcut);
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
    m_canvas->setRenderService(m_controller->renderService());
    m_nativeViewport = new NativeVideoViewport(nullptr);
    m_nativeViewport->setWindowTitle(QStringLiteral("Native Video Viewport Test"));
    m_nativeViewport->resize(960, 540);
    m_nativeViewport->hide();
    m_nativeViewport->installEventFilter(this);
    m_nativeViewport->setRenderService(nullptr);
    m_nativeViewportWindow = m_nativeViewport;
    connect(this, &QObject::destroyed, m_nativeViewportWindow, &QObject::deleteLater);
    m_timelinePanel = new QFrame(m_mainVerticalSplitter);
    m_timelinePanel->setObjectName(QStringLiteral("timelinePanel"));
    m_timelinePanel->setFrameShape(QFrame::NoFrame);
    m_timelinePanel->setMinimumHeight(72);
    auto* timelinePanelLayout = new QVBoxLayout(m_timelinePanel);
    timelinePanelLayout->setContentsMargins(0, 0, 0, 0);
    timelinePanelLayout->setSpacing(0);

    m_timeline = new TimelineView(m_timelinePanel);
    timelinePanelLayout->addWidget(m_timeline, 1);

    m_clipEditorPanel = new QFrame(m_mainVerticalSplitter);
    m_clipEditorPanel->setObjectName(QStringLiteral("clipEditorPanel"));
    m_clipEditorPanel->setVisible(false);
    m_clipEditorPanel->setFrameShape(QFrame::NoFrame);
    m_clipEditorPanel->setMinimumHeight(148);
    auto* clipEditorPanelLayout = new QVBoxLayout(m_clipEditorPanel);
    clipEditorPanelLayout->setContentsMargins(0, 0, 0, 0);
    clipEditorPanelLayout->setSpacing(0);

    m_clipEditorView = new ClipEditorView(m_clipEditorPanel);
    clipEditorPanelLayout->addWidget(m_clipEditorView, 1);

    m_mixPanel = new QFrame(m_mainVerticalSplitter);
    m_mixPanel->setObjectName(QStringLiteral("mixPanel"));
    m_mixPanel->setVisible(false);
    m_mixPanel->setFrameShape(QFrame::NoFrame);
    m_mixPanel->setMinimumHeight(220);
    auto* mixPanelLayout = new QVBoxLayout(m_mixPanel);
    mixPanelLayout->setContentsMargins(0, 0, 0, 0);
    mixPanelLayout->setSpacing(0);

    m_mixView = new MixView(m_mixPanel);
    mixPanelLayout->addWidget(m_mixView, 1);

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
    m_audioPoolMenuButton = new QToolButton(m_audioPoolPanel);
    m_audioPoolMenuButton->setCursor(Qt::PointingHandCursor);
    m_audioPoolMenuButton->setText(QStringLiteral("\u2630"));
    m_audioPoolMenuButton->setFixedWidth(28);
    m_audioPoolMenuButton->setToolTip(QStringLiteral("Audio Pool options"));
    m_audioPoolMenuButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; color: #d7dee8; border: none; border-radius: 4px; padding: 1px 4px; font-size: 8.5pt; }"
        "QToolButton:hover { background: rgba(255, 255, 255, 0.08); }"
        "QToolButton:pressed { background: #111821; }"));
    audioPoolHeader->addWidget(audioPoolTitle);
    audioPoolHeader->addStretch(1);
    audioPoolHeader->addWidget(m_audioPoolMenuButton);
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
    m_videoAudioMuteButton->setCursor(Qt::PointingHandCursor);
    m_videoAudioMuteButton->setFixedWidth(28);
    m_videoAudioMuteButton->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  background: transparent;"
        "  color: #d7dee8;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 1px 4px;"
        "  font-size: 8.5pt;"
        "}"
        "QToolButton:hover { background: rgba(255, 255, 255, 0.08); }"
        "QToolButton:pressed { background: #111821; }"));
    connect(m_videoAudioMuteButton, &QToolButton::clicked, this, [this]()
    {
        if (!m_videoAudioMuteButton)
        {
            return;
        }

        QMenu menu(m_videoAudioMuteButton);
        auto menuFont = menu.font();
        menuFont.setPointSizeF(8.5);
        menu.setFont(menuFont);
        menu.setStyleSheet(QStringLiteral(
            "QMenu { background: rgba(8, 10, 13, 204); color: #eef2f6; border: 1px solid #1f2935; border-radius: 8px; padding: 4px 0; font-size: 8.5pt; }"
            "QMenu::item { padding: 6px 14px; }"
            "QMenu::item:selected { background: #1a2028; }"));

        const auto muted = m_controller->isEmbeddedVideoAudioMuted();
        const auto fastPlaybackEnabled = m_controller->isFastPlaybackEnabled();
        auto* toggleMuteAction = menu.addAction(muted ? QStringLiteral("Unmute") : QStringLiteral("Mute"));
        auto* fastPlaybackAction = menu.addAction(QStringLiteral("Fast Playback"));
        fastPlaybackAction->setCheckable(true);
        fastPlaybackAction->setChecked(fastPlaybackEnabled);

        const auto popupWidth = menu.sizeHint().width();
        auto popupPoint = m_videoAudioMuteButton->mapToGlobal(
            QPoint(m_videoAudioMuteButton->width() - popupWidth, m_videoAudioMuteButton->height()));
        if (const auto* screen = m_videoAudioMuteButton->screen())
        {
            const auto screenRect = screen->availableGeometry();
            if (popupPoint.x() + popupWidth > screenRect.right())
            {
                popupPoint.setX(screenRect.right() - popupWidth);
            }
            popupPoint.setX(std::max(screenRect.left(), popupPoint.x()));
        }

        const auto* chosenAction = menu.exec(popupPoint);
        if (chosenAction == toggleMuteAction)
        {
            m_controller->toggleEmbeddedVideoAudioMuted();
        }
        else if (chosenAction == fastPlaybackAction)
        {
            m_controller->setFastPlaybackEnabled(!fastPlaybackEnabled);
        }
    });

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
    connect(m_audioPoolMenuButton, &QToolButton::clicked, this, [this]()
    {
        if (!m_audioPoolMenuButton)
        {
            return;
        }

        QMenu menu(m_audioPoolMenuButton);
        auto menuFont = menu.font();
        menuFont.setPointSizeF(8.5);
        menu.setFont(menuFont);
        menu.setStyleSheet(QStringLiteral(
            "QMenu { background: rgba(8, 10, 13, 204); color: #eef2f6; border: 1px solid #1f2935; border-radius: 8px; padding: 4px 0; font-size: 8.5pt; }"
            "QMenu::item { padding: 6px 14px; }"
            "QMenu::item:selected { background: #1a2028; }"));

        auto* importAction = menu.addAction(QStringLiteral("Import (Shift+Ctrl+I)"));
        auto* closeAction = menu.addAction(QStringLiteral("Close (P)"));

        const auto popupWidth = menu.sizeHint().width();
        auto popupPoint = m_audioPoolMenuButton->mapToGlobal(
            QPoint(m_audioPoolMenuButton->width() - popupWidth, m_audioPoolMenuButton->height()));
        if (const auto* screen = m_audioPoolMenuButton->screen())
        {
            const auto screenRect = screen->availableGeometry();
            if (popupPoint.x() + popupWidth > screenRect.right())
            {
                popupPoint.setX(screenRect.right() - popupWidth);
            }
            popupPoint.setX(std::max(screenRect.left(), popupPoint.x()));
        }

        const auto* chosenAction = menu.exec(popupPoint);
        if (chosenAction == importAction)
        {
            importAudioToPool();
        }
        else if (chosenAction == closeAction)
        {
            updateAudioPoolVisibility(false);
            showStatus(QStringLiteral("Audio Pool hidden."));
        }
    });

    m_mainVerticalSplitter->addWidget(m_canvas);
    m_mainVerticalSplitter->addWidget(m_timelinePanel);
    m_mainVerticalSplitter->addWidget(m_clipEditorPanel);
    m_mainVerticalSplitter->addWidget(m_mixPanel);
    m_mainVerticalSplitter->setStretchFactor(0, 1);
    m_mainVerticalSplitter->setStretchFactor(1, 0);
    m_mainVerticalSplitter->setStretchFactor(2, 0);
    m_mainVerticalSplitter->setStretchFactor(3, 0);
    m_mainVerticalSplitter->setSizes({700, m_timelinePreferredHeight, 0, 0});
    connect(m_mainVerticalSplitter, &QSplitter::splitterMoved, this, [this]()
    {
        if (m_timelinePanel && m_timelinePanel->isVisible())
        {
            m_timelinePreferredHeight = std::max(96, m_timelinePanel->height());
        }
        if (m_clipEditorPanel && m_clipEditorPanel->isVisible())
        {
            m_clipEditorPreferredHeight = std::max(148, m_clipEditorPanel->height());
        }
        if (m_mixPanel && m_mixPanel->isVisible())
        {
            m_mixPreferredHeight = std::max(132, m_mixPanel->height());
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
    menuBar()->setCornerWidget(m_frameLabel, Qt::TopRightCorner);
    statusBar()->hide();

    auto* debugOverlay = new DebugOverlayWindow();
    m_debugOverlay = debugOverlay;
    connect(this, &QObject::destroyed, debugOverlay, &QObject::deleteLater);
    m_debugOverlay->move(mapToGlobal(QPoint(16, 48)));
    m_debugOverlay->setVisible(m_debugVisible);
    connect(debugOverlay, &DebugOverlayWindow::closeRequested, this, [this]()
    {
        updateDebugVisibility(false);
        showStatus(QStringLiteral("Debug window hidden."));
    });

    m_statusToast = new QLabel(root);
    m_statusToast->setObjectName(QStringLiteral("statusToast"));
    m_statusToast->setWordWrap(true);
    m_statusToast->hide();

    m_canvasTipsOverlay = new QLabel(m_canvas);
    m_canvasTipsOverlay->setObjectName(QStringLiteral("canvasTipsOverlay"));
    m_canvasTipsOverlay->setWordWrap(true);
    m_canvasTipsOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_canvasTipsOverlay->setMaximumWidth(300);
    m_canvasTipsOverlay->hide();
    updateOverlayPositions();

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
            background: rgba(18, 23, 32, 204);
            color: #f3f5f7;
            border: 1px solid #324155;
            border-radius: 8px;
            font-size: 8.5pt;
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
        QStatusBar QLabel {
            color: #b8c6d4;
            font-size: 9pt;
            padding-right: 10px;
        }
        QLabel#statusToast {
            color: #f2f5f8;
            font-size: 9pt;
            background: rgba(11, 15, 20, 210);
            border: 1px solid #324155;
            border-radius: 8px;
            padding: 8px 12px;
        }
        QLabel#canvasTipsOverlay {
            color: #dce4ec;
            font-size: 9pt;
            background: rgba(11, 15, 20, 156);
            border: 1px solid rgba(80, 98, 123, 180);
            border-radius: 8px;
            padding: 8px 10px;
        }
        QFrame {
            background: #0f141b;
        }
        QFrame#audioPoolPanel {
            background: #07090c;
        }
        QFrame#mixPanel {
            background: #080b10;
            border-top: 1px solid #131a23;
        }
        QScrollArea#audioPoolScroll {
            background: #07090c;
            border: none;
        }
        QWidget#audioPoolListContainer {
            background: #07090c;
        }
        QFrame#debugOverlay {
            background: transparent;
            border: none;
        }
        QWidget#debugOverlayTitleBar {
            background: rgba(17, 24, 33, 128);
            border: 1px solid #253142;
            border-radius: 8px;
        }
        QLabel#debugOverlayTitle {
            color: #f3f5f7;
            font-weight: 600;
        }
        QLabel#debugOverlayText {
            color: #d8dde4;
            font-size: 9pt;
            padding: 10px;
            background: rgba(11, 15, 20, 128);
            border: 1px solid #253142;
            border-radius: 8px;
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

        const QStringList preferredVideoNames{
            QStringLiteral("test-video-2.mp4"),
            QStringLiteral("test-video-2.MP4"),
            QStringLiteral("test-video.mov"),
            QStringLiteral("test-video.MOV")
        };

        QString selectedVideoPath;
        for (const auto& fileName : preferredVideoNames)
        {
            const auto candidatePath = devDir.filePath(fileName);
            if (QFileInfo::exists(candidatePath))
            {
                selectedVideoPath = candidatePath;
                break;
            }
        }

        if (selectedVideoPath.isEmpty())
        {
            continue;
        }

        if (m_controller->openVideo(selectedVideoPath))
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
