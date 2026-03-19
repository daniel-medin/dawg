#include "ui/VideoViewportQuickItem.h"

#include <algorithm>
#include <cmath>

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <QtQuick/qsgtexture_platform.h>
#endif

#include "ui/VideoViewportQuickController.h"

namespace
{
#ifdef Q_OS_WIN
template <typename T>
void releaseTyped(T*& object)
{
    if (object)
    {
        object->Release();
        object = nullptr;
    }
}

void enableD3D11MultithreadProtection(ID3D11DeviceContext* deviceContext)
{
    if (!deviceContext)
    {
        return;
    }

    ID3D11Multithread* multithread = nullptr;
    if (SUCCEEDED(deviceContext->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void**>(&multithread)))
        && multithread)
    {
        multithread->SetMultithreadProtected(TRUE);
        multithread->Release();
    }
}

struct VideoTextureNode final : QSGSimpleTextureNode
{
    std::shared_ptr<void> nativeResource;
    int renderedRevision = -1;
    QSize outputSize;
    QSize inputSize;
    DXGI_FORMAT inputFormat = DXGI_FORMAT_UNKNOWN;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* deviceContext = nullptr;
    ID3D11VideoDevice* videoDevice = nullptr;
    ID3D11VideoContext* videoContext = nullptr;
    ID3D11VideoProcessorEnumerator* videoProcessorEnumerator = nullptr;
    ID3D11VideoProcessor* videoProcessor = nullptr;
    ID3D11VideoProcessorOutputView* videoProcessorOutputView = nullptr;
    ID3D11Texture2D* convertedTexture = nullptr;
    ID3D11Texture2D* sampleTexture = nullptr;

    VideoTextureNode()
    {
        setOwnsTexture(true);
    }

    ~VideoTextureNode() override
    {
        releaseProcessorResources();
        releaseTyped(videoContext);
        releaseTyped(videoDevice);
        releaseTyped(deviceContext);
        releaseTyped(device);
    }

    void releaseProcessorResources()
    {
        releaseTyped(videoProcessorOutputView);
        releaseTyped(videoProcessor);
        releaseTyped(videoProcessorEnumerator);
        releaseTyped(sampleTexture);
        releaseTyped(convertedTexture);
        outputSize = {};
        inputSize = {};
        inputFormat = DXGI_FORMAT_UNKNOWN;
        renderedRevision = -1;
    }

    bool ensureVideoInterfaces(ID3D11Device* nextDevice)
    {
        if (!nextDevice)
        {
            return false;
        }

        if (device != nextDevice || !deviceContext || !videoDevice || !videoContext)
        {
            releaseProcessorResources();
            releaseTyped(videoContext);
            releaseTyped(videoDevice);
            releaseTyped(deviceContext);
            releaseTyped(device);

            device = nextDevice;
            device->AddRef();
            device->GetImmediateContext(&deviceContext);
            if (!deviceContext)
            {
                releaseTyped(device);
                return false;
            }
            enableD3D11MultithreadProtection(deviceContext);

            if (FAILED(device->QueryInterface(__uuidof(ID3D11VideoDevice), reinterpret_cast<void**>(&videoDevice)))
                || FAILED(deviceContext->QueryInterface(__uuidof(ID3D11VideoContext), reinterpret_cast<void**>(&videoContext)))
                || !videoDevice
                || !videoContext)
            {
                releaseTyped(videoContext);
                releaseTyped(videoDevice);
                releaseTyped(deviceContext);
                releaseTyped(device);
                return false;
            }
        }

        return true;
    }

