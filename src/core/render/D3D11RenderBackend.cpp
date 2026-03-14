#include "core/render/D3D11RenderBackend.h"

#include <cstddef>
#include <cstring>
#include <memory>

#include <QImage>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>
#endif

struct D3D11RenderBackend::Impl
{
#ifdef Q_OS_WIN
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* deviceContext = nullptr;
    ID3D11Texture2D* frameTexture = nullptr;
    QSize textureSize;

    ~Impl()
    {
        if (frameTexture)
        {
            frameTexture->Release();
        }

        if (deviceContext)
        {
            deviceContext->Release();
        }

        if (device)
        {
            device->Release();
        }
    }

    void ensureFrameTexture(const QSize& imageSize)
    {
        if (!device || !deviceContext || !imageSize.isValid())
        {
            return;
        }

        if (frameTexture
            && textureSize.width() == imageSize.width()
            && textureSize.height() == imageSize.height())
        {
            return;
        }

        if (frameTexture)
        {
            frameTexture->Release();
            frameTexture = nullptr;
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(imageSize.width());
        description.Height = static_cast<UINT>(imageSize.height());
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        if (FAILED(device->CreateTexture2D(&description, nullptr, &frameTexture)))
        {
            frameTexture = nullptr;
            textureSize = {};
            return;
        }

        textureSize = imageSize;
    }

    void uploadFrame(const QImage& image)
    {
        ensureFrameTexture(image.size());
        if (!frameTexture || image.isNull())
        {
            return;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(deviceContext->Map(frameTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            return;
        }

        const auto bytesPerRow = static_cast<std::size_t>(image.width()) * 4;
        for (int row = 0; row < image.height(); ++row)
        {
            const auto* sourceRow = image.constBits() + (row * image.bytesPerLine());
            auto* destinationRow = static_cast<std::byte*>(mapped.pData) + (row * mapped.RowPitch);
            std::memcpy(destinationRow, sourceRow, bytesPerRow);
        }

        deviceContext->Unmap(frameTexture, 0);
    }
#endif
};

D3D11RenderBackend::D3D11RenderBackend()
    : m_impl(std::make_unique<Impl>())
{
#ifdef Q_OS_WIN
    D3D_FEATURE_LEVEL featureLevel{};
    D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &m_impl->device,
        &featureLevel,
        &m_impl->deviceContext);
#endif
}

D3D11RenderBackend::~D3D11RenderBackend() = default;

bool D3D11RenderBackend::isReady() const
{
#ifdef Q_OS_WIN
    return m_impl && m_impl->device && m_impl->deviceContext;
#else
    return false;
#endif
}

QString D3D11RenderBackend::backendName() const
{
    return QStringLiteral("Direct3D 11");
}

QImage D3D11RenderBackend::renderFrame(const VideoFrame& frame)
{
    if (frame.cpuImage.isNull())
    {
        return {};
    }

    const auto uploadImage = frame.cpuImage.convertToFormat(QImage::Format_ARGB32);
#ifdef Q_OS_WIN
    if (isReady())
    {
        m_impl->uploadFrame(uploadImage);
    }
#endif
    return uploadImage;
}
