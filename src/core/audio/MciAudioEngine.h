#pragma once

#include <QHash>
#include <QString>
#include <QUuid>

#include "core/audio/AudioEngine.h"

class MciAudioEngine final : public AudioEngine
{
    Q_OBJECT

public:
    explicit MciAudioEngine(QObject* parent = nullptr);

    bool playTrack(const QUuid& trackId, const QString& filePath, int offsetMs = 0) override;
    void setTrackGain(const QUuid& trackId, float gainDb) override;
    void setTrackPan(const QUuid& trackId, float pan) override;
    void setMasterGain(float gainDb) override;
    void stopTrack(const QUuid& trackId) override;
    void stopAll() override;
    [[nodiscard]] bool isTrackPlaying(const QUuid& trackId) const override;
    [[nodiscard]] float trackLevel(const QUuid& trackId) const override;
    [[nodiscard]] float masterLevel() const override;
    [[nodiscard]] std::optional<int> durationMs(const QString& filePath) const override;

private:
    struct ActiveTrackPlayback
    {
        QString alias;
        QString filePath;
        int offsetMs = 0;
    };

    [[nodiscard]] QString aliasForTrack(const QUuid& trackId) const;

    QHash<QUuid, ActiveTrackPlayback> m_activeTracks;
};
