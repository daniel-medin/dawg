#pragma once

#include <QImage>
#include <QString>

#include "core/video/VideoFrame.h"

class IRenderBackend
{
public:
    virtual ~IRenderBackend() = default;

    [[nodiscard]] virtual bool isReady() const = 0;
    [[nodiscard]] virtual QString backendName() const = 0;
    [[nodiscard]] virtual QImage renderFrame(const VideoFrame& frame) = 0;
};
