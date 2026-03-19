#pragma once

#include <memory>
#include <optional>

#include <QObject>
#include <QString>
#include <QUuid>

class AudioEngine : public QObject
{
    Q_OBJECT

public:
    struct StereoLevels
    {
        float left = 0.0F;
        float right = 0.0F;
    };

    struct TrackPlaybackOptions
    {
        int offsetMs = 0;
        int clipStartMs = 0;
        std::optional<int> clipEndMs;
        bool loopEnabled = false;
    };

    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    [[nodiscard]] static std::unique_ptr<AudioEngine> create(QObject* parent = nullptr);

    [[nodiscard]] virtual bool isReady() const = 0;
    [[nodiscard]] virtual QString initializationError() const = 0;
    virtual bool playTrack(const QUuid& trackId, const QString& filePath, int offsetMs = 0) = 0;
    virtual bool playTrack(const QUuid& trackId, const QString& filePath, const TrackPlaybackOptions& options)
    {
        return playTrack(trackId, filePath, options.offsetMs);
    }
    virtual void setTrackGain(const QUuid& trackId, float gainDb) = 0;
    virtual void setTrackPan(const QUuid& trackId, float pan) = 0;
    virtual void setMasterGain(float gainDb) = 0;
    virtual void stopTrack(const QUuid& trackId) = 0;
    virtual void stopAll() = 0;
    [[nodiscard]] virtual bool isTrackPlaying(const QUuid& trackId) const = 0;
    [[nodiscard]] virtual float trackLevel(const QUuid& trackId) const = 0;
    [[nodiscard]] virtual StereoLevels trackStereoLevels(const QUuid& trackId) const = 0;
    [[nodiscard]] virtual float masterLevel() const = 0;
    [[nodiscard]] virtual StereoLevels masterStereoLevels() const = 0;
    [[nodiscard]] virtual std::optional<int> channelCount(const QString& filePath) const = 0;
    [[nodiscard]] virtual std::optional<int> durationMs(const QString& filePath) const = 0;

signals:
    void statusChanged(const QString& message);
};
