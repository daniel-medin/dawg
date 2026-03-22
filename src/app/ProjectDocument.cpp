#include "app/ProjectDocument.h"

#include <algorithm>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{
using dawg::project::ControllerState;
using dawg::project::Document;
using dawg::project::MixLaneState;
using dawg::project::UiState;
using TimelineLoopRange = ::TimelineLoopRange;

QJsonObject colorToJson(const QColor& color)
{
    return QJsonObject{
        {QStringLiteral("r"), color.red()},
        {QStringLiteral("g"), color.green()},
        {QStringLiteral("b"), color.blue()},
        {QStringLiteral("a"), color.alpha()}
    };
}

QColor colorFromJson(const QJsonObject& object)
{
    return QColor(
        object.value(QStringLiteral("r")).toInt(),
        object.value(QStringLiteral("g")).toInt(),
        object.value(QStringLiteral("b")).toInt(),
        object.value(QStringLiteral("a")).toInt(255));
}

QJsonObject pointToJson(const QPointF& point)
{
    return QJsonObject{
        {QStringLiteral("x"), point.x()},
        {QStringLiteral("y"), point.y()}
    };
}

QPointF pointFromJson(const QJsonObject& object)
{
    return QPointF{
        object.value(QStringLiteral("x")).toDouble(),
        object.value(QStringLiteral("y")).toDouble()
    };
}

QJsonObject audioAttachmentToJson(const AudioAttachment& attachment)
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

QJsonObject trackPointToJson(const TrackPoint& track)
{
    QJsonArray samples;
    for (const auto& [frameIndex, point] : track.samples)
    {
        samples.append(QJsonObject{
            {QStringLiteral("frameIndex"), frameIndex},
            {QStringLiteral("point"), pointToJson(point)}
        });
    }

    QJsonObject object{
        {QStringLiteral("id"), track.id.toString(QUuid::WithoutBraces)},
        {QStringLiteral("label"), track.label},
        {QStringLiteral("color"), colorToJson(track.color)},
        {QStringLiteral("seedFrameIndex"), track.seedFrameIndex},
        {QStringLiteral("startFrame"), track.startFrame},
        {QStringLiteral("motionTracked"), track.motionTracked},
        {QStringLiteral("showLabel"), track.showLabel},
        {QStringLiteral("autoPanEnabled"), track.autoPanEnabled},
        {QStringLiteral("samples"), samples}
    };
    if (track.endFrame.has_value())
    {
        object.insert(QStringLiteral("endFrame"), *track.endFrame);
    }
    if (track.attachedAudio.has_value())
    {
        object.insert(QStringLiteral("attachedAudio"), audioAttachmentToJson(*track.attachedAudio));
    }
    return object;
}

TrackPoint trackPointFromJson(const QJsonObject& object)
{
    TrackPoint track;
    track.id = QUuid(object.value(QStringLiteral("id")).toString());
    if (track.id.isNull())
    {
        track.id = QUuid::createUuid();
    }
    track.label = object.value(QStringLiteral("label")).toString();
    track.color = colorFromJson(object.value(QStringLiteral("color")).toObject());
    track.seedFrameIndex = object.value(QStringLiteral("seedFrameIndex")).toInt(-1);
    track.startFrame = object.value(QStringLiteral("startFrame")).toInt(-1);
    if (object.contains(QStringLiteral("endFrame")) && !object.value(QStringLiteral("endFrame")).isNull())
    {
        track.endFrame = object.value(QStringLiteral("endFrame")).toInt();
    }
    track.motionTracked = object.value(QStringLiteral("motionTracked")).toBool(false);
    track.showLabel = object.value(QStringLiteral("showLabel")).toBool(false);
    track.autoPanEnabled = object.value(QStringLiteral("autoPanEnabled")).toBool(true);

    const auto samples = object.value(QStringLiteral("samples")).toArray();
    for (const auto& sampleValue : samples)
    {
        const auto sampleObject = sampleValue.toObject();
        track.samples.emplace(
            sampleObject.value(QStringLiteral("frameIndex")).toInt(),
            pointFromJson(sampleObject.value(QStringLiteral("point")).toObject()));
    }

    if (object.contains(QStringLiteral("attachedAudio")) && object.value(QStringLiteral("attachedAudio")).isObject())
    {
        track.attachedAudio = audioAttachmentFromJson(object.value(QStringLiteral("attachedAudio")).toObject());
    }

    return track;
}

