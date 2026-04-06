#include "app/NodeDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace
{
using dawg::node::Document;
using dawg::node::NodeData;
using dawg::node::TrackData;

QJsonObject audioAttachmentToJson(
    const AudioAttachment& attachment,
    const QString& embeddedAudioFileName,
    const QByteArray& embeddedAudioData)
{
    QJsonObject object{
        {QStringLiteral("assetPath"), attachment.assetPath},
        {QStringLiteral("gainDb"), attachment.gainDb},
        {QStringLiteral("clipStartMs"), attachment.clipStartMs},
        {QStringLiteral("loopEnabled"), attachment.loopEnabled}
    };
    if (attachment.clipEndMs.has_value())
    {
        object.insert(QStringLiteral("clipEndMs"), *attachment.clipEndMs);
    }
    if (!embeddedAudioFileName.isEmpty())
    {
        object.insert(QStringLiteral("embeddedAudioFileName"), embeddedAudioFileName);
    }
    if (!embeddedAudioData.isEmpty())
    {
        object.insert(QStringLiteral("embeddedAudioData"), QString::fromLatin1(embeddedAudioData.toBase64()));
    }
    return object;
}

AudioAttachment audioAttachmentFromJson(const QJsonObject& object)
{
    AudioAttachment attachment;
    attachment.assetPath = object.value(QStringLiteral("assetPath")).toString();
    attachment.gainDb = static_cast<float>(object.value(QStringLiteral("gainDb")).toDouble());
    attachment.clipStartMs = object.value(QStringLiteral("clipStartMs")).toInt();
    if (object.contains(QStringLiteral("clipEndMs")) && !object.value(QStringLiteral("clipEndMs")).isNull())
    {
        attachment.clipEndMs = object.value(QStringLiteral("clipEndMs")).toInt();
    }
    attachment.loopEnabled = object.value(QStringLiteral("loopEnabled")).toBool(false);
    return attachment;
}

QJsonObject trackToJson(const TrackData& track)
{
    QJsonObject object{
        {QStringLiteral("label"), track.label}
    };
    if (track.attachedAudio.has_value())
    {
        object.insert(
            QStringLiteral("attachedAudio"),
            audioAttachmentToJson(*track.attachedAudio, track.embeddedAudioFileName, track.embeddedAudioData));
    }
    return object;
}

TrackData trackFromJson(const QJsonObject& object)
{
    TrackData track;
    track.label = object.value(QStringLiteral("label")).toString();
    if (object.contains(QStringLiteral("attachedAudio")) && object.value(QStringLiteral("attachedAudio")).isObject())
    {
        const auto audioObject = object.value(QStringLiteral("attachedAudio")).toObject();
        track.attachedAudio = audioAttachmentFromJson(audioObject);
        track.embeddedAudioFileName = audioObject.value(QStringLiteral("embeddedAudioFileName")).toString();
        track.embeddedAudioData = QByteArray::fromBase64(
            audioObject.value(QStringLiteral("embeddedAudioData")).toString().toLatin1());
    }
    return track;
}

std::optional<Document> loadLegacyDocumentV1(const QJsonObject& root)
{
    const auto nodeObject = root.value(QStringLiteral("node")).toObject();
    NodeData node;
    node.label = nodeObject.value(QStringLiteral("label")).toString();
    node.autoPanEnabled = nodeObject.value(QStringLiteral("autoPanEnabled")).toBool(true);
    node.timelineFrameCount = nodeObject.value(QStringLiteral("timelineFrameCount")).toInt(0);
    node.timelineFps = nodeObject.value(QStringLiteral("timelineFps")).toDouble(0.0);

    TrackData primaryTrack;
    primaryTrack.label = node.label;
    if (nodeObject.contains(QStringLiteral("attachedAudio")) && nodeObject.value(QStringLiteral("attachedAudio")).isObject())
    {
        const auto audioObject = nodeObject.value(QStringLiteral("attachedAudio")).toObject();
        primaryTrack.attachedAudio = audioAttachmentFromJson(audioObject);
        primaryTrack.embeddedAudioFileName = audioObject.value(QStringLiteral("embeddedAudioFileName")).toString();
        primaryTrack.embeddedAudioData = QByteArray::fromBase64(
            audioObject.value(QStringLiteral("embeddedAudioData")).toString().toLatin1());
    }
    if (primaryTrack.attachedAudio.has_value() || !primaryTrack.label.isEmpty())
    {
        node.tracks.push_back(primaryTrack);
    }

    return Document{
        .name = root.value(QStringLiteral("name")).toString(),
        .node = node
    };
}
}

