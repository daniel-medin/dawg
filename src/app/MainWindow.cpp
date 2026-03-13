#include "app/MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenuBar>
#include <QSizePolicy>
#include <QShortcut>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

#include "app/PlayerController.h"
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
    connect(m_stepBackAction, &QAction::triggered, m_controller, &PlayerController::stepBackward);
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
    connect(m_deleteNodeAction, &QAction::triggered, m_controller, &PlayerController::deleteSelectedTrack);
    connect(m_clearAllAction, &QAction::triggered, m_controller, &PlayerController::clearAllTracks);
    connect(m_canvas, &VideoCanvas::seedPointRequested, m_controller, &PlayerController::seedTrack);
    connect(m_canvas, &VideoCanvas::trackSelected, m_controller, &PlayerController::selectTrack);
    connect(m_canvas, &VideoCanvas::selectedTrackMoved, m_controller, &PlayerController::moveSelectedTrack);
    connect(m_timeline, &TimelineView::frameRequested, m_controller, &PlayerController::seekToFrame);
    connect(m_toggleDebugAction, &QAction::toggled, this, &MainWindow::updateDebugVisibility);
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
    connect(m_controller, &PlayerController::videoLoaded, this, &MainWindow::handleVideoLoaded);
    connect(m_controller, &PlayerController::statusChanged, this, &MainWindow::showStatus);

    updatePlaybackState(false);
    updateInsertionFollowsPlaybackState(m_controller->isInsertionFollowsPlayback());
    syncMotionTrackingUi(m_controller->isMotionTrackingEnabled());
    updateSelectionState(m_controller->hasSelection());
    updateTrackAvailabilityState(m_controller->hasTracks());
    updateMemoryUsage();
    updateDebugVisibility(true);
    updateDebugText();
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

            if (key == Qt::Key_A && modifiers == Qt::ControlModifier)
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

    m_controller->openVideo(filePath);
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

void MainWindow::updateFrame(const QImage& image, const int frameIndex, const double timestampSeconds)
{
    m_canvas->setFrame(image);
    m_timeline->setCurrentFrame(frameIndex);
    m_frameLabel->setText(
        QStringLiteral("Frame %1  |  %2 s")
            .arg(frameIndex)
            .arg(timestampSeconds, 0, 'f', 2));
    updateDebugText();
}

void MainWindow::updateMemoryUsage()
{
    m_memoryUsageText = currentMemoryUsageText();
    updateDebugText();
}

void MainWindow::updateDebugText()
{
    if (!m_debugMenuLabel)
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

    m_debugMenuLabel->setText(
        QStringLiteral(
            "DEBUG  |  Clip %1  |  Motion %2  |  Insert %3  |  Frame %4/%5  |  Time %6 s  |  FPS %7  |  Nodes %8  |  Selected %9  |  %10")
            .arg(clipText)
            .arg(m_controller->isMotionTrackingEnabled() ? QStringLiteral("On") : QStringLiteral("Off"))
            .arg(insertionText)
            .arg(currentFrame)
            .arg(totalFrames)
            .arg(currentSeconds, 0, 'f', 2)
            .arg(fpsText)
            .arg(m_controller->trackCount())
            .arg(m_controller->hasSelection() ? QStringLiteral("Yes") : QStringLiteral("No"))
            .arg(m_memoryUsageText.isEmpty() ? QStringLiteral("Memory --") : m_memoryUsageText));
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
    m_setNodeStartAction->setEnabled(hasSelection);
    m_setNodeEndAction->setEnabled(hasSelection);
    m_deleteNodeAction->setEnabled(hasSelection);
    updateDebugText();
}

void MainWindow::updateTrackAvailabilityState(const bool hasTracks)
{
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
    if (m_debugMenuLabel)
    {
        m_debugMenuLabel->setVisible(enabled);
    }
    if (m_toggleDebugAction && m_toggleDebugAction->isChecked() != enabled)
    {
        m_toggleDebugAction->setChecked(enabled);
    }
}

