#include "app/ProjectTimelineThumbnails.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "core/video/FfmpegVideoDecoder.h"
#include "core/video/OpenCvVideoDecoder.h"
#include "core/video/VideoDecoder.h"

namespace
{
constexpr int kThumbnailSchemaVersion = 1;
constexpr int kThumbnailWidth = 112;
constexpr int kThumbnailHeight = 64;

std::unique_ptr<VideoDecoder> createThumbnailDecoder()
{
#if DAWG_HAS_FFMPEG
    return std::make_unique<FfmpegVideoDecoder>();
#else
    return std::make_unique<OpenCvVideoDecoder>();
#endif
}

QVector<int> thumbnailLevelSampleCounts(const int totalFrames)
{
    QVector<int> sampleCounts;
    const QVector<int> preferredCounts{24, 48, 96};
    for (const int preferredCount : preferredCounts)
    {
        const int clampedCount = std::clamp(preferredCount, 1, std::max(1, totalFrames));
        if (!sampleCounts.contains(clampedCount))
        {
            sampleCounts.push_back(clampedCount);
        }
    }
    if (sampleCounts.isEmpty())
    {
        sampleCounts.push_back(1);
    }
    return sampleCounts;
}

QVector<int> buildFrameSet(const int totalFrames, const int sampleCount)
{
    QVector<int> frames;
    if (totalFrames <= 0)
    {
        return frames;
    }

    const int maxFrameIndex = std::max(0, totalFrames - 1);
    const int clampedCount = std::clamp(sampleCount, 1, totalFrames);
    frames.reserve(clampedCount);
    for (int index = 0; index < clampedCount; ++index)
    {
        const double ratio = clampedCount <= 1 ? 0.0 : static_cast<double>(index) / static_cast<double>(clampedCount - 1);
        const int frameIndex = std::clamp(
            static_cast<int>(std::lround(ratio * static_cast<double>(maxFrameIndex))),
            0,
            maxFrameIndex);
        if (frames.isEmpty() || frames.back() != frameIndex)
        {
            frames.push_back(frameIndex);
        }
    }
    if (frames.isEmpty())
    {
        frames.push_back(0);
    }
    return frames;
}

QString timelineThumbnailDirectoryPath(const QString& projectRootPath, const int levelIndex)
{
    return QDir(projectRootPath).filePath(QStringLiteral("thumbnails/timeline/level-%1").arg(levelIndex));
}

QJsonArray framesToJson(const QVector<int>& frames)
{
    QJsonArray array;
    for (const int frameIndex : frames)
    {
        array.append(frameIndex);
    }
    return array;
}

QVector<int> framesFromJson(const QJsonArray& array)
{
    QVector<int> frames;
    frames.reserve(array.size());
    for (const auto& value : array)
    {
        frames.push_back(value.toInt());
    }
    return frames;
}

QJsonObject levelToJson(const dawg::timeline::ThumbnailLevel& level)
{
    return QJsonObject{
        {QStringLiteral("index"), level.index},
        {QStringLiteral("frameStep"), level.frameStep},
        {QStringLiteral("frames"), framesToJson(level.frames)}
    };
}

dawg::timeline::ThumbnailLevel levelFromJson(const QJsonObject& object)
{
    dawg::timeline::ThumbnailLevel level;
    level.index = object.value(QStringLiteral("index")).toInt();
    level.frameStep = std::max(1, object.value(QStringLiteral("frameStep")).toInt(1));
    level.frames = framesFromJson(object.value(QStringLiteral("frames")).toArray());
    return level;
}

QJsonObject manifestToJson(const dawg::timeline::ThumbnailManifest& manifest)
{
    QJsonArray levels;
    for (const auto& level : manifest.levels)
    {
        levels.append(levelToJson(level));
    }

    return QJsonObject{
        {QStringLiteral("schemaVersion"), kThumbnailSchemaVersion},
        {QStringLiteral("videoRelativePath"), manifest.videoRelativePath},
        {QStringLiteral("videoSize"), static_cast<double>(manifest.videoSize)},
        {QStringLiteral("videoLastModifiedUtcMs"), static_cast<double>(manifest.videoLastModifiedUtcMs)},
        {QStringLiteral("totalFrames"), manifest.totalFrames},
        {QStringLiteral("fps"), manifest.fps},
        {QStringLiteral("levels"), levels}
    };
}

std::optional<dawg::timeline::ThumbnailManifest> manifestFromJson(const QJsonObject& object)
{
    if (object.value(QStringLiteral("schemaVersion")).toInt() != kThumbnailSchemaVersion)
    {
        return std::nullopt;
    }

    dawg::timeline::ThumbnailManifest manifest;
    manifest.videoRelativePath = object.value(QStringLiteral("videoRelativePath")).toString();
    manifest.videoSize = static_cast<qint64>(object.value(QStringLiteral("videoSize")).toDouble(-1));
    manifest.videoLastModifiedUtcMs = static_cast<qint64>(object.value(QStringLiteral("videoLastModifiedUtcMs")).toDouble(-1));
    manifest.totalFrames = object.value(QStringLiteral("totalFrames")).toInt();
    manifest.fps = object.value(QStringLiteral("fps")).toDouble();
    const auto levelArray = object.value(QStringLiteral("levels")).toArray();
    manifest.levels.reserve(levelArray.size());
    for (const auto& value : levelArray)
    {
        manifest.levels.push_back(levelFromJson(value.toObject()));
    }

    if (manifest.videoRelativePath.isEmpty() || manifest.totalFrames <= 0 || manifest.levels.isEmpty())
    {
        return std::nullopt;
    }

    return manifest;
}

bool manifestMatchesVideo(
    const QString& projectRootPath,
    const dawg::timeline::ThumbnailManifest& manifest,
    const QString& absoluteVideoPath,
    const int totalFrames)
{
    const QFileInfo videoInfo(absoluteVideoPath);
    const QDir root(projectRootPath);
    const QString relativeVideoPath = QDir::cleanPath(root.relativeFilePath(videoInfo.absoluteFilePath()));
    if (manifest.videoRelativePath != relativeVideoPath)
    {
        return false;
    }
    if (manifest.totalFrames != totalFrames)
    {
        return false;
    }
    if (manifest.videoSize != videoInfo.size())
    {
        return false;
    }
    return manifest.videoLastModifiedUtcMs == videoInfo.lastModified().toMSecsSinceEpoch();
}

bool manifestFilesExist(const QString& projectRootPath, const dawg::timeline::ThumbnailManifest& manifest)
{
    for (const auto& level : manifest.levels)
    {
        for (const int frameIndex : level.frames)
        {
            if (!QFileInfo::exists(dawg::timeline::timelineThumbnailFilePath(projectRootPath, level.index, frameIndex)))
            {
                return false;
            }
        }
    }
    return true;
}

bool openDecoderForVideo(std::unique_ptr<VideoDecoder>* decoder, const QString& absoluteVideoPath)
{
    if (!decoder)
    {
        return false;
    }

    auto nextDecoder = createThumbnailDecoder();
    nextDecoder->setCpuFrameExtractionEnabled(true);
    nextDecoder->setOutputScale(0.18);
    if (!nextDecoder->open(absoluteVideoPath.toStdString()))
    {
        nextDecoder = std::make_unique<OpenCvVideoDecoder>();
        nextDecoder->setCpuFrameExtractionEnabled(true);
        nextDecoder->setOutputScale(0.18);
        if (!nextDecoder->open(absoluteVideoPath.toStdString()))
        {
            return false;
        }
    }

    *decoder = std::move(nextDecoder);
    return true;
}

std::optional<QImage> decodeThumbnailImage(VideoDecoder& decoder, const int frameIndex)
{
    if (!decoder.seekFrame(frameIndex))
    {
        return std::nullopt;
    }

    while (true)
    {
        auto frame = decoder.readFrame();
        if (!frame.has_value() || !frame->isValid())
        {
            return std::nullopt;
        }
        if (frame->index < frameIndex)
        {
            continue;
        }
        if (!frame->hasCpuImage())
        {
            return std::nullopt;
        }

        return frame->cpuImage.scaled(
            QSize{kThumbnailWidth, kThumbnailHeight},
            Qt::KeepAspectRatioByExpanding,
            Qt::SmoothTransformation);
    }
}
}