namespace dawg::node
{
QString sanitizeNodeName(const QString& name)
{
    auto sanitized = name.trimmed();
    sanitized.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    sanitized.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    return sanitized;
}

QString nodeFileNameForName(const QString& nodeName)
{
    const auto sanitizedName = sanitizeNodeName(nodeName);
    return sanitizedName.isEmpty()
        ? QStringLiteral("Node") + QString::fromLatin1(kNodeFileSuffix)
        : sanitizedName + QString::fromLatin1(kNodeFileSuffix);
}

std::optional<Document> loadDocument(const QString& nodeFilePath, QString* errorMessage)
{
    QFile file(nodeFilePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to open node file:\n%1").arg(nodeFilePath);
        }
        return std::nullopt;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull() || !document.isObject())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to parse node file:\n%1").arg(parseError.errorString());
        }
        return std::nullopt;
    }

    const auto root = document.object();
    const auto schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (schemaVersion == 1)
    {
        return loadLegacyDocumentV1(root);
    }
    if (schemaVersion != kSchemaVersion)
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Unsupported node file version.");
        }
        return std::nullopt;
    }

    const auto nodeObject = root.value(QStringLiteral("node")).toObject();
    NodeData node;
    node.label = nodeObject.value(QStringLiteral("label")).toString();
    node.autoPanEnabled = nodeObject.value(QStringLiteral("autoPanEnabled")).toBool(true);
    node.timelineFrameCount = nodeObject.value(QStringLiteral("timelineFrameCount")).toInt(0);
    node.timelineFps = nodeObject.value(QStringLiteral("timelineFps")).toDouble(0.0);
    const auto tracksArray = nodeObject.value(QStringLiteral("tracks")).toArray();
    node.tracks.reserve(static_cast<std::size_t>(tracksArray.size()));
    for (const auto& trackValue : tracksArray)
    {
        if (!trackValue.isObject())
        {
            continue;
        }
        node.tracks.push_back(trackFromJson(trackValue.toObject()));
    }

    return Document{
        .name = root.value(QStringLiteral("name")).toString(),
        .node = node
    };
}

bool saveDocument(const QString& nodeFilePath, const Document& document, QString* errorMessage)
{
    const QFileInfo fileInfo(nodeFilePath);
    QDir directory(fileInfo.absolutePath());
    if (!directory.exists() && !QDir().mkpath(directory.absolutePath()))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to create node folder:\n%1").arg(directory.absolutePath());
        }
        return false;
    }

    QJsonArray tracksArray;
    for (const auto& track : document.node.tracks)
    {
        TrackData savedTrack = track;
        if (savedTrack.attachedAudio.has_value() && savedTrack.embeddedAudioData.isEmpty())
        {
            const auto assetPath = savedTrack.attachedAudio->assetPath;
            if (!assetPath.isEmpty())
            {
                QFile audioFile(assetPath);
                if (!audioFile.open(QIODevice::ReadOnly))
                {
                    if (errorMessage)
                    {
                        *errorMessage = QStringLiteral("Failed to read node audio:\n%1").arg(assetPath);
                    }
                    return false;
                }
                savedTrack.embeddedAudioData = audioFile.readAll();
                savedTrack.embeddedAudioFileName = QFileInfo(assetPath).fileName();
            }
        }
        tracksArray.append(trackToJson(savedTrack));
    }

    QJsonObject nodeObject{
        {QStringLiteral("label"), document.node.label},
        {QStringLiteral("autoPanEnabled"), document.node.autoPanEnabled},
        {QStringLiteral("timelineFrameCount"), document.node.timelineFrameCount},
        {QStringLiteral("timelineFps"), document.node.timelineFps},
        {QStringLiteral("tracks"), tracksArray}
    };

    const QJsonObject root{
        {QStringLiteral("schemaVersion"), kSchemaVersion},
        {QStringLiteral("name"), document.name},
        {QStringLiteral("node"), nodeObject}
    };

    QSaveFile file(nodeFilePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to write node file:\n%1").arg(nodeFilePath);
        }
        return false;
    }

    const auto json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size() || !file.commit())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to save node file:\n%1").arg(nodeFilePath);
        }
        return false;
    }

    return true;
}
}
