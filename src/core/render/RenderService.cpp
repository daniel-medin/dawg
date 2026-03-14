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

    if (m_fastPlaybackEnabled && playbackActive && !frame.cpuImage.isNull())
    {
        const auto reducedImage = frame.cpuImage.scaled(
            std::max(1, frame.cpuImage.width() / 2),
            std::max(1, frame.cpuImage.height() / 2),
            Qt::IgnoreAspectRatio,
            Qt::FastTransformation);

        return reducedImage.scaled(
            frame.cpuImage.width(),
            frame.cpuImage.height(),
            Qt::IgnoreAspectRatio,
            Qt::FastTransformation);
    }

    return frame.cpuImage;
}
