#pragma once

#include <memory>

#include "core/video/VideoDecoder.h"

class FfmpegVideoDecoder final : public VideoDecoder
{
public:
    FfmpegVideoDecoder();
    ~FfmpegVideoDecoder() override;

    bool open(const std::string& filePath) override;
    bool isOpen() const override;
    bool seekFrame(int frameIndex) override;
    bool seekTimestampSeconds(double timestampSeconds) override;
    std::optional<VideoFrame> readFrame() override;
    int frameCount() const override;
    double fps() const override;
    cv::Size frameSize() const override;
    void setCpuFrameExtractionEnabled(bool enabled) override;
    [[nodiscard]] bool cpuFrameExtractionEnabled() const override;
    void setOutputScale(double scale) override;
    [[nodiscard]] double outputScale() const override;
    void setPreferredD3D11Device(void* device) override;
    [[nodiscard]] QString backendName() const override;
    [[nodiscard]] bool isHardwareAccelerated() const override;

private:
    struct Impl;

    std::unique_ptr<Impl> m_impl;
};