QJsonObject motionTrackerStateToJson(const MotionTrackerState& state)
{
    QJsonArray tracks;
    for (const auto& track : state.tracks)
    {
        tracks.append(trackPointToJson(track));
    }

    return QJsonObject{
        {QStringLiteral("nextColorIndex"), state.nextColorIndex},
        {QStringLiteral("tracks"), tracks}
    };
}

MotionTrackerState motionTrackerStateFromJson(const QJsonObject& object)
{
    MotionTrackerState state;
    state.nextColorIndex = object.value(QStringLiteral("nextColorIndex")).toInt(0);
    const auto tracks = object.value(QStringLiteral("tracks")).toArray();
    state.tracks.reserve(static_cast<std::size_t>(tracks.size()));
    for (const auto& trackValue : tracks)
    {
        state.tracks.push_back(trackPointFromJson(trackValue.toObject()));
    }
    return state;
}

QJsonArray stringVectorToJson(const std::vector<QString>& values)
{
    QJsonArray array;
    for (const auto& value : values)
    {
        array.append(value);
    }
    return array;
}

std::vector<QString> stringVectorFromJson(const QJsonArray& array)
{
    std::vector<QString> values;
    values.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& value : array)
    {
        values.push_back(value.toString());
    }
    return values;
}

QJsonArray intVectorToJson(const std::vector<int>& values)
{
    QJsonArray array;
    for (const auto value : values)
    {
        array.append(value);
    }
    return array;
}

std::vector<int> intVectorFromJson(const QJsonArray& array)
{
    std::vector<int> values;
    values.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& value : array)
    {
        values.push_back(value.toInt());
    }
    return values;
}

QJsonArray uuidVectorToJson(const std::vector<QUuid>& values)
{
    QJsonArray array;
    for (const auto& value : values)
    {
        array.append(value.toString(QUuid::WithoutBraces));
    }
    return array;
}

std::vector<QUuid> uuidVectorFromJson(const QJsonArray& array)
{
    std::vector<QUuid> values;
    values.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& value : array)
    {
        const QUuid id(value.toString());
        if (!id.isNull())
        {
            values.push_back(id);
        }
    }
    return values;
}

QJsonArray mixLanesToJson(const std::vector<MixLaneState>& lanes)
{
    QJsonArray array;
    for (const auto& lane : lanes)
    {
        array.append(QJsonObject{
            {QStringLiteral("laneIndex"), lane.laneIndex},
            {QStringLiteral("gainDb"), lane.gainDb},
            {QStringLiteral("muted"), lane.muted},
            {QStringLiteral("soloed"), lane.soloed}
        });
    }
    return array;
}

std::vector<MixLaneState> mixLanesFromJson(const QJsonArray& array)
{
    std::vector<MixLaneState> lanes;
    lanes.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& value : array)
    {
        const auto object = value.toObject();
        lanes.push_back(MixLaneState{
            .laneIndex = object.value(QStringLiteral("laneIndex")).toInt(),
            .gainDb = static_cast<float>(object.value(QStringLiteral("gainDb")).toDouble()),
            .muted = object.value(QStringLiteral("muted")).toBool(false),
            .soloed = object.value(QStringLiteral("soloed")).toBool(false)
        });
    }
    return lanes;
}

QJsonArray clipPlayheadsToJson(const std::vector<std::pair<QUuid, int>>& playheads)
{
    QJsonArray array;
    for (const auto& [trackId, playheadMs] : playheads)
    {
        array.append(QJsonObject{
            {QStringLiteral("trackId"), trackId.toString(QUuid::WithoutBraces)},
            {QStringLiteral("playheadMs"), playheadMs}
        });
    }
    return array;
}

std::vector<std::pair<QUuid, int>> clipPlayheadsFromJson(const QJsonArray& array)
{
    std::vector<std::pair<QUuid, int>> playheads;
    playheads.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& value : array)
    {
        const auto object = value.toObject();
        const QUuid trackId(object.value(QStringLiteral("trackId")).toString());
        if (trackId.isNull())
        {
            continue;
        }
        playheads.emplace_back(trackId, object.value(QStringLiteral("playheadMs")).toInt());
    }
    return playheads;
}

QJsonArray loopRangesToJson(const std::vector<TimelineLoopRange>& ranges)
{
    QJsonArray array;
    for (const auto& range : ranges)
    {
        array.append(QJsonObject{
            {QStringLiteral("startFrame"), range.startFrame},
            {QStringLiteral("endFrame"), range.endFrame}
        });
    }
    return array;
}

