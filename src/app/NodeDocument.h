#pragma once

#include <optional>
#include <vector>

#include <QByteArray>
#include <QString>

#include "core/tracking/TrackTypes.h"

namespace dawg::node
{
constexpr int kSchemaVersion = 2;
constexpr auto kNodeFileSuffix = ".node";
constexpr auto kLegacyNodeFileSuffix = ".dawgnode";

struct TrackData
{
    QString label;
    std::optional<AudioAttachment> attachedAudio;
    QString embeddedAudioFileName;
    QByteArray embeddedAudioData;
};

struct NodeData
{
    QString label;
    bool autoPanEnabled = true;
    int timelineFrameCount = 0;
    double timelineFps = 0.0;
    std::vector<TrackData> tracks;
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
