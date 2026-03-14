#pragma once

#include <QRectF>
#include <QImage>
#include <QString>
#include <QSize>
#include <QWidget>

#include "core/video/VideoFrame.h"

class IRenderBackend
{
public:
    virtual ~IRenderBackend() = default;

    [[nodiscard]] virtual bool isReady() const = 0;
    [[nodiscard]] virtual QString backendName() const = 0;
    [[nodiscard]] virtual QImage renderFrame(const VideoFrame& frame) = 0;
    [[nodiscard]] virtual bool canPresentToNativeWindow() const { return false; }
    virtual bool presentToNativeWindow(
        QWidget* widget,
        const QSize& surfaceSize,
        const VideoFrame& frame,
        const QRectF& targetRect,
        const QImage& overlayImage)
    {
        Q_UNUSED(widget);
        Q_UNUSED(surfaceSize);
        Q_UNUSED(frame);
        Q_UNUSED(targetRect);
        Q_UNUSED(overlayImage);
        return false;
    }
};
