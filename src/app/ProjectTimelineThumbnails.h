#pragma once

#include <optional>

#include <QString>
#include <QVector>

namespace dawg::timeline
{
struct ThumbnailLevel
{
    int index = 0;
    int frameStep = 1;
    QVector<int> frames;
};

struct ThumbnailManifest
{
    QString videoRelativePath;
    qint64 videoSize = 0;
    qint64 videoLastModifiedUtcMs = 0;
    int totalFrames = 0;
    double fps = 0.0;
    QVector<ThumbnailLevel> levels;
};

QString timelineThumbnailRootPath(const QString& projectRootPath);
QString timelineThumbnailManifestPath(const QString& projectRootPath);
QString timelineThumbnailFilePath(const QString& projectRootPath, int levelIndex, int frameIndex);
std::optional<ThumbnailManifest> loadTimelineThumbnailManifest(
    const QString& projectRootPath,
    QString* errorMessage = nullptr);
bool ensureProjectTimelineThumbnails(
    const QString& projectRootPath,
    const QString& absoluteVideoPath,
    int totalFrames,
    double fps,
    QString* errorMessage = nullptr);
}
