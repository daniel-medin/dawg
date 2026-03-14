#include "core/render/D3D11RenderBackend.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <memory>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTextStream>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace
{
#ifdef Q_OS_WIN
struct Vertex
{
    float x;
    float y;
    float z;
    float u;
    float v;
};

constexpr char kVertexShaderSource[] = R"(
struct VSInput {
    float3 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

PSInput main(VSInput input)
{
    PSInput output;
    output.pos = float4(input.pos, 1.0f);
    output.uv = input.uv;
    return output;
}
)";

constexpr char kPixelShaderSource[] = R"(
Texture2D sourceTexture : register(t0);
SamplerState sourceSampler : register(s0);

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    return sourceTexture.Sample(sourceSampler, uv);
}
)";

void releaseCom(IUnknown*& object)
{
    if (object)
    {
        object->Release();
        object = nullptr;
    }
}

template <typename T>
void releaseTyped(T*& object)
{
    if (object)
    {
        object->Release();
        object = nullptr;
    }
}

struct TextureResource
{
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* shaderResourceView = nullptr;
    QSize size;

    void reset()
    {
        releaseTyped(shaderResourceView);
        releaseTyped(texture);
        size = {};
    }
};

void setQuadVertices(
    std::array<Vertex, 4>& vertices,
    const QRectF& rect,
    const QSize& surfaceSize)
{
    const auto width = std::max(1, surfaceSize.width());
    const auto height = std::max(1, surfaceSize.height());

    const auto left = static_cast<float>((rect.left() / width) * 2.0 - 1.0);
    const auto right = static_cast<float>((rect.right() / width) * 2.0 - 1.0);
    const auto top = static_cast<float>(1.0 - (rect.top() / height) * 2.0);
    const auto bottom = static_cast<float>(1.0 - (rect.bottom() / height) * 2.0);

    vertices = {{
        {left, top, 0.0F, 0.0F, 0.0F},
        {right, top, 0.0F, 1.0F, 0.0F},
        {left, bottom, 0.0F, 0.0F, 1.0F},
        {right, bottom, 0.0F, 1.0F, 1.0F},
    }};
}

QString findRepositoryLogPath()
{
    QDir dir{QCoreApplication::applicationDirPath()};
    while (dir.exists() && !dir.isRoot())
    {
        if (dir.exists(QStringLiteral(".git")))
        {
            return dir.absoluteFilePath(QStringLiteral(".watch-out.log"));
        }

        if (!dir.cdUp())
        {
            break;
        }
    }

    return QDir::current().absoluteFilePath(QStringLiteral(".watch-out.log"));
}

void logD3dEvent(const QString& category, const QString& message)
{
    QFile file(findRepositoryLogPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        return;
    }

    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
           << " [" << category << "] "
           << message
           << '\n';
}

QString hresultToString(const HRESULT result)
{
    return QStringLiteral("0x%1")
        .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'))
        .toUpper();
}

#endif
}

struct D3D11RenderBackend::Impl
{
#ifdef Q_OS_WIN
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* deviceContext = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* renderTargetView = nullptr;
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    ID3D11Buffer* vertexBuffer = nullptr;
    ID3D11SamplerState* samplerState = nullptr;
    ID3D11BlendState* alphaBlendState = nullptr;
    ID3D11RasterizerState* rasterizerState = nullptr;
    TextureResource videoTexture;
    TextureResource overlayTexture;
    HWND hwnd = nullptr;
    QSize swapChainSize;
    QString lastDiagnostic;
    int successfulPresentCount = 0;

    ~Impl()
    {
        releaseSwapChainResources();
        videoTexture.reset();
        overlayTexture.reset();
        releaseTyped(rasterizerState);
        releaseTyped(alphaBlendState);
        releaseTyped(samplerState);
        releaseTyped(vertexBuffer);
        releaseTyped(inputLayout);
        releaseTyped(pixelShader);
        releaseTyped(vertexShader);
        releaseTyped(deviceContext);
        releaseTyped(device);
    }

    void logDiagnostic(const QString& category, const QString& message, const bool force = false)
    {
        const auto diagnostic = category + QLatin1Char('|') + message;
        if (!force && diagnostic == lastDiagnostic)
        {
            return;
        }

        lastDiagnostic = diagnostic;
        logD3dEvent(category, message);
    }

