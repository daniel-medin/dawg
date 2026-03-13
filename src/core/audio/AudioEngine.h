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
    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    [[nodiscard]] static std::unique_ptr<AudioEngine> create(QObject* parent = nullptr);

    virtual bool playTrack(const QUuid& trackId, const QString& filePath, int offsetMs = 0) = 0;
    virtual void stopTrack(const QUuid& trackId) = 0;
    virtual void stopAll() = 0;
    [[nodiscard]] virtual bool isTrackPlaying(const QUuid& trackId) const = 0;
    [[nodiscard]] virtual std::optional<int> durationMs(const QString& filePath) const = 0;

signals:
    void statusChanged(const QString& message);
};
