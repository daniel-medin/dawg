#pragma once

#include <memory>

#include <QImage>
#include <QRectF>
#include <QString>
#include <QSize>
#include <QWidget>

#include "core/video/VideoFrame.h"

class IRenderBackend;

class RenderService
{
public:
    RenderService();
    ~RenderService();

    [[nodiscard]] QString backendName() const;
    [[nodiscard]] bool isHardwareAccelerated() const;
    void setFastPlaybackEnabled(bool enabled);
    [[nodiscard]] bool fastPlaybackEnabled() const;
    [[nodiscard]] QImage presentFrame(const VideoFrame& frame, bool playbackActive);
    [[nodiscard]] bool canPresentToNativeWindow() const;
    bool presentToNativeWindow(
        QWidget* widget,
        const QSize& surfaceSize,
        const VideoFrame& frame,
        const QRectF& targetRect,
        const QImage& overlayImage);

private:
    std::unique_ptr<IRenderBackend> m_backend;
    bool m_fastPlaybackEnabled = false;
};
