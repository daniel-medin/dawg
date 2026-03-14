#pragma once

#include <memory>

#include <QImage>
#include <QString>

#include "core/video/VideoFrame.h"

class IRenderBackend;

class RenderService
{
public:
    RenderService();
    ~RenderService();

    [[nodiscard]] QString backendName() const;
    [[nodiscard]] bool isHardwareAccelerated() const;
    [[nodiscard]] QImage presentFrame(const VideoFrame& frame);

private:
    std::unique_ptr<IRenderBackend> m_backend;
};
