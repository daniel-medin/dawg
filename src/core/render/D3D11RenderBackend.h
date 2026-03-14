#pragma once

#include <memory>

#include "core/render/IRenderBackend.h"

class D3D11RenderBackend final : public IRenderBackend
{
public:
    D3D11RenderBackend();
    ~D3D11RenderBackend() override;

    [[nodiscard]] bool isReady() const override;
    [[nodiscard]] QString backendName() const override;
    [[nodiscard]] QImage renderFrame(const VideoFrame& frame) override;

private:
    struct Impl;

    std::unique_ptr<Impl> m_impl;
};
