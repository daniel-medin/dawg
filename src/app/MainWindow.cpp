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
#include <QShortcut>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

#include "app/PlayerController.h"
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
    qApp->installEventFilter(this);
    m_clearAllShortcutTimer.setSingleShot(true);
    m_clearAllShortcutTimer.setInterval(1500);
    m_memoryUsageTimer.setInterval(1000);
    connect(&m_clearAllShortcutTimer, &QTimer::timeout, this, &MainWindow::clearPendingClearAllShortcut);
    connect(&m_memoryUsageTimer, &QTimer::timeout, this, &MainWindow::updateMemoryUsage);

    connect(m_openButton, &QPushButton::clicked, this, &MainWindow::openVideo);
    connect(
        m_motionTrackingButton,
        &QPushButton::toggled,
        m_controller,
        &PlayerController::setMotionTrackingEnabled);
    connect(m_startButton, &QPushButton::clicked, m_controller, &PlayerController::goToStart);
    connect(m_playButton, &QPushButton::clicked, m_controller, &PlayerController::togglePlayback);
    connect(m_stepBackButton, &QPushButton::clicked, m_controller, &PlayerController::stepBackward);
    connect(m_stepButton, &QPushButton::clicked, m_controller, &PlayerController::stepForward);
    connect(m_deleteButton, &QPushButton::clicked, m_controller, &PlayerController::deleteSelectedTrack);
    connect(m_clearAllButton, &QPushButton::clicked, m_controller, &PlayerController::clearAllTracks);
    connect(m_canvas, &VideoCanvas::seedPointRequested, m_controller, &PlayerController::seedTrack);
    connect(m_canvas, &VideoCanvas::trackSelected, m_controller, &PlayerController::selectTrack);
    connect(m_canvas, &VideoCanvas::selectedTrackMoved, m_controller, &PlayerController::moveSelectedTrack);
    connect(m_playPauseShortcut, &QShortcut::activated, m_controller, &PlayerController::togglePlayback);
    connect(m_startShortcut, &QShortcut::activated, m_controller, &PlayerController::goToStart);
    connect(m_numpadStartShortcut, &QShortcut::activated, m_controller, &PlayerController::goToStart);
    connect(m_stepBackShortcut, &QShortcut::activated, m_controller, &PlayerController::stepBackward);
    connect(m_stepForwardShortcut, &QShortcut::activated, m_controller, &PlayerController::stepForward);
    connect(m_deleteShortcut, &QShortcut::activated, m_controller, &PlayerController::deleteSelectedTrack);
    connect(m_controller, &PlayerController::frameReady, this, &MainWindow::updateFrame);
    connect(m_controller, &PlayerController::overlaysChanged, this, &MainWindow::refreshOverlays);
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
    syncMotionTrackingUi(m_controller->isMotionTrackingEnabled());
    updateSelectionState(m_controller->hasSelection());
    updateTrackAvailabilityState(m_controller->hasTracks());
    updateMemoryUsage();
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

void MainWindow::updateFrame(const QImage& image, const int frameIndex, const double timestampSeconds)
{
    m_canvas->setFrame(image);
    m_frameLabel->setText(
        QStringLiteral("Frame %1  |  %2 s")
            .arg(frameIndex)
            .arg(timestampSeconds, 0, 'f', 2));
}

void MainWindow::updateMemoryUsage()
{
    m_memoryLabel->setText(currentMemoryUsageText());
}

void MainWindow::refreshOverlays()
{
    m_canvas->setOverlays(m_controller->currentOverlays());
}

void MainWindow::updatePlaybackState(const bool playing)
{
    m_playButton->setText(playing ? QStringLiteral("Pause (Space)") : QStringLiteral("Play (Space)"));
}

void MainWindow::updateMotionTrackingState(const bool enabled)
{
    syncMotionTrackingUi(enabled);
}

void MainWindow::updateSelectionState(const bool hasSelection)
{
    m_deleteButton->setEnabled(hasSelection);
}

void MainWindow::updateTrackAvailabilityState(const bool hasTracks)
{
    m_clearAllButton->setEnabled(hasTracks);
    if (!hasTracks)
    {
        clearPendingClearAllShortcut();
    }
}