std::vector<TimelineLoopRange> loopRangesFromJson(const QJsonArray& array)
{
    std::vector<TimelineLoopRange> ranges;
    ranges.reserve(static_cast<std::size_t>(array.size()));
    for (const auto& value : array)
    {
        const auto object = value.toObject();
        auto startFrame = object.value(QStringLiteral("startFrame")).toInt();
        auto endFrame = object.value(QStringLiteral("endFrame")).toInt();
        if (endFrame < startFrame)
        {
            std::swap(startFrame, endFrame);
        }
        ranges.push_back(TimelineLoopRange{
            .startFrame = startFrame,
            .endFrame = endFrame
        });
    }
    std::sort(
        ranges.begin(),
        ranges.end(),
        [](const TimelineLoopRange& left, const TimelineLoopRange& right)
        {
            if (left.startFrame != right.startFrame)
            {
                return left.startFrame < right.startFrame;
            }
            return left.endFrame < right.endFrame;
        });
    return ranges;
}

QJsonObject controllerStateToJson(const ControllerState& state)
{
    QJsonObject object{
        {QStringLiteral("videoPath"), state.videoPath},
        {QStringLiteral("audioPoolAssetPaths"), stringVectorToJson(state.audioPoolAssetPaths)},
        {QStringLiteral("trackerState"), motionTrackerStateToJson(state.trackerState)},
        {QStringLiteral("selectedTrackIds"), uuidVectorToJson(state.selectedTrackIds)},
        {QStringLiteral("currentFrameIndex"), state.currentFrameIndex},
        {QStringLiteral("motionTrackingEnabled"), state.motionTrackingEnabled},
        {QStringLiteral("insertionFollowsPlayback"), state.insertionFollowsPlayback},
        {QStringLiteral("fastPlaybackEnabled"), state.fastPlaybackEnabled},
        {QStringLiteral("embeddedVideoAudioMuted"), state.embeddedVideoAudioMuted},
        {QStringLiteral("loopRanges"), loopRangesToJson(state.loopRanges)},
        {QStringLiteral("masterMixGainDb"), state.masterMixGainDb},
        {QStringLiteral("masterMixMuted"), state.masterMixMuted},
        {QStringLiteral("mixSoloXorMode"), state.mixSoloXorMode},
        {QStringLiteral("mixLanes"), mixLanesToJson(state.mixLanes)},
        {QStringLiteral("clipEditorPlayheads"), clipPlayheadsToJson(state.clipEditorPlayheads)}
    };
    return object;
}

ControllerState controllerStateFromJson(const QJsonObject& object)
{
    ControllerState state;
    state.videoPath = object.value(QStringLiteral("videoPath")).toString();
    state.audioPoolAssetPaths = stringVectorFromJson(object.value(QStringLiteral("audioPoolAssetPaths")).toArray());
    state.trackerState = motionTrackerStateFromJson(object.value(QStringLiteral("trackerState")).toObject());
    state.selectedTrackIds = uuidVectorFromJson(object.value(QStringLiteral("selectedTrackIds")).toArray());
    state.currentFrameIndex = object.value(QStringLiteral("currentFrameIndex")).toInt(0);
    state.motionTrackingEnabled = object.value(QStringLiteral("motionTrackingEnabled")).toBool(false);
    state.insertionFollowsPlayback = object.value(QStringLiteral("insertionFollowsPlayback")).toBool(false);
    state.fastPlaybackEnabled = object.value(QStringLiteral("fastPlaybackEnabled")).toBool(false);
    state.embeddedVideoAudioMuted = object.value(QStringLiteral("embeddedVideoAudioMuted")).toBool(true);
    if (object.contains(QStringLiteral("loopRanges")) && object.value(QStringLiteral("loopRanges")).isArray())
    {
        state.loopRanges = loopRangesFromJson(object.value(QStringLiteral("loopRanges")).toArray());
    }
    else if (object.contains(QStringLiteral("loopStartFrame")) || object.contains(QStringLiteral("loopEndFrame")))
    {
        const auto startFrame = object.contains(QStringLiteral("loopStartFrame")) && !object.value(QStringLiteral("loopStartFrame")).isNull()
            ? object.value(QStringLiteral("loopStartFrame")).toInt()
            : object.value(QStringLiteral("loopEndFrame")).toInt();
        const auto endFrame = object.contains(QStringLiteral("loopEndFrame")) && !object.value(QStringLiteral("loopEndFrame")).isNull()
            ? object.value(QStringLiteral("loopEndFrame")).toInt()
            : startFrame;
        state.loopRanges.push_back(TimelineLoopRange{
            .startFrame = std::min(startFrame, endFrame),
            .endFrame = std::max(startFrame, endFrame)
        });
    }
    state.masterMixGainDb = static_cast<float>(object.value(QStringLiteral("masterMixGainDb")).toDouble());
    state.masterMixMuted = object.value(QStringLiteral("masterMixMuted")).toBool(false);
    state.mixSoloXorMode = object.value(QStringLiteral("mixSoloXorMode")).toBool(false);
    state.mixLanes = mixLanesFromJson(object.value(QStringLiteral("mixLanes")).toArray());
    state.clipEditorPlayheads = clipPlayheadsFromJson(object.value(QStringLiteral("clipEditorPlayheads")).toArray());
    return state;
}

