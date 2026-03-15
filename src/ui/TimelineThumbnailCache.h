#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

#include <QHash>
#include <QObject>
#include <QImage>
#include <QString>
#include <QVector>

class VideoDecoder;

class TimelineThumbnailCache final : public QObject
{
    Q_OBJECT

public:
    explicit TimelineThumbnailCache(QObject* parent = nullptr);
    ~TimelineThumbnailCache() override;

    void clear();
    void requestFrames(const QString& videoPath, const QVector<int>& frameIndices);
    [[nodiscard]] bool hasThumbnail(const QString& videoPath, int frameIndex) const;
    [[nodiscard]] QImage thumbnail(const QString& videoPath, int frameIndex) const;

signals:
    void thumbnailReady(const QString& videoPath, int frameIndex);

private:
    void workerLoop();
    bool ensureDecoderOpen(const QString& videoPath);

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::thread m_workerThread;
    std::unique_ptr<VideoDecoder> m_decoder;
    QString m_decoderPath;
    QString m_requestedPath;
    std::deque<int> m_pendingFrames;
    std::unordered_set<int> m_pendingFrameSet;
    QHash<QString, QImage> m_thumbnailImages;
    bool m_stop = false;
};

TimelineThumbnailCache& timelineThumbnailCache();
