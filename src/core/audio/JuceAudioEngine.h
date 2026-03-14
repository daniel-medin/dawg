#pragma once

#include <memory>
#include <optional>

#include <QString>
#include <QUuid>

#include "core/audio/AudioEngine.h"

class JuceAudioEngine final : public AudioEngine
{
    Q_OBJECT

public:
    explicit JuceAudioEngine(QObject* parent = nullptr);
    ~JuceAudioEngine() override;

    [[nodiscard]] bool isReady() const;

    bool playTrack(const QUuid& trackId, const QString& filePath, int offsetMs = 0) override;
    void setTrackPan(const QUuid& trackId, float pan) override;
    void stopTrack(const QUuid& trackId) override;
    void stopAll() override;
    [[nodiscard]] bool isTrackPlaying(const QUuid& trackId) const override;
    [[nodiscard]] std::optional<int> durationMs(const QString& filePath) const override;

private:
    struct Impl;

    std::unique_ptr<Impl> m_impl;
};