    bool ensureProcessor(QQuickWindow* window, const QSize& nextInputSize, const DXGI_FORMAT nextInputFormat, const QSize& nextOutputSize)
    {
        if (!window || !device || !videoDevice || !videoContext || !nextInputSize.isValid() || !nextOutputSize.isValid())
        {
            return false;
        }

        const auto needsRebuild =
            !videoProcessor
            || !videoProcessorEnumerator
            || !videoProcessorOutputView
            || !convertedTexture
            || inputSize != nextInputSize
            || outputSize != nextOutputSize
            || inputFormat != nextInputFormat;
        if (!needsRebuild)
        {
            return true;
        }

        releaseProcessorResources();

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDescription{};
        contentDescription.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        contentDescription.InputWidth = static_cast<UINT>(std::max(1, nextInputSize.width()));
        contentDescription.InputHeight = static_cast<UINT>(std::max(1, nextInputSize.height()));
        contentDescription.OutputWidth = static_cast<UINT>(std::max(1, nextOutputSize.width()));
        contentDescription.OutputHeight = static_cast<UINT>(std::max(1, nextOutputSize.height()));
        contentDescription.InputFrameRate.Numerator = 60;
        contentDescription.InputFrameRate.Denominator = 1;
        contentDescription.OutputFrameRate.Numerator = 60;
        contentDescription.OutputFrameRate.Denominator = 1;
        contentDescription.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

        if (FAILED(videoDevice->CreateVideoProcessorEnumerator(&contentDescription, &videoProcessorEnumerator))
            || !videoProcessorEnumerator)
        {
            releaseProcessorResources();
            return false;
        }

        if (FAILED(videoDevice->CreateVideoProcessor(videoProcessorEnumerator, 0, &videoProcessor)) || !videoProcessor)
        {
            releaseProcessorResources();
            return false;
        }

        D3D11_TEXTURE2D_DESC outputTextureDescription{};
        outputTextureDescription.Width = static_cast<UINT>(std::max(1, nextOutputSize.width()));
        outputTextureDescription.Height = static_cast<UINT>(std::max(1, nextOutputSize.height()));
        outputTextureDescription.MipLevels = 1;
        outputTextureDescription.ArraySize = 1;
        outputTextureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        outputTextureDescription.SampleDesc.Count = 1;
        outputTextureDescription.Usage = D3D11_USAGE_DEFAULT;
        outputTextureDescription.BindFlags = D3D11_BIND_RENDER_TARGET;

        if (FAILED(device->CreateTexture2D(&outputTextureDescription, nullptr, &convertedTexture)) || !convertedTexture)
        {
            releaseProcessorResources();
            return false;
        }

        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDescription{};
        outputViewDescription.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        outputViewDescription.Texture2D.MipSlice = 0;
        if (FAILED(videoDevice->CreateVideoProcessorOutputView(
                convertedTexture,
                videoProcessorEnumerator,
                &outputViewDescription,
                &videoProcessorOutputView))
            || !videoProcessorOutputView)
        {
            releaseProcessorResources();
            return false;
        }

        D3D11_TEXTURE2D_DESC sampleTextureDescription = outputTextureDescription;
        sampleTextureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&sampleTextureDescription, nullptr, &sampleTexture)) || !sampleTexture)
        {
            releaseProcessorResources();
            return false;
        }

        auto* wrappedTexture = QNativeInterface::QSGD3D11Texture::fromNative(
            sampleTexture,
            window,
            nextOutputSize,
            QQuickWindow::TextureHasAlphaChannel);
        if (!wrappedTexture)
        {
            releaseProcessorResources();
            return false;
        }

        setTexture(wrappedTexture);
        setFiltering(QSGTexture::Linear);
        inputSize = nextInputSize;
        outputSize = nextOutputSize;
        inputFormat = nextInputFormat;
        return true;
    }

    bool convertFrame(QQuickWindow* window, const VideoFrame& frame, const QSize& nextOutputSize)
    {
        if (!window || !frame.hasNativeTexture() || frame.rotationDegrees != 0)
        {
            return false;
        }

        auto* windowDevice = static_cast<ID3D11Device*>(
            window->rendererInterface()->getResource(window, QSGRendererInterface::DeviceResource));
        auto* inputTexture = reinterpret_cast<ID3D11Texture2D*>(frame.nativeHandle);
        if (!windowDevice || !inputTexture || !ensureVideoInterfaces(windowDevice))
        {
            return false;
        }

        D3D11_TEXTURE2D_DESC inputDescription{};
        inputTexture->GetDesc(&inputDescription);
        const QSize nextInputSize{
            static_cast<int>(inputDescription.Width),
            static_cast<int>(inputDescription.Height)};
        if (!ensureProcessor(window, nextInputSize, inputDescription.Format, nextOutputSize))
        {
            return false;
        }

        ID3D11VideoProcessorInputView* inputView = nullptr;
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDescription{};
        inputViewDescription.FourCC = 0;
        inputViewDescription.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        inputViewDescription.Texture2D.MipSlice = 0;
        inputViewDescription.Texture2D.ArraySlice = frame.nativeSubresourceIndex;
        if (FAILED(videoDevice->CreateVideoProcessorInputView(
                inputTexture,
                videoProcessorEnumerator,
                &inputViewDescription,
                &inputView))
            || !inputView)
        {
            releaseTyped(inputView);
            return false;
        }

        RECT sourceRect{
            0,
            0,
            static_cast<LONG>(inputDescription.Width),
            static_cast<LONG>(inputDescription.Height)};
        RECT destinationRect{
            0,
            0,
            static_cast<LONG>(nextOutputSize.width()),
            static_cast<LONG>(nextOutputSize.height())};
        RECT outputRect = destinationRect;

        videoContext->VideoProcessorSetOutputTargetRect(videoProcessor, TRUE, &outputRect);
        videoContext->VideoProcessorSetStreamSourceRect(videoProcessor, 0, TRUE, &sourceRect);
        videoContext->VideoProcessorSetStreamDestRect(videoProcessor, 0, TRUE, &destinationRect);
        videoContext->VideoProcessorSetStreamFrameFormat(videoProcessor, 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);

        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = inputView;
        const auto result = videoContext->VideoProcessorBlt(videoProcessor, videoProcessorOutputView, 0, 1, &stream);
        releaseTyped(inputView);
        if (FAILED(result))
        {
            return false;
        }

        deviceContext->CopyResource(sampleTexture, convertedTexture);
        return true;
    }
};
#endif
}