QJsonObject uiStateToJson(const UiState& state)
{
    return QJsonObject{
        {QStringLiteral("videoDetached"), state.videoDetached},
        {QStringLiteral("detachedVideoWindowGeometry"),
         QString::fromLatin1(state.detachedVideoWindowGeometry.toBase64())},
        {QStringLiteral("timelineDetached"), state.timelineDetached},
        {QStringLiteral("detachedTimelineWindowGeometry"),
         QString::fromLatin1(state.detachedTimelineWindowGeometry.toBase64())},
        {QStringLiteral("clipEditorDetached"), state.clipEditorDetached},
        {QStringLiteral("detachedClipEditorWindowGeometry"),
         QString::fromLatin1(state.detachedClipEditorWindowGeometry.toBase64())},
        {QStringLiteral("mixDetached"), state.mixDetached},
        {QStringLiteral("detachedMixWindowGeometry"),
         QString::fromLatin1(state.detachedMixWindowGeometry.toBase64())},
        {QStringLiteral("audioPoolDetached"), state.audioPoolDetached},
        {QStringLiteral("detachedAudioPoolWindowGeometry"),
         QString::fromLatin1(state.detachedAudioPoolWindowGeometry.toBase64())},
        {QStringLiteral("timelineVisible"), state.timelineVisible},
        {QStringLiteral("clipEditorVisible"), state.clipEditorVisible},
        {QStringLiteral("mixVisible"), state.mixVisible},
        {QStringLiteral("audioPoolVisible"), state.audioPoolVisible},
        {QStringLiteral("audioPoolShowLength"), state.audioPoolShowLength},
        {QStringLiteral("audioPoolShowSize"), state.audioPoolShowSize},
        {QStringLiteral("showAllNodeNames"), state.showAllNodeNames},
        {QStringLiteral("timelineClickSeeks"), state.timelineClickSeeks},
        {QStringLiteral("timelineThumbnailsVisible"), state.timelineThumbnailsVisible},
        {QStringLiteral("audioPoolPreferredWidth"), state.audioPoolPreferredWidth},
        {QStringLiteral("timelinePreferredHeight"), state.timelinePreferredHeight},
        {QStringLiteral("clipEditorPreferredHeight"), state.clipEditorPreferredHeight},
        {QStringLiteral("mixPreferredHeight"), state.mixPreferredHeight},
        {QStringLiteral("contentSplitterSizes"), intVectorToJson(state.contentSplitterSizes)},
        {QStringLiteral("mainVerticalSplitterSizes"), intVectorToJson(state.mainVerticalSplitterSizes)},
        {QStringLiteral("windowGeometry"), QString::fromLatin1(state.windowGeometry.toBase64())},
        {QStringLiteral("windowMaximized"), state.windowMaximized}
    };
}

