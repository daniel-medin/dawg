#include "app/NodeDocument.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

namespace
{
using dawg::node::Document;
using dawg::node::AudioClipData;
using dawg::node::LaneData;
using dawg::node::NodeData;

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
    attachment.loopEnabled = false;
    return attachment;
}

QJsonObject audioClipToJson(const AudioClipData& clip)
{
    QJsonObject object{
        {QStringLiteral("id"), clip.id},
        {QStringLiteral("label"), clip.label},
        {QStringLiteral("laneOffsetMs"), clip.laneOffsetMs}
    };
    if (clip.attachedAudio.has_value())
    {
        object.insert(
            QStringLiteral("attachedAudio"),
            audioAttachmentToJson(*clip.attachedAudio, clip.embeddedAudioFileName, clip.embeddedAudioData));
    }
    return object;
}

AudioClipData audioClipFromJson(const QJsonObject& object)
{
    AudioClipData clip;
    clip.id = object.value(QStringLiteral("id")).toString();
    if (clip.id.isEmpty())
    {
        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    clip.label = object.value(QStringLiteral("label")).toString();
    clip.laneOffsetMs = std::max(0, object.value(QStringLiteral("laneOffsetMs")).toInt(0));
    if (object.contains(QStringLiteral("attachedAudio")) && object.value(QStringLiteral("attachedAudio")).isObject())
    {
        const auto audioObject = object.value(QStringLiteral("attachedAudio")).toObject();
        clip.attachedAudio = audioAttachmentFromJson(audioObject);
        clip.embeddedAudioFileName = audioObject.value(QStringLiteral("embeddedAudioFileName")).toString();
        clip.embeddedAudioData = QByteArray::fromBase64(
            audioObject.value(QStringLiteral("embeddedAudioData")).toString().toLatin1());
    }
    return clip;
}

QJsonObject laneToJson(const LaneData& lane)
{
    QJsonArray clipsArray;
    for (const auto& clip : lane.audioClips)
    {
        clipsArray.append(audioClipToJson(clip));
    }

    return QJsonObject{
        {QStringLiteral("id"), lane.id},
        {QStringLiteral("label"), lane.label},
        {QStringLiteral("muted"), lane.muted},
        {QStringLiteral("soloed"), lane.soloed},
        {QStringLiteral("audioClips"), clipsArray}
    };
}

LaneData laneFromJson(const QJsonObject& object)
{
    LaneData lane;
    lane.id = object.value(QStringLiteral("id")).toString();
    if (lane.id.isEmpty())
    {
        lane.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    lane.label = object.value(QStringLiteral("label")).toString();
    lane.muted = object.value(QStringLiteral("muted")).toBool(false);
    lane.soloed = object.value(QStringLiteral("soloed")).toBool(false);
    const auto clipsArray = object.value(QStringLiteral("audioClips")).toArray();
    lane.audioClips.reserve(static_cast<std::size_t>(clipsArray.size()));
    for (const auto& clipValue : clipsArray)
    {
        if (!clipValue.isObject())
        {
            continue;
        }
        lane.audioClips.push_back(audioClipFromJson(clipValue.toObject()));
    }
    return lane;
}

AudioClipData legacyTrackClipFromJson(const QJsonObject& object)
{
    AudioClipData clip;
    clip.label = object.value(QStringLiteral("label")).toString();
    if (object.contains(QStringLiteral("attachedAudio")) && object.value(QStringLiteral("attachedAudio")).isObject())
    {
        const auto audioObject = object.value(QStringLiteral("attachedAudio")).toObject();
        clip.attachedAudio = audioAttachmentFromJson(audioObject);
        clip.embeddedAudioFileName = audioObject.value(QStringLiteral("embeddedAudioFileName")).toString();
        clip.embeddedAudioData = QByteArray::fromBase64(
            audioObject.value(QStringLiteral("embeddedAudioData")).toString().toLatin1());
    }
    return clip;
}

std::optional<Document> loadLegacyDocumentV1(const QJsonObject& root)
{
    const auto nodeObject = root.value(QStringLiteral("node")).toObject();
    NodeData node;
    node.label = nodeObject.value(QStringLiteral("label")).toString();
    node.autoPanEnabled = nodeObject.value(QStringLiteral("autoPanEnabled")).toBool(true);
    node.timelineFrameCount = nodeObject.value(QStringLiteral("timelineFrameCount")).toInt(0);
    node.timelineFps = nodeObject.value(QStringLiteral("timelineFps")).toDouble(0.0);

    LaneData primaryLane;
    primaryLane.label = node.label;
    AudioClipData primaryClip;
    primaryClip.label = node.label;
    if (nodeObject.contains(QStringLiteral("attachedAudio")) && nodeObject.value(QStringLiteral("attachedAudio")).isObject())
    {
        const auto audioObject = nodeObject.value(QStringLiteral("attachedAudio")).toObject();
        primaryClip.attachedAudio = audioAttachmentFromJson(audioObject);
        primaryClip.embeddedAudioFileName = audioObject.value(QStringLiteral("embeddedAudioFileName")).toString();
        primaryClip.embeddedAudioData = QByteArray::fromBase64(
            audioObject.value(QStringLiteral("embeddedAudioData")).toString().toLatin1());
    }
    if (primaryClip.attachedAudio.has_value())
    {
        primaryLane.audioClips.push_back(primaryClip);
    }
    if (!primaryLane.label.isEmpty() || !primaryLane.audioClips.empty())
    {
        node.lanes.push_back(primaryLane);
    }

    return Document{
        .name = root.value(QStringLiteral("name")).toString(),
        .node = node
    };
}

std::optional<Document> loadLegacyDocumentV2(const QJsonObject& root)
{
    const auto nodeObject = root.value(QStringLiteral("node")).toObject();
    NodeData node;
    node.label = nodeObject.value(QStringLiteral("label")).toString();
    node.autoPanEnabled = nodeObject.value(QStringLiteral("autoPanEnabled")).toBool(true);
    node.timelineFrameCount = nodeObject.value(QStringLiteral("timelineFrameCount")).toInt(0);
    node.timelineFps = nodeObject.value(QStringLiteral("timelineFps")).toDouble(0.0);

    const auto tracksArray = nodeObject.value(QStringLiteral("tracks")).toArray();
    node.lanes.reserve(static_cast<std::size_t>(tracksArray.size()));
    for (int index = 0; index < tracksArray.size(); ++index)
    {
        const auto trackValue = tracksArray.at(index);
        if (!trackValue.isObject())
        {
            continue;
        }

        const auto trackObject = trackValue.toObject();
        auto clip = legacyTrackClipFromJson(trackObject);
        LaneData lane;
        lane.label = clip.label.trimmed().isEmpty()
            ? QStringLiteral("Lane %1").arg(index + 1)
            : clip.label.trimmed();
        if (clip.attachedAudio.has_value())
        {
            lane.audioClips.push_back(clip);
        }
        node.lanes.push_back(lane);
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
    if (schemaVersion == 2)
    {
        return loadLegacyDocumentV2(root);
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
    const auto lanesArray = nodeObject.value(QStringLiteral("lanes")).toArray();
    node.lanes.reserve(static_cast<std::size_t>(lanesArray.size()));
    for (const auto& laneValue : lanesArray)
    {
        if (!laneValue.isObject())
        {
            continue;
        }
        node.lanes.push_back(laneFromJson(laneValue.toObject()));
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

    QJsonArray lanesArray;
    for (const auto& lane : document.node.lanes)
    {
        LaneData savedLane = lane;
        for (auto& savedClip : savedLane.audioClips)
        {
            if (savedClip.attachedAudio.has_value() && savedClip.embeddedAudioData.isEmpty())
            {
                const auto assetPath = savedClip.attachedAudio->assetPath;
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
                    savedClip.embeddedAudioData = audioFile.readAll();
                    savedClip.embeddedAudioFileName = QFileInfo(assetPath).fileName();
                }
            }
        }
        lanesArray.append(laneToJson(savedLane));
    }

    QJsonObject nodeObject{
        {QStringLiteral("label"), document.node.label},
        {QStringLiteral("autoPanEnabled"), document.node.autoPanEnabled},
        {QStringLiteral("timelineFrameCount"), document.node.timelineFrameCount},
        {QStringLiteral("timelineFps"), document.node.timelineFps},
        {QStringLiteral("lanes"), lanesArray}
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
