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

QImage RenderService::presentFrame(const VideoFrame& frame)
{
    // The current UI still presents QImage through Qt, so eagerly uploading every
    // frame into D3D11 only adds CPU/GPU overhead without changing what is shown.
    Q_UNUSED(m_backend);
    return frame.cpuImage;
}