UiState uiStateFromJson(const QJsonObject& object)
{
    UiState state;
    state.videoDetached = object.value(QStringLiteral("videoDetached")).toBool(false);
    state.detachedVideoWindowGeometry = QByteArray::fromBase64(
        object.value(QStringLiteral("detachedVideoWindowGeometry")).toString().toLatin1());
    state.timelineDetached = object.value(QStringLiteral("timelineDetached")).toBool(false);
    state.detachedTimelineWindowGeometry = QByteArray::fromBase64(
        object.value(QStringLiteral("detachedTimelineWindowGeometry")).toString().toLatin1());
    state.clipEditorDetached = object.value(QStringLiteral("clipEditorDetached")).toBool(false);
    state.detachedClipEditorWindowGeometry = QByteArray::fromBase64(
        object.value(QStringLiteral("detachedClipEditorWindowGeometry")).toString().toLatin1());
    state.mixDetached = object.value(QStringLiteral("mixDetached")).toBool(false);
    state.detachedMixWindowGeometry = QByteArray::fromBase64(
        object.value(QStringLiteral("detachedMixWindowGeometry")).toString().toLatin1());
    state.audioPoolDetached = object.value(QStringLiteral("audioPoolDetached")).toBool(false);
    state.detachedAudioPoolWindowGeometry = QByteArray::fromBase64(
        object.value(QStringLiteral("detachedAudioPoolWindowGeometry")).toString().toLatin1());
    state.timelineVisible = object.value(QStringLiteral("timelineVisible")).toBool(true);
    state.clipEditorVisible = object.value(QStringLiteral("clipEditorVisible")).toBool(false);
    state.mixVisible = object.value(QStringLiteral("mixVisible")).toBool(false);
    state.audioPoolVisible = object.value(QStringLiteral("audioPoolVisible")).toBool(false);
    state.audioPoolShowLength = object.value(QStringLiteral("audioPoolShowLength")).toBool(true);
    state.audioPoolShowSize = object.value(QStringLiteral("audioPoolShowSize")).toBool(true);
    state.showAllNodeNames = object.value(QStringLiteral("showAllNodeNames")).toBool(true);
    state.timelineClickSeeks = object.value(QStringLiteral("timelineClickSeeks")).toBool(true);
    state.timelineThumbnailsVisible = object.value(QStringLiteral("timelineThumbnailsVisible")).toBool(true);
    state.audioPoolPreferredWidth = object.value(QStringLiteral("audioPoolPreferredWidth")).toInt(320);
    state.timelinePreferredHeight = object.value(QStringLiteral("timelinePreferredHeight")).toInt(148);
    state.clipEditorPreferredHeight = object.value(QStringLiteral("clipEditorPreferredHeight")).toInt(224);
    state.mixPreferredHeight = object.value(QStringLiteral("mixPreferredHeight")).toInt(368);
    state.contentSplitterSizes = intVectorFromJson(object.value(QStringLiteral("contentSplitterSizes")).toArray());
    state.mainVerticalSplitterSizes = intVectorFromJson(object.value(QStringLiteral("mainVerticalSplitterSizes")).toArray());
    state.windowGeometry = QByteArray::fromBase64(
        object.value(QStringLiteral("windowGeometry")).toString().toLatin1());
    state.windowMaximized = object.value(QStringLiteral("windowMaximized")).toBool(false);
    return state;
}
}

namespace dawg::project
{
QString sanitizeProjectName(const QString& name)
{
    QString sanitized = name.trimmed();
    if (sanitized.isEmpty())
    {
        return QStringLiteral("Untitled Project");
    }

    for (QChar& character : sanitized)
    {
        switch (character.unicode())
        {
        case '<':
        case '>':
        case ':':
        case '"':
        case '/':
        case '\\':
        case '|':
        case '?':
        case '*':
            character = QChar('_');
            break;
        default:
            break;
        }
    }

    return sanitized;
}

QString projectFileNameForName(const QString& projectName)
{
    return sanitizeProjectName(projectName) + QString::fromLatin1(kProjectFileSuffix);
}

std::optional<Document> loadDocument(const QString& projectFilePath, QString* errorMessage)
{
    QFile file(projectFilePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to open project file %1.")
                .arg(QFileInfo(projectFilePath).fileName());
        }
        return std::nullopt;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Project file %1 is not valid JSON.")
                .arg(QFileInfo(projectFilePath).fileName());
        }
        return std::nullopt;
    }

    const auto root = document.object();
    const auto schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (schemaVersion != 1 && schemaVersion != 2 && schemaVersion != 3 && schemaVersion != kSchemaVersion)
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Project file %1 uses unsupported schema version %2.")
                .arg(QFileInfo(projectFilePath).fileName())
                .arg(schemaVersion);
        }
        return std::nullopt;
    }

    Document loaded;
    loaded.name = root.value(QStringLiteral("name")).toString(QFileInfo(projectFilePath).completeBaseName());
    loaded.controller = controllerStateFromJson(root.value(QStringLiteral("controller")).toObject());
    loaded.ui = uiStateFromJson(root.value(QStringLiteral("ui")).toObject());
    return loaded;
}

bool saveDocument(const QString& projectFilePath, const Document& document, QString* errorMessage)
{
    const QJsonObject root{
        {QStringLiteral("schemaVersion"), kSchemaVersion},
        {QStringLiteral("name"), sanitizeProjectName(document.name)},
        {QStringLiteral("controller"), controllerStateToJson(document.controller)},
        {QStringLiteral("ui"), uiStateToJson(document.ui)}
    };

    QSaveFile file(projectFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to write project file %1.")
                .arg(QFileInfo(projectFilePath).fileName());
        }
        return false;
    }

    const auto json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size() || !file.commit())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to finish writing project file %1.")
                .arg(QFileInfo(projectFilePath).fileName());
        }
        return false;
    }

    return true;
}
}
