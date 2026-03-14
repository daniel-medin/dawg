#include "core/render/RenderService.h"

#include <memory>

#include "core/render/D3D11RenderBackend.h"
#include "core/render/IRenderBackend.h"

RenderService::RenderService()
    : m_backend(std::make_unique<D3D11RenderBackend>())
{
}

RenderService::~RenderService() = default;

QString RenderService::backendName() const
{
    return m_backend ? m_backend->backendName() : QStringLiteral("Software");
}

bool RenderService::isHardwareAccelerated() const
{
    return m_backend && m_backend->isReady();
}

void RenderService::setFastPlaybackEnabled(const bool enabled)
{
    m_fastPlaybackEnabled = enabled;
}

bool RenderService::fastPlaybackEnabled() const
{
    return m_fastPlaybackEnabled;
}

QImage RenderService::presentFrame(const VideoFrame& frame, const bool playbackActive)
{
    // The current UI still presents QImage through Qt, so eagerly uploading every
    // frame into D3D11 only adds CPU/GPU overhead without changing what is shown.
    Q_UNUSED(m_backend);
    Q_UNUSED(playbackActive);

    return frame.cpuImage;
}

bool RenderService::canPresentToNativeWindow() const
{
    return m_backend && m_backend->canPresentToNativeWindow();
}

bool RenderService::presentToNativeWindow(
    QWidget* widget,
    const QSize& surfaceSize,
    const VideoFrame& frame,
    const QRectF& targetRect,
    const QImage& overlayImage)
{
    return m_backend
        && m_backend->presentToNativeWindow(widget, surfaceSize, frame, targetRect, overlayImage);
}
