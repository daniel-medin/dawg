#pragma once

#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QShortcut>

class PlayerController;
class VideoCanvas;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void openVideo();
    void updateFrame(const QImage& image, int frameIndex, double timestampSeconds);
    void refreshOverlays();
    void updatePlaybackState(bool playing);
    void updateMotionTrackingState(bool enabled);
    void updateSelectionState(bool hasSelection);
    void handleVideoLoaded(const QString& filePath, int totalFrames, double fps);
    void showStatus(const QString& message);

private:
    void buildUi();
    void syncMotionTrackingUi(bool enabled);

    PlayerController* m_controller = nullptr;
    VideoCanvas* m_canvas = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_motionTrackingButton = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_playButton = nullptr;
    QPushButton* m_stepBackButton = nullptr;
    QPushButton* m_stepButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QLabel* m_clipLabel = nullptr;
    QLabel* m_frameLabel = nullptr;
    QLabel* m_hintLabel = nullptr;
    QShortcut* m_playPauseShortcut = nullptr;
    QShortcut* m_startShortcut = nullptr;
    QShortcut* m_numpadStartShortcut = nullptr;
    QShortcut* m_stepBackShortcut = nullptr;
    QShortcut* m_stepForwardShortcut = nullptr;
    QShortcut* m_deleteShortcut = nullptr;
};