    bool initializeDevice()
    {
        if (device && deviceContext)
        {
            return true;
        }

        UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        D3D_FEATURE_LEVEL featureLevel{};
        const auto result = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            creationFlags,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &device,
            &featureLevel,
            &deviceContext);
        if (FAILED(result) || !device || !deviceContext)
        {
            logDiagnostic(
                QStringLiteral("d3d_init_fail"),
                QStringLiteral("createDevice hr=%1").arg(hresultToString(result)),
                true);
            return false;
        }

        logDiagnostic(
            QStringLiteral("d3d_init_ok"),
            QStringLiteral("featureLevel=0x%1")
                .arg(static_cast<quint32>(featureLevel), 0, 16)
                .toUpper());
        return true;
    }

    void releaseSwapChainResources()
    {
        releaseTyped(renderTargetView);
        if (swapChain)
        {
            swapChain->SetFullscreenState(FALSE, nullptr);
        }
        releaseTyped(swapChain);
        hwnd = nullptr;
        swapChainSize = {};
    }

    bool ensurePipeline()
    {
        if (vertexShader && pixelShader && inputLayout && vertexBuffer && samplerState && alphaBlendState && rasterizerState)
        {
            return true;
        }

        if (!initializeDevice())
        {
            return false;
        }

        ID3DBlob* vertexShaderBlob = nullptr;
        ID3DBlob* pixelShaderBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;

        auto cleanupBlobs = [&]()
        {
            releaseTyped(vertexShaderBlob);
            releaseTyped(pixelShaderBlob);
            releaseTyped(errorBlob);
        };

        if (FAILED(D3DCompile(
                kVertexShaderSource,
                sizeof(kVertexShaderSource) - 1,
                nullptr,
                nullptr,
                nullptr,
                "main",
                "vs_4_0",
                0,
                0,
                &vertexShaderBlob,
                &errorBlob)))
        {
            cleanupBlobs();
            return false;
        }

        if (FAILED(D3DCompile(
                kPixelShaderSource,
                sizeof(kPixelShaderSource) - 1,
                nullptr,
                nullptr,
                nullptr,
                "main",
                "ps_4_0",
                0,
                0,
                &pixelShaderBlob,
                &errorBlob)))
        {
            cleanupBlobs();
            return false;
        }

        if (FAILED(device->CreateVertexShader(
                vertexShaderBlob->GetBufferPointer(),
                vertexShaderBlob->GetBufferSize(),
                nullptr,
                &vertexShader)))
        {
            cleanupBlobs();
            return false;
        }

        if (FAILED(device->CreatePixelShader(
                pixelShaderBlob->GetBufferPointer(),
                pixelShaderBlob->GetBufferSize(),
                nullptr,
                &pixelShader)))
        {
            cleanupBlobs();
            return false;
        }

        const D3D11_INPUT_ELEMENT_DESC inputLayoutDescription[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        if (FAILED(device->CreateInputLayout(
                inputLayoutDescription,
                ARRAYSIZE(inputLayoutDescription),
                vertexShaderBlob->GetBufferPointer(),
                vertexShaderBlob->GetBufferSize(),
                &inputLayout)))
        {
            cleanupBlobs();
            return false;
        }

        D3D11_BUFFER_DESC vertexBufferDescription{};
        vertexBufferDescription.ByteWidth = static_cast<UINT>(sizeof(Vertex) * 4);
        vertexBufferDescription.Usage = D3D11_USAGE_DYNAMIC;
        vertexBufferDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertexBufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&vertexBufferDescription, nullptr, &vertexBuffer)))
        {
            cleanupBlobs();
            return false;
        }

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
        samplerDescription.MinLOD = 0;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&samplerDescription, &samplerState)))
        {
            cleanupBlobs();
            return false;
        }

        D3D11_BLEND_DESC blendDescription{};
        blendDescription.RenderTarget[0].BlendEnable = TRUE;
        blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device->CreateBlendState(&blendDescription, &alphaBlendState)))
        {
            cleanupBlobs();
            return false;
        }

        D3D11_RASTERIZER_DESC rasterizerDescription{};
        rasterizerDescription.FillMode = D3D11_FILL_SOLID;
        rasterizerDescription.CullMode = D3D11_CULL_NONE;
        rasterizerDescription.DepthClipEnable = TRUE;
        if (FAILED(device->CreateRasterizerState(&rasterizerDescription, &rasterizerState)))
        {
            cleanupBlobs();
            return false;
        }

        cleanupBlobs();
        return true;
    }

    bool ensureSwapChain(HWND targetWindow, const QSize& size)
    {
        const auto pipelineReady = ensurePipeline();
        if (!pipelineReady || !targetWindow || !size.isValid())
        {
            logDiagnostic(
                QStringLiteral("d3d_swapchain_skip"),
                QStringLiteral("pipeline=%1 hwnd=%2 size=%3x%4")
                    .arg(pipelineReady ? QStringLiteral("ok") : QStringLiteral("fail"))
                    .arg(targetWindow != nullptr ? QStringLiteral("yes") : QStringLiteral("no"))
                    .arg(size.width())
                    .arg(size.height()));
            return false;
        }

        if (swapChain && hwnd == targetWindow)
        {
            if (swapChainSize == size)
            {
                return ensureRenderTargetView();
            }

            releaseTyped(renderTargetView);
            if (FAILED(swapChain->ResizeBuffers(
                    0,
                    static_cast<UINT>(size.width()),
                    static_cast<UINT>(size.height()),
                    DXGI_FORMAT_B8G8R8A8_UNORM,
                    0)))
            {
                logDiagnostic(
                    QStringLiteral("d3d_resize_fail"),
                    QStringLiteral("size=%1x%2").arg(size.width()).arg(size.height()),
                    true);
                releaseSwapChainResources();
                return false;
            }

            swapChainSize = size;
            return ensureRenderTargetView();
        }

        releaseSwapChainResources();

        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory* factory = nullptr;
        if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))))
        {
            logDiagnostic(QStringLiteral("d3d_swapchain_fail"), QStringLiteral("QueryInterface IDXGIDevice failed"), true);
            return false;
        }

        const auto releaseDxgi = [&]()
        {
            releaseTyped(factory);
            releaseTyped(adapter);
            releaseTyped(dxgiDevice);
        };

        if (FAILED(dxgiDevice->GetAdapter(&adapter))
            || FAILED(adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory))))
        {
            logDiagnostic(QStringLiteral("d3d_swapchain_fail"), QStringLiteral("GetAdapter/GetParent failed"), true);
            releaseDxgi();
            return false;
        }

        DXGI_SWAP_CHAIN_DESC swapChainDescription{};
        swapChainDescription.BufferDesc.Width = static_cast<UINT>(size.width());
        swapChainDescription.BufferDesc.Height = static_cast<UINT>(size.height());
        swapChainDescription.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDescription.SampleDesc.Count = 1;
        swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDescription.BufferCount = 2;
        swapChainDescription.OutputWindow = targetWindow;
        swapChainDescription.Windowed = TRUE;
        swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        const auto result = factory->CreateSwapChain(device, &swapChainDescription, &swapChain);
        releaseDxgi();
        if (FAILED(result) || !swapChain)
        {
            logDiagnostic(
                QStringLiteral("d3d_swapchain_fail"),
                QStringLiteral("CreateSwapChain hr=%1 size=%2x%3")
                    .arg(hresultToString(result))
                    .arg(size.width())
                    .arg(size.height()),
                true);
            releaseSwapChainResources();
            return false;
        }

        hwnd = targetWindow;
        swapChainSize = size;
        logDiagnostic(
            QStringLiteral("d3d_swapchain_ok"),
            QStringLiteral("size=%1x%2 hwnd=0x%3")
                .arg(size.width())
                .arg(size.height())
                .arg(reinterpret_cast<quintptr>(targetWindow), 0, 16)
                .toUpper());
        return ensureRenderTargetView();
    }

    bool ensureRenderTargetView()
    {
        if (!swapChain)
        {
            return false;
        }

        if (renderTargetView)
        {
            return true;
        }

        ID3D11Texture2D* backBuffer = nullptr;
        const auto result = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
        if (FAILED(result) || !backBuffer)
        {
            logDiagnostic(
                QStringLiteral("d3d_rtv_fail"),
                QStringLiteral("GetBuffer hr=%1").arg(hresultToString(result)),
                true);
            releaseTyped(backBuffer);
            return false;
        }

        const auto createResult = device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);
        releaseTyped(backBuffer);
        if (FAILED(createResult) || !renderTargetView)
        {
            logDiagnostic(
                QStringLiteral("d3d_rtv_fail"),
                QStringLiteral("CreateRenderTargetView hr=%1").arg(hresultToString(createResult)),
                true);
            return false;
        }

        logDiagnostic(QStringLiteral("d3d_rtv_ok"), QStringLiteral("ready"));
        return true;
    }

    bool ensureTexture(TextureResource& resource, const QSize& imageSize)
    {
        if (!device || !imageSize.isValid())
        {
            return false;
        }

        if (resource.texture
            && resource.size.width() == imageSize.width()
            && resource.size.height() == imageSize.height())
        {
            return true;
        }

        resource.reset();

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

        if (FAILED(device->CreateTexture2D(&description, nullptr, &resource.texture)))
        {
            logDiagnostic(
                QStringLiteral("d3d_texture_fail"),
                QStringLiteral("CreateTexture2D size=%1x%2").arg(imageSize.width()).arg(imageSize.height()),
                true);
            resource.reset();
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDescription{};
        srvDescription.Format = description.Format;
        srvDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDescription.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateShaderResourceView(resource.texture, &srvDescription, &resource.shaderResourceView)))
        {
            logDiagnostic(
                QStringLiteral("d3d_texture_fail"),
                QStringLiteral("CreateShaderResourceView size=%1x%2").arg(imageSize.width()).arg(imageSize.height()),
                true);
            resource.reset();
            return false;
        }

        resource.size = imageSize;
        return true;
    }

    bool uploadImage(TextureResource& resource, const QImage& image, const QImage::Format uploadFormat)
    {
        const auto preparedImage = image.convertToFormat(uploadFormat);
        if (!ensureTexture(resource, preparedImage.size()))
        {
            return false;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(deviceContext->Map(resource.texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            logDiagnostic(
                QStringLiteral("d3d_upload_fail"),
                QStringLiteral("Map texture size=%1x%2").arg(preparedImage.width()).arg(preparedImage.height()),
                true);
            return false;
        }

        const auto bytesPerRow = static_cast<std::size_t>(preparedImage.width()) * 4;
        for (int row = 0; row < preparedImage.height(); ++row)
        {
            const auto* sourceRow = preparedImage.constBits() + (row * preparedImage.bytesPerLine());
            auto* destinationRow = static_cast<std::byte*>(mapped.pData) + (row * mapped.RowPitch);
            std::memcpy(destinationRow, sourceRow, bytesPerRow);
        }

        deviceContext->Unmap(resource.texture, 0);
        return true;
    }

    bool updateQuadBuffer(const std::array<Vertex, 4>& vertices)
    {
        if (!vertexBuffer)
        {
            return false;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(deviceContext->Map(vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            logDiagnostic(QStringLiteral("d3d_vertex_fail"), QStringLiteral("Map vertex buffer failed"), true);
            return false;
        }

        std::memcpy(mapped.pData, vertices.data(), sizeof(Vertex) * vertices.size());
        deviceContext->Unmap(vertexBuffer, 0);
        return true;
    }

    bool drawTexturedQuad(
        ID3D11ShaderResourceView* shaderResourceView,
        const QRectF& rect,
        const QSize& surfaceSize,
        const bool alphaBlend)
    {
        if (!shaderResourceView || !renderTargetView)
        {
            return false;
        }

        std::array<Vertex, 4> vertices{};
        setQuadVertices(vertices, rect, surfaceSize);
        if (!updateQuadBuffer(vertices))
        {
            return false;
        }

        const UINT stride = sizeof(Vertex);
        const UINT offset = 0;
        deviceContext->IASetInputLayout(inputLayout);
        deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        deviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        deviceContext->VSSetShader(vertexShader, nullptr, 0);
        deviceContext->PSSetShader(pixelShader, nullptr, 0);
        deviceContext->PSSetSamplers(0, 1, &samplerState);
        deviceContext->PSSetShaderResources(0, 1, &shaderResourceView);
        deviceContext->RSSetState(rasterizerState);

        const float blendFactor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
        deviceContext->OMSetBlendState(alphaBlend ? alphaBlendState : nullptr, blendFactor, 0xFFFFFFFF);
        deviceContext->Draw(4, 0);

        ID3D11ShaderResourceView* nullShaderResource = nullptr;
        deviceContext->PSSetShaderResources(0, 1, &nullShaderResource);
        return true;
    }

    bool present(
        QWidget* widget,
        const QSize& surfaceSize,
        const VideoFrame& frame,
        const QRectF& targetRect,
        const QImage& overlayImage)
    {
        if (!widget || !ensureSwapChain(reinterpret_cast<HWND>(widget->winId()), surfaceSize))
        {
            logDiagnostic(
                QStringLiteral("d3d_present_fail"),
                QStringLiteral("precheck widget=%1 surface=%2x%3 frame=%4x%5")
                    .arg(widget != nullptr ? QStringLiteral("yes") : QStringLiteral("no"))
                    .arg(surfaceSize.width())
                    .arg(surfaceSize.height())
                    .arg(frame.cpuImage.width())
                    .arg(frame.cpuImage.height()),
                true);
            return false;
        }

        const auto videoUploadImage = frame.cpuImage.convertToFormat(QImage::Format_RGB32);
        if (!uploadImage(videoTexture, videoUploadImage, QImage::Format_RGB32))
        {
            logDiagnostic(
                QStringLiteral("d3d_present_fail"),
                QStringLiteral("videoUpload frame=%1 size=%2x%3")
                    .arg(frame.index)
                    .arg(videoUploadImage.width())
                    .arg(videoUploadImage.height()),
                true);
            return false;
        }

        auto preparedOverlayImage = overlayImage.isNull()
            ? QImage(surfaceSize, QImage::Format_ARGB32_Premultiplied)
            : overlayImage;
        if (overlayImage.isNull())
        {
            preparedOverlayImage.fill(Qt::transparent);
        }
        if (preparedOverlayImage.isNull()
            || !uploadImage(overlayTexture, preparedOverlayImage, QImage::Format_ARGB32_Premultiplied))
        {
            logDiagnostic(
                QStringLiteral("d3d_present_fail"),
                QStringLiteral("overlayUpload size=%1x%2")
                    .arg(preparedOverlayImage.width())
                    .arg(preparedOverlayImage.height()),
                true);
            return false;
        }

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(surfaceSize.width());
        viewport.Height = static_cast<float>(surfaceSize.height());
        viewport.MinDepth = 0.0F;
        viewport.MaxDepth = 1.0F;

        const float clearColor[4] = {12.0F / 255.0F, 14.0F / 255.0F, 18.0F / 255.0F, 1.0F};
        deviceContext->OMSetRenderTargets(1, &renderTargetView, nullptr);
        deviceContext->RSSetViewports(1, &viewport);
        deviceContext->ClearRenderTargetView(renderTargetView, clearColor);

        if (!drawTexturedQuad(videoTexture.shaderResourceView, targetRect, surfaceSize, false))
        {
            logDiagnostic(QStringLiteral("d3d_present_fail"), QStringLiteral("draw video quad failed"), true);
            return false;
        }

        const QRectF fullRect{0.0, 0.0, static_cast<double>(surfaceSize.width()), static_cast<double>(surfaceSize.height())};
        if (!drawTexturedQuad(overlayTexture.shaderResourceView, fullRect, surfaceSize, true))
        {
            logDiagnostic(QStringLiteral("d3d_present_fail"), QStringLiteral("draw overlay quad failed"), true);
            return false;
        }

        const auto presentResult = swapChain->Present(1, 0);
        if (FAILED(presentResult))
        {
            successfulPresentCount = 0;
            logDiagnostic(
                QStringLiteral("d3d_present_fail"),
                QStringLiteral("Present hr=%1").arg(hresultToString(presentResult)),
                true);
            return false;
        }

        if (successfulPresentCount == 0)
        {
            logDiagnostic(
                QStringLiteral("d3d_present_ok"),
                QStringLiteral("frame=%1 surface=%2x%3 target=%4,%5 %6x%7 successCount=%8")
                    .arg(frame.index)
                    .arg(surfaceSize.width())
                    .arg(surfaceSize.height())
                    .arg(targetRect.x(), 0, 'f', 1)
                    .arg(targetRect.y(), 0, 'f', 1)
                    .arg(targetRect.width(), 0, 'f', 1)
                    .arg(targetRect.height(), 0, 'f', 1)
                    .arg(successfulPresentCount + 1),
                true);
        }

        ++successfulPresentCount;
        return true;
    }
#endif
};

D3D11RenderBackend::D3D11RenderBackend()
    : m_impl(std::make_unique<Impl>())
{
#ifdef Q_OS_WIN
    static_cast<void>(m_impl->initializeDevice());
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
    return frame.cpuImage;
}

bool D3D11RenderBackend::canPresentToNativeWindow() const
{
#ifdef Q_OS_WIN
    return isReady();
#else
    return false;
#endif
}

bool D3D11RenderBackend::presentToNativeWindow(
    QWidget* widget,
    const QSize& surfaceSize,
    const VideoFrame& frame,
    const QRectF& targetRect,
    const QImage& overlayImage)
{
#ifdef Q_OS_WIN
    if (!widget || frame.cpuImage.isNull())
    {
        return false;
    }

    return m_impl->present(widget, surfaceSize, frame, targetRect, overlayImage);
#else
    Q_UNUSED(widget);
    Q_UNUSED(surfaceSize);
    Q_UNUSED(frame);
    Q_UNUSED(targetRect);
    Q_UNUSED(overlayImage);
    return false;
#endif
}