void MainWindow::showStatus(const QString& message)
{
    statusBar()->showMessage(message, 5000);
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
    m_stepBackAction = new QAction(QStringLiteral("Step Back (,)"), this);
    m_insertionFollowsPlaybackAction = new QAction(QStringLiteral("Insertion Follows Playback (N)"), this);
    m_unselectAllAction = new QAction(QStringLiteral("Unselect All (Esc)"), this);

    m_setNodeStartAction = new QAction(QStringLiteral("Set Start (A)"), this);
    m_setNodeEndAction = new QAction(QStringLiteral("Set End (S)"), this);
    m_deleteNodeAction = new QAction(QStringLiteral("Delete (Backspace)"), this);
    m_clearAllAction = new QAction(QStringLiteral("Clear All (Ctrl+A, Backspace)"), this);

    m_motionTrackingAction = new QAction(QStringLiteral("Motion Tracking"), this);
    m_motionTrackingAction->setCheckable(true);
    m_insertionFollowsPlaybackAction->setCheckable(true);
    m_insertionFollowsPlaybackAction->setChecked(true);

    m_toggleDebugAction = new QAction(QStringLiteral("Toggle Debug"), this);
    m_toggleDebugAction->setCheckable(true);
    m_toggleDebugAction->setChecked(true);

    m_setNodeStartAction->setEnabled(false);
    m_setNodeEndAction->setEnabled(false);
    m_deleteNodeAction->setEnabled(false);
    m_clearAllAction->setEnabled(false);

    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(m_openAction);

    auto* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(m_goToStartAction);
    editMenu->addAction(m_playAction);
    editMenu->addAction(m_stepBackAction);
    editMenu->addAction(m_insertionFollowsPlaybackAction);
    editMenu->addAction(m_unselectAllAction);

    auto* nodeMenu = menuBar()->addMenu(QStringLiteral("&Node"));
    nodeMenu->addAction(m_setNodeStartAction);
    nodeMenu->addAction(m_setNodeEndAction);
    nodeMenu->addSeparator();
    nodeMenu->addAction(m_deleteNodeAction);
    nodeMenu->addAction(m_clearAllAction);

    auto* motionMenu = menuBar()->addMenu(QStringLiteral("&Motion"));
    motionMenu->addAction(m_motionTrackingAction);

    auto* debugMenu = menuBar()->addMenu(QStringLiteral("&Debug"));
    debugMenu->addAction(m_toggleDebugAction);

    m_debugMenuLabel = new QLabel(menuBar());
    m_debugMenuLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    m_debugMenuLabel->setContentsMargins(12, 0, 8, 0);
    m_debugMenuLabel->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    menuBar()->setCornerWidget(m_debugMenuLabel, Qt::TopRightCorner);
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("dawg"));
    resize(1400, 900);

    auto* root = new QWidget(this);
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(20, 8, 20, 20);
    layout->setSpacing(16);
    m_frameLabel = new QLabel(QStringLiteral("Frame 0  |  0.00 s"), root);
    m_playPauseShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    m_startShortcut = new QShortcut(QKeySequence(Qt::Key_Return), this);
    m_numpadStartShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), this);
    m_stepBackShortcut = new QShortcut(QKeySequence(Qt::Key_Comma), this);
    m_stepForwardShortcut = new QShortcut(QKeySequence(Qt::Key_Period), this);
    m_insertionFollowsPlaybackShortcut = new QShortcut(QKeySequence(Qt::Key_N), this);
    m_nodeStartShortcut = new QShortcut(QKeySequence(Qt::Key_A), this);
    m_nodeEndShortcut = new QShortcut(QKeySequence(Qt::Key_S), this);
    m_deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    m_unselectAllShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    m_playPauseShortcut->setContext(Qt::ApplicationShortcut);
    m_startShortcut->setContext(Qt::ApplicationShortcut);
    m_numpadStartShortcut->setContext(Qt::ApplicationShortcut);
    m_stepBackShortcut->setContext(Qt::ApplicationShortcut);
    m_stepForwardShortcut->setContext(Qt::ApplicationShortcut);
    m_insertionFollowsPlaybackShortcut->setContext(Qt::ApplicationShortcut);
    m_nodeStartShortcut->setContext(Qt::ApplicationShortcut);
    m_nodeEndShortcut->setContext(Qt::ApplicationShortcut);
    m_deleteShortcut->setContext(Qt::ApplicationShortcut);
    m_unselectAllShortcut->setContext(Qt::ApplicationShortcut);

    m_canvas = new VideoCanvas(root);
    m_timeline = new TimelineView(root);
    auto* timelineInfoRow = new QHBoxLayout();
    timelineInfoRow->setSpacing(12);
    timelineInfoRow->addWidget(m_frameLabel);
    timelineInfoRow->addStretch(1);

    layout->addWidget(m_canvas, 1);
    layout->addLayout(timelineInfoRow);
    layout->addWidget(m_timeline);

    setCentralWidget(root);

    setStyleSheet(R"(
        QMainWindow {
            background: #0d1014;
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

        m_controller->openVideo(matches.front().absoluteFilePath());
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
