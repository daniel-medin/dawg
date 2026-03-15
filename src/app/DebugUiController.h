#pragma once

#include <QImage>

class MainWindow;

class DebugUiController
{
public:
    explicit DebugUiController(MainWindow& window);

    void resetOutputFpsTracking();
    void updateFrame(const QImage& image, int frameIndex, double timestampSeconds);
    void updateMemoryUsage();
    void updateDebugText();
    void handleVideoLoaded(const QString& filePath, int totalFrames, double fps);
    void updateDebugVisibility(bool enabled);
    void updateNativeViewportVisibility(bool visible);

private:
    MainWindow& m_window;
};