namespace dawg::timeline
{
QString timelineThumbnailRootPath(const QString& projectRootPath)
{
    return QDir(projectRootPath).filePath(QStringLiteral("thumbnails/timeline"));
}

QString timelineThumbnailManifestPath(const QString& projectRootPath)
{
    return QDir(timelineThumbnailRootPath(projectRootPath)).filePath(QStringLiteral("manifest.json"));
}

QString timelineThumbnailFilePath(const QString& projectRootPath, const int levelIndex, const int frameIndex)
{
    return QDir(timelineThumbnailDirectoryPath(projectRootPath, levelIndex))
        .filePath(QStringLiteral("frame-%1.png").arg(frameIndex, 8, 10, QLatin1Char('0')));
}

std::optional<ThumbnailManifest> loadTimelineThumbnailManifest(const QString& projectRootPath, QString* errorMessage)
{
    QFile file(timelineThumbnailManifestPath(projectRootPath));
    if (!file.exists())
    {
        return std::nullopt;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to open thumbnail manifest.");
        }
        return std::nullopt;
    }

    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Thumbnail manifest is invalid.");
        }
        return std::nullopt;
    }

    auto manifest = manifestFromJson(document.object());
    if (!manifest.has_value())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Thumbnail manifest schema is unsupported.");
        }
        return std::nullopt;
    }

    return manifest;
}

