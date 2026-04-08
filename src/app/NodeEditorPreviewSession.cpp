#include "app/NodeEditorPreviewSession.h"

#include <algorithm>

#include <QFileInfo>
#include <QDebug>
#include <QScopedValueRollback>

#include "app/NodeDocument.h"
#include "app/NodeEditorDocumentUtils.h"
#include "app/PlayerController.h"
#include "app/TransportUiSyncController.h"
#include "ui/NodeEditorQuickController.h"

namespace
{
constexpr qint64 kNodePreviewMixMeterIntervalMs = 66;
constexpr int kNodePreviewAudioResyncIntervalMs = 5000;
}

NodeEditorPreviewSession::NodeEditorPreviewSession(
    PlayerController& controller,
    NodeEditorQuickController& nodeEditorQuickController,
    QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_nodeEditorQuickController(nodeEditorQuickController)
{
    m_meterTimer.setTimerType(Qt::PreciseTimer);
    m_meterTimer.setInterval(16);
    connect(&m_meterTimer, &QTimer::timeout, this, [this]()
    {
        updateMeters();
    });
}

void NodeEditorPreviewSession::setTransportUiSyncController(TransportUiSyncController* transportUiSyncController)
{
    m_transportUiSyncController = transportUiSyncController;
}

void NodeEditorPreviewSession::setStatusCallback(std::function<void(const QString&)> statusCallback)
{
    m_statusCallback = std::move(statusCallback);
}

void NodeEditorPreviewSession::setMixViewRefreshCallback(std::function<void()> refreshCallback)
{
    m_mixViewRefreshCallback = std::move(refreshCallback);
}

void NodeEditorPreviewSession::setMixMeterRefreshCallback(std::function<void()> refreshCallback)
{
    m_mixMeterRefreshCallback = std::move(refreshCallback);
}

bool NodeEditorPreviewSession::isActive() const
{
    return m_active;
}

bool NodeEditorPreviewSession::isUpdatingPlayhead() const
{
    return m_updatingPlayhead;
}

int NodeEditorPreviewSession::nodeDurationMs() const
{
    return m_previewNodeDurationMs;
}

void NodeEditorPreviewSession::resetPlayheadToStart()
{
    const QScopedValueRollback playbackUpdateGuard{m_updatingPlayhead, true};
    m_nodeEditorQuickController.setInsertionMarkerMs(0);
    m_nodeEditorQuickController.setPlayheadMs(0);
    m_previewAnchorMs = 0;
    m_previewStartMs = 0;

    if (m_transportUiSyncController)
    {
        m_transportUiSyncController->syncProjectPlayheadToNodeEditor(0);
    }

    if (!m_active)
    {
        return;
    }

    static_cast<void>(m_controller.syncNodeEditorPreview(
        m_previewClips,
        m_previewNodeDurationMs,
        0,
        true));
    if (m_mixMeterRefreshCallback)
    {
        m_mixMeterRefreshCallback();
    }
}

void NodeEditorPreviewSession::syncFromBoundDocument(const bool forcePreviewSync)
{
    if (!m_active || !m_controller.hasSelection())
    {
        return;
    }

    dawg::node::Document nodeDocument;
    if (!loadSelectedNodeDocument(&nodeDocument))
    {
        return;
    }

    const auto playheadMs = m_nodeEditorQuickController.playheadMs();
    m_previewClips = previewClipsFromDocument(nodeDocument);
    m_activeAudioSignature.clear();
    m_lastAudioSyncMs = -1;
    static_cast<void>(m_controller.syncNodeEditorPreview(
        m_previewClips,
        m_previewNodeDurationMs,
        playheadMs,
        forcePreviewSync));
}

bool NodeEditorPreviewSession::start()
{
    if (!m_controller.hasSelection())
    {
        return false;
    }

    dawg::node::Document nodeDocument;
    QString errorMessage;
    if (!loadSelectedNodeDocument(&nodeDocument, &errorMessage))
    {
        showStatus(
            errorMessage.isEmpty()
                ? QStringLiteral("Save or import audio into this node before previewing it.")
                : errorMessage);
        return false;
    }

    m_previewClips = previewClipsFromDocument(nodeDocument);
    m_previewNodeDurationMs = std::max(1, m_nodeEditorQuickController.nodeDurationMs());
    if (m_transportUiSyncController)
    {
        m_transportUiSyncController->resetNodeEditorSync();
    }
    m_activeAudioSignature.clear();
    m_lastAudioSyncMs = -1;

    auto playheadMs = std::clamp(
        m_nodeEditorQuickController.playheadMs(),
        0,
        m_previewNodeDurationMs);
    if (playheadMs >= m_previewNodeDurationMs)
    {
        playheadMs = 0;
        m_nodeEditorQuickController.setPlayheadMs(playheadMs);
    }

    const auto projectFrame = m_transportUiSyncController
        ? m_transportUiSyncController->nodeEditorProjectFrameForPlayheadMs(playheadMs)
        : std::nullopt;
    if (!projectFrame.has_value())
    {
        showStatus(QStringLiteral("Failed to resolve the node preview timeline position."));
        return false;
    }

    if (!m_controller.startNodeEditorPreview(
            m_previewClips,
            m_previewNodeDurationMs,
            playheadMs,
            *projectFrame))
    {
        showStatus(QStringLiteral("Failed to start node preview."));
        return false;
    }

    m_previewAnchorMs = playheadMs;
    m_previewStartMs = playheadMs;
    m_activeAudioSignature = activeAudioSignature(playheadMs);
    m_lastAudioSyncMs = playheadMs;
    m_nodeEditorQuickController.setInsertionMarkerMs(playheadMs);
    m_active = true;
    m_nodeEditorQuickController.setPlaybackActive(true);
    m_mixMeterRefreshTimer.invalidate();
    updateMeters();
    m_meterTimer.start();
    if (m_transportUiSyncController)
    {
        m_transportUiSyncController->syncThumbnailStripMarkerToNodeEditor(playheadMs);
    }
    showStatus(QStringLiteral("Playing node preview."));
    return true;
}

void NodeEditorPreviewSession::stop(const bool restorePlaybackAnchor)
{
    const auto wasPlaying = m_active;
    m_active = false;
    if (m_meterTimer.isActive())
    {
        m_meterTimer.stop();
    }
    m_mixMeterRefreshTimer.invalidate();
    m_controller.stopNodeEditorPreview();
    m_nodeEditorQuickController.setLaneMeterStates({});
    m_previewClips.clear();
    m_previewNodeDurationMs = 0;
    if (m_transportUiSyncController)
    {
        m_transportUiSyncController->resetNodeEditorSync();
    }
    m_activeAudioSignature.clear();
    m_lastAudioSyncMs = -1;
    if (wasPlaying
        && restorePlaybackAnchor
        && !m_controller.isInsertionFollowsPlayback())
    {
        const QScopedValueRollback playbackUpdateGuard{m_updatingPlayhead, true};
        m_nodeEditorQuickController.setPlayheadMs(m_previewAnchorMs);
    }
    m_nodeEditorQuickController.setPlaybackActive(false);
    m_previewAnchorMs = 0;
    m_previewStartMs = 0;
    if (wasPlaying && m_mixViewRefreshCallback)
    {
        m_mixViewRefreshCallback();
    }
}

void NodeEditorPreviewSession::toggle()
{
    if (m_active)
    {
        stop();
        showStatus(QStringLiteral("Stopped node preview."));
        return;
    }

    static_cast<void>(start());
}

void NodeEditorPreviewSession::handleFrameAdvanced(const int frameIndex)
{
    if (m_transportUiSyncController)
    {
        const QScopedValueRollback playbackUpdateGuard{m_updatingPlayhead, true};
        m_transportUiSyncController->syncNodeEditorPlayheadToProjectFrame(frameIndex);
    }
    if (!m_active)
    {
        return;
    }

    const auto playheadMs = std::clamp(
        m_nodeEditorQuickController.playheadMs(),
        0,
        std::max(0, m_previewNodeDurationMs));
    const auto selectedRange = m_controller.selectedTrackFrameRange();
    if (playheadMs >= m_previewNodeDurationMs
        || (selectedRange.has_value() && frameIndex >= selectedRange->second))
    {
        stop(false);
        return;
    }

    if (shouldSyncAudio(playheadMs))
    {
        static_cast<void>(m_controller.syncNodeEditorPreview(
            m_previewClips,
            m_previewNodeDurationMs,
            playheadMs));
    }
}

void NodeEditorPreviewSession::handlePlaybackStateChanged(const bool playing)
{
    if (!playing && m_active)
    {
        stop(false);
    }

    m_nodeEditorQuickController.setPlaybackActive(playing || m_active);
}

void NodeEditorPreviewSession::handlePlayheadChanged(const int playheadMs)
{
    if (m_updatingPlayhead)
    {
        return;
    }

    if (m_transportUiSyncController)
    {
        m_transportUiSyncController->syncProjectPlayheadToNodeEditor(playheadMs);
    }
    if (!m_active || m_previewNodeDurationMs <= 0)
    {
        return;
    }

    m_previewStartMs = std::clamp(playheadMs, 0, m_previewNodeDurationMs);
    m_previewAnchorMs = m_previewStartMs;
    m_nodeEditorQuickController.setInsertionMarkerMs(m_previewAnchorMs);
    static_cast<void>(m_controller.syncNodeEditorPreview(
        m_previewClips,
        m_previewNodeDurationMs,
        m_previewStartMs,
        true));
}

bool NodeEditorPreviewSession::loadSelectedNodeDocument(
    dawg::node::Document* nodeDocument,
    QString* errorMessage) const
{
    if (!nodeDocument)
    {
        return false;
    }

    const auto selectedTrackId = m_controller.selectedTrackId();
    const auto nodeDocumentPath = m_controller.trackNodeDocumentPath(selectedTrackId);
    if (nodeDocumentPath.isEmpty() || !QFileInfo::exists(nodeDocumentPath))
    {
        if (errorMessage)
        {
            errorMessage->clear();
        }
        return false;
    }

    QString loadError;
    const auto loadedDocument = dawg::node::loadDocument(nodeDocumentPath, &loadError);
    if (!loadedDocument.has_value())
    {
        if (errorMessage)
        {
            *errorMessage = loadError;
        }
        return false;
    }

    *nodeDocument = *loadedDocument;
    if (errorMessage)
    {
        errorMessage->clear();
    }
    return true;
}

std::vector<AudioPlaybackCoordinator::NodePreviewClip> NodeEditorPreviewSession::previewClipsFromDocument(
    const dawg::node::Document& nodeDocument) const
{
    return dawg::nodeeditor::nodePreviewClipsFromDocument(
        nodeDocument,
        [this](const QString& filePath) -> std::optional<int>
        {
            return m_controller.audioFileDurationMs(filePath);
        },
        [this](const QString& filePath) -> std::optional<int>
        {
            return m_controller.audioFileChannelCount(filePath);
        });
}

QString NodeEditorPreviewSession::activeAudioSignature(const int playheadMs) const
{
    QStringList activeClipIds;
    activeClipIds.reserve(static_cast<int>(m_previewClips.size()));
    const auto clampedPlayheadMs = std::clamp(playheadMs, 0, std::max(0, m_previewNodeDurationMs));
    for (const auto& clip : m_previewClips)
    {
        if (clip.previewTrackId.isNull()
            || clip.assetPath.isEmpty()
            || clip.clipEndMs <= clip.clipStartMs)
        {
            continue;
        }

        const auto elapsedWithinClipMs = clampedPlayheadMs - std::max(0, clip.laneOffsetMs);
        const auto clipDurationMs = std::max(1, clip.clipEndMs - clip.clipStartMs);
        if (elapsedWithinClipMs < 0 || (!clip.loopEnabled && elapsedWithinClipMs >= clipDurationMs))
        {
            continue;
        }

        activeClipIds.push_back(clip.previewTrackId.toString(QUuid::WithoutBraces));
    }
    activeClipIds.sort();
    return activeClipIds.join(QLatin1Char('|'));
}

bool NodeEditorPreviewSession::shouldSyncAudio(const int playheadMs)
{
    const auto nextSignature = activeAudioSignature(playheadMs);
    const auto activeSetChanged = nextSignature != m_activeAudioSignature;
    const auto needsPeriodicResync = m_lastAudioSyncMs < 0
        || std::abs(playheadMs - m_lastAudioSyncMs) >= kNodePreviewAudioResyncIntervalMs;
    if (!activeSetChanged && !needsPeriodicResync)
    {
        return false;
    }

    m_activeAudioSignature = nextSignature;
    m_lastAudioSyncMs = playheadMs;
    return true;
}

void NodeEditorPreviewSession::updateMeters()
{
    QVariantList meterStates;
    if (m_active)
    {
        const auto laneMeterStates = m_controller.nodePreviewLaneMeterStates();
        meterStates.reserve(static_cast<qsizetype>(laneMeterStates.size()));
        for (const auto& state : laneMeterStates)
        {
            meterStates.push_back(QVariantMap{
                {QStringLiteral("laneId"), state.laneId},
                {QStringLiteral("meterLevel"), state.meterLevel},
                {QStringLiteral("meterLeftLevel"), state.meterLeftLevel},
                {QStringLiteral("meterRightLevel"), state.meterRightLevel},
                {QStringLiteral("useStereoMeter"), state.useStereoMeter}
            });
        }
    }
    m_nodeEditorQuickController.setLaneMeterStates(meterStates);

    if (!m_active)
    {
        return;
    }
    if (!m_mixMeterRefreshTimer.isValid()
        || m_mixMeterRefreshTimer.elapsed() >= kNodePreviewMixMeterIntervalMs)
    {
        if (m_mixMeterRefreshCallback)
        {
            m_mixMeterRefreshCallback();
        }
        m_mixMeterRefreshTimer.restart();
    }
}

void NodeEditorPreviewSession::showStatus(const QString& message) const
{
    if (m_statusCallback)
    {
        m_statusCallback(message);
    }
}
