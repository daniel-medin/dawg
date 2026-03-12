#include "app/MainWindow.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include "app/PlayerController.h"
#include "ui/VideoCanvas.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_controller(new PlayerController(this))
{
    buildUi();

    connect(m_openButton, &QPushButton::clicked, this, &MainWindow::openVideo);
    connect(m_playButton, &QPushButton::clicked, m_controller, &PlayerController::togglePlayback);
    connect(m_stepButton, &QPushButton::clicked, m_controller, &PlayerController::stepForward);
    connect(m_canvas, &VideoCanvas::seedPointRequested, m_controller, &PlayerController::seedTrack);

    connect(m_controller, &PlayerController::frameReady, this, &MainWindow::updateFrame);
    connect(m_controller, &PlayerController::overlaysChanged, this, &MainWindow::refreshOverlays);
    connect(m_controller, &PlayerController::playbackStateChanged, this, &MainWindow::updatePlaybackState);
    connect(m_controller, &PlayerController::videoLoaded, this, &MainWindow::handleVideoLoaded);
    connect(m_controller, &PlayerController::statusChanged, this, &MainWindow::showStatus);

    updatePlaybackState(false);
    showStatus(QStringLiteral("Open a clip to start tracking."));
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

void MainWindow::refreshOverlays()
{
    m_canvas->setOverlays(m_controller->currentOverlays());
}

void MainWindow::updatePlaybackState(const bool playing)
{
    m_playButton->setText(playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
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
    m_playButton = new QPushButton(QStringLiteral("Play"), root);
    m_stepButton = new QPushButton(QStringLiteral("Step"), root);
    m_clipLabel = new QLabel(QStringLiteral("No clip loaded"), root);
    m_frameLabel = new QLabel(QStringLiteral("Frame --"), root);

    transportRow->addWidget(m_openButton);
    transportRow->addWidget(m_playButton);
    transportRow->addWidget(m_stepButton);
    transportRow->addSpacing(12);
    transportRow->addWidget(m_clipLabel, 1);
    transportRow->addWidget(m_frameLabel);

    m_hintLabel = new QLabel(
        QStringLiteral("Seed a point on the current frame. Playback will advance that point with optical flow."),
        root);
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
        QLabel {
            color: #d8dde4;
        }
        QStatusBar {
            background: #121720;
        }
    )");
}
