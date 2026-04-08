#pragma once

#include <functional>
#include <vector>

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include "app/AudioPlaybackCoordinator.h"
#include "app/NodeDocument.h"

class PlayerController;
class TransportUiSyncController;
class NodeEditorQuickController;

class NodeEditorPreviewSession : public QObject
{
    Q_OBJECT

public:
    NodeEditorPreviewSession(
        PlayerController& controller,
        NodeEditorQuickController& nodeEditorQuickController,
        QObject* parent = nullptr);

    void setTransportUiSyncController(TransportUiSyncController* transportUiSyncController);
    void setStatusCallback(std::function<void(const QString&)> statusCallback);
    void setMixViewRefreshCallback(std::function<void()> refreshCallback);
    void setMixMeterRefreshCallback(std::function<void()> refreshCallback);

    [[nodiscard]] bool isActive() const;
    [[nodiscard]] bool isUpdatingPlayhead() const;
    [[nodiscard]] int nodeDurationMs() const;

    void resetPlayheadToStart();
    void syncFromBoundDocument(bool forcePreviewSync = false);
    [[nodiscard]] bool start();
    void stop(bool restorePlaybackAnchor = true);
    void toggle();
    void handleFrameAdvanced(int frameIndex);
    void handlePlaybackStateChanged(bool playing);
    void handlePlayheadChanged(int playheadMs);

private:
    [[nodiscard]] bool loadSelectedNodeDocument(dawg::node::Document* nodeDocument, QString* errorMessage = nullptr) const;
    [[nodiscard]] std::vector<AudioPlaybackCoordinator::NodePreviewClip> previewClipsFromDocument(
        const dawg::node::Document& nodeDocument) const;
    [[nodiscard]] QString activeAudioSignature(int playheadMs) const;
    [[nodiscard]] bool shouldSyncAudio(int playheadMs);
    void updateMeters();
    void showStatus(const QString& message) const;

    PlayerController& m_controller;
    NodeEditorQuickController& m_nodeEditorQuickController;
    TransportUiSyncController* m_transportUiSyncController = nullptr;
    std::function<void(const QString&)> m_statusCallback;
    std::function<void()> m_mixViewRefreshCallback;
    std::function<void()> m_mixMeterRefreshCallback;
    QTimer m_meterTimer;
    QElapsedTimer m_mixMeterRefreshTimer;
    int m_previewAnchorMs = 0;
    int m_previewStartMs = 0;
    int m_previewNodeDurationMs = 0;
    std::vector<AudioPlaybackCoordinator::NodePreviewClip> m_previewClips;
    bool m_updatingPlayhead = false;
    bool m_active = false;
    QString m_activeAudioSignature;
    int m_lastAudioSyncMs = -1;
};
