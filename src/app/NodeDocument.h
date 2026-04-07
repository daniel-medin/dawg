#pragma once

#include <optional>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QUuid>

#include "core/tracking/TrackTypes.h"

namespace dawg::node
{
constexpr int kSchemaVersion = 3;
constexpr auto kNodeFileSuffix = ".node";
constexpr auto kLegacyNodeFileSuffix = ".dawgnode";

struct AudioClipData
{
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString label;
    int laneOffsetMs = 0;
    std::optional<AudioAttachment> attachedAudio;
    QString embeddedAudioFileName;
    QByteArray embeddedAudioData;
};

struct LaneData
{
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString label;
    bool muted = false;
    bool soloed = false;
    std::vector<AudioClipData> audioClips;
};

struct NodeData
{
    QString label;
    bool autoPanEnabled = true;
    int timelineFrameCount = 0;
    double timelineFps = 0.0;
    std::vector<LaneData> lanes;
};

struct Document
{
    QString name;
    NodeData node;
};

QString sanitizeNodeName(const QString& name);
QString nodeFileNameForName(const QString& nodeName);
std::optional<Document> loadDocument(const QString& nodeFilePath, QString* errorMessage = nullptr);
bool saveDocument(const QString& nodeFilePath, const Document& document, QString* errorMessage = nullptr);
}
