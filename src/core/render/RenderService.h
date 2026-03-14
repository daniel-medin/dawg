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
    void setFastPlaybackEnabled(bool enabled);
    [[nodiscard]] bool fastPlaybackEnabled() const;
    [[nodiscard]] QImage presentFrame(const VideoFrame& frame, bool playbackActive);

private:
    std::unique_ptr<IRenderBackend> m_backend;
    bool m_fastPlaybackEnabled = false;
};