bool ensureProjectTimelineThumbnails(
    const QString& projectRootPath,
    const QString& absoluteVideoPath,
    const int totalFrames,
    const double fps,
    QString* errorMessage)
{
    if (projectRootPath.isEmpty())
    {
        return false;
    }

    QDir projectRoot(projectRootPath);
    if (!projectRoot.exists() && !QDir().mkpath(projectRootPath))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Project folder is missing: %1").arg(projectRootPath);
        }
        return false;
    }

    if (!projectRoot.mkpath(QStringLiteral("thumbnails/timeline")))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to create project thumbnail folder.");
        }
        return false;
    }

    if (absoluteVideoPath.isEmpty() || totalFrames <= 0)
    {
        return true;
    }

    const QFileInfo videoInfo(absoluteVideoPath);
    if (!videoInfo.exists() || !videoInfo.isFile())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Project video is missing: %1").arg(absoluteVideoPath);
        }
        return false;
    }

    const auto existingManifest = loadTimelineThumbnailManifest(projectRootPath);
    if (existingManifest.has_value()
        && manifestMatchesVideo(projectRootPath, *existingManifest, absoluteVideoPath, totalFrames)
        && manifestFilesExist(projectRootPath, *existingManifest))
    {
        return true;
    }

    QDir thumbnailRoot(timelineThumbnailRootPath(projectRootPath));
    if (thumbnailRoot.exists())
    {
        thumbnailRoot.removeRecursively();
    }
    if (!projectRoot.mkpath(QStringLiteral("thumbnails/timeline")))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to reset project thumbnail folder.");
        }
        return false;
    }

    std::unique_ptr<VideoDecoder> decoder;
    if (!openDecoderForVideo(&decoder, absoluteVideoPath))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to open the project video for timeline thumbnail generation.");
        }
        return false;
    }

    ThumbnailManifest manifest;
    manifest.videoRelativePath = QDir::cleanPath(projectRoot.relativeFilePath(videoInfo.absoluteFilePath()));
    manifest.videoSize = videoInfo.size();
    manifest.videoLastModifiedUtcMs = videoInfo.lastModified().toMSecsSinceEpoch();
    manifest.totalFrames = totalFrames;
    manifest.fps = fps;

    const auto sampleCounts = thumbnailLevelSampleCounts(totalFrames);
    manifest.levels.reserve(sampleCounts.size());
    for (int levelIndex = 0; levelIndex < sampleCounts.size(); ++levelIndex)
    {
        ThumbnailLevel level;
        level.index = levelIndex;
        level.frames = buildFrameSet(totalFrames, sampleCounts.at(levelIndex));
        level.frameStep = std::max(1, level.frames.size() <= 1
            ? totalFrames
            : static_cast<int>(std::lround(static_cast<double>(std::max(1, totalFrames - 1))
                / static_cast<double>(level.frames.size() - 1))));

        if (!projectRoot.mkpath(QStringLiteral("thumbnails/timeline/level-%1").arg(level.index)))
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("Failed to create a project thumbnail level folder.");
            }
            return false;
        }

        for (const int frameIndex : level.frames)
        {
            const auto image = decodeThumbnailImage(*decoder, frameIndex);
            if (!image.has_value())
            {
                if (errorMessage)
                {
                    *errorMessage = QStringLiteral("Failed to render timeline thumbnail frame %1.").arg(frameIndex);
                }
                return false;
            }

            const auto outputPath = timelineThumbnailFilePath(projectRootPath, level.index, frameIndex);
            if (!QDir().mkpath(QFileInfo(outputPath).absolutePath()))
            {
                if (errorMessage)
                {
                    *errorMessage = QStringLiteral("Failed to prepare a timeline thumbnail folder.");
                }
                return false;
            }

            if (!image->save(outputPath, "PNG"))
            {
                if (errorMessage)
                {
                    *errorMessage = QStringLiteral("Failed to save a timeline thumbnail image.");
                }
                return false;
            }
        }

        manifest.levels.push_back(level);
    }

    QSaveFile manifestFile(timelineThumbnailManifestPath(projectRootPath));
    if (!manifestFile.open(QIODevice::WriteOnly))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to write the thumbnail manifest.");
        }
        return false;
    }

    manifestFile.write(QJsonDocument(manifestToJson(manifest)).toJson(QJsonDocument::Indented));
    if (!manifestFile.commit())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to finalize the thumbnail manifest.");
        }
        return false;
    }

    return true;
}
}