void MainWindow::handleVideoLoaded(const QString& filePath, const int totalFrames, const double fps)
{
    const QFileInfo fileInfo{filePath};
    m_clipLabel->setText(
        QStringLiteral("%1  |  %2 frames  |  %3 fps")
            .arg(fileInfo.fileName())
            .arg(totalFrames)
            .arg(fps, 0, 'f', 2));
}

void MainWindow::showStatus(const QString& message)
{
    statusBar()->showMessage(message, 5000);
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("dawg"));
    resize(1400, 900);

    auto* root = new QWidget(this);
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    auto* transportRow = new QHBoxLayout();
    transportRow->setSpacing(12);

    m_openButton = new QPushButton(QStringLiteral("Open Video"), root);
    m_motionTrackingButton = new QPushButton(QStringLiteral("Motion Tracking"), root);
    m_motionTrackingButton->setCheckable(true);
    m_motionTrackingButton->setChecked(false);
    m_startButton = new QPushButton(QStringLiteral("Start (Enter)"), root);
    m_playButton = new QPushButton(QStringLiteral("Play (Space)"), root);
    m_stepBackButton = new QPushButton(QStringLiteral("Step Back (,)"), root);
    m_stepButton = new QPushButton(QStringLiteral("Step (.)"), root);
    m_deleteButton = new QPushButton(QStringLiteral("Delete (Backspace)"), root);
    m_clearAllButton = new QPushButton(QStringLiteral("Clear All (Ctrl+A, Backspace)"), root);
    m_deleteButton->setEnabled(false);
    m_clearAllButton->setEnabled(false);
    m_clipLabel = new QLabel(QStringLiteral("No clip loaded"), root);
    m_frameLabel = new QLabel(QStringLiteral("Frame --"), root);
    m_memoryLabel = new QLabel(currentMemoryUsageText(), root);
    m_playPauseShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    m_startShortcut = new QShortcut(QKeySequence(Qt::Key_Return), this);
    m_numpadStartShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), this);
    m_stepBackShortcut = new QShortcut(QKeySequence(Qt::Key_Comma), this);
    m_stepForwardShortcut = new QShortcut(QKeySequence(Qt::Key_Period), this);
    m_deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    m_playPauseShortcut->setContext(Qt::ApplicationShortcut);
    m_startShortcut->setContext(Qt::ApplicationShortcut);
    m_numpadStartShortcut->setContext(Qt::ApplicationShortcut);
    m_stepBackShortcut->setContext(Qt::ApplicationShortcut);
    m_stepForwardShortcut->setContext(Qt::ApplicationShortcut);
    m_deleteShortcut->setContext(Qt::ApplicationShortcut);

    transportRow->addWidget(m_openButton);
    transportRow->addWidget(m_motionTrackingButton);
    transportRow->addWidget(m_startButton);
    transportRow->addWidget(m_playButton);
    transportRow->addWidget(m_stepBackButton);
    transportRow->addWidget(m_stepButton);
    transportRow->addWidget(m_deleteButton);
    transportRow->addWidget(m_clearAllButton);
    transportRow->addSpacing(12);
    transportRow->addWidget(m_clipLabel, 1);
    transportRow->addWidget(m_memoryLabel);
    transportRow->addWidget(m_frameLabel);

    m_hintLabel = new QLabel(root);
    m_hintLabel->setWordWrap(true);

    m_canvas = new VideoCanvas(root);

    layout->addLayout(transportRow);
    layout->addWidget(m_hintLabel);
    layout->addWidget(m_canvas, 1);

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
        QPushButton {
            background: #1a222d;
            border: 1px solid #324155;
            border-radius: 8px;
            padding: 10px 16px;
        }
        QPushButton:hover {
            background: #223146;
        }
        QPushButton:checked {
            background: #2f5d3a;
            border-color: #4b9461;
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
    m_motionTrackingButton->setChecked(enabled);
    m_hintLabel->setText(
        enabled
            ? QStringLiteral("Click to add tracked nodes, drag a node to reposition it on the current frame, and use Delete (Backspace) to remove the selected node.")
            : QStringLiteral("Click to add manual nodes, drag a node to reposition it on the current frame, and turn on Motion Tracking if you want new nodes to automatically follow motion."));
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
