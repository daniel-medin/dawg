#pragma once

#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>

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
    void handleVideoLoaded(const QString& filePath, int totalFrames, double fps);
    void showStatus(const QString& message);

private:
    void buildUi();

    PlayerController* m_controller = nullptr;
    VideoCanvas* m_canvas = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_playButton = nullptr;
    QPushButton* m_stepButton = nullptr;
    QLabel* m_clipLabel = nullptr;
    QLabel* m_frameLabel = nullptr;
    QLabel* m_hintLabel = nullptr;
};