VideoViewportQuickItem::VideoViewportQuickItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

VideoViewportQuickItem::~VideoViewportQuickItem()
{
    disconnectController();
}

QObject* VideoViewportQuickItem::controller() const
{
    return m_controller;
}

void VideoViewportQuickItem::setController(QObject* controllerObject)
{
    auto* nextController = qobject_cast<VideoViewportQuickController*>(controllerObject);
    if (m_controller == nextController)
    {
        return;
    }

    disconnectController();
    m_controller = nextController;
    syncSnapshot();
    if (m_controller)
    {
        m_controllerConnections.push_back(
            connect(m_controller, &VideoViewportQuickController::frameSourceChanged, this, &VideoViewportQuickItem::handleFrameChanged));
        m_controllerConnections.push_back(
            connect(m_controller, &VideoViewportQuickController::hasFrameChanged, this, &VideoViewportQuickItem::handleFrameChanged));
        m_controllerConnections.push_back(
            connect(
                m_controller,
                &VideoViewportQuickController::nativePresentationActiveChanged,
                this,
                &VideoViewportQuickItem::handleFrameChanged));
    }

    update();
    emit controllerChanged();
}

QSGNode* VideoViewportQuickItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updateData)
{
    Q_UNUSED(updateData);

#ifndef Q_OS_WIN
    return nullptr;
#else
    if (!window()
        || !m_snapshot.nativePresentationActive
        || !m_snapshot.videoFrame.hasNativeTexture()
        || width() <= 0.0
        || height() <= 0.0
        || window()->rendererInterface()->graphicsApi() != QSGRendererInterface::Direct3D11)
    {
        return nullptr;
    }

    auto* node = static_cast<VideoTextureNode*>(oldNode);
    if (!node)
    {
        node = new VideoTextureNode();
    }

    const QSize outputSize{
        std::max(1, static_cast<int>(std::lround(width()))),
        std::max(1, static_cast<int>(std::lround(height())))};
    if (!node->convertFrame(window(), m_snapshot.videoFrame, outputSize) || !node->texture())
    {
        if (node != oldNode)
        {
            delete node;
        }
        return nullptr;
    }

    node->nativeResource = m_snapshot.videoFrame.nativeResource;
    node->renderedRevision = m_snapshot.revision;
    node->setRect(boundingRect());
    return node;
#endif
}

void VideoViewportQuickItem::releaseResources()
{
    update();
}

void VideoViewportQuickItem::syncSnapshot()
{
    if (!m_controller)
    {
        m_snapshot = {};
        return;
    }

    m_snapshot.videoFrame = m_controller->currentVideoFrame();
    m_snapshot.nativePresentationActive = m_controller->nativePresentationActive();
    m_snapshot.revision = m_controller->frameRevision();
}

void VideoViewportQuickItem::handleFrameChanged()
{
    syncSnapshot();
    update();
}

void VideoViewportQuickItem::disconnectController()
{
    for (const auto& connection : m_controllerConnections)
    {
        disconnect(connection);
    }
    m_controllerConnections.clear();
    m_controller = nullptr;
    m_snapshot = {};
}
