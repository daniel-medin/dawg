#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <QAction>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QIcon>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QQuickItem>
#include <QQuickView>
#include <QShortcut>
#include <QTimer>

#include "app/ProjectDocument.h"
#include "ui/ClipEditorTypes.h"
#include "ui/MixTypes.h"
#include "ui/TimelineTypes.h"

class PlayerController;
class DebugOverlayWindow;
class ClipEditorQuickController;
class ClipWaveformQuickItem;
class MixQuickController;
class NativeVideoViewport;
class MainWindowActions;
class ProjectWindowController;
class PanelLayoutController;
class DebugUiController;
class MediaImportController;
class ActionRegistry;
class AudioPoolQuickController;
class ContextMenuController;
class DialogController;
class FilePickerController;
class ShellLayoutController;
class ShellOverlayController;
class QWidget;
class TimelineQuickController;
class VideoViewportQuickController;
class WindowChromeController;
class QThread;

class MainWindow final : public QQuickView
{
    Q_OBJECT

public:
    explicit MainWindow(QWindow* parent = nullptr);
    ~MainWindow() override;
    [[nodiscard]] bool openProjectFilePath(const QString& projectFilePath);
    void setWindowTitle(const QString& title);
    [[nodiscard]] QString windowTitle() const;
    [[nodiscard]] QString currentProjectTitle() const;
    void setWindowIcon(const QIcon& icon);
    [[nodiscard]] QIcon windowIcon() const;
    [[nodiscard]] bool isMaximized() const;
    [[nodiscard]] QByteArray saveGeometry() const;
    bool restoreGeometry(const QByteArray& geometry);
    void restoreLastProjectOnStartup();

    Q_INVOKABLE void requestImportVideo();
    Q_INVOKABLE void requestSeedPoint(double imageX, double imageY);
    Q_INVOKABLE void requestSelectedTrackMoved(double imageX, double imageY);
    Q_INVOKABLE void requestTrackSelected(const QString& trackId);
    Q_INVOKABLE void requestTrackActivated(const QString& trackId);
    Q_INVOKABLE void requestTracksSelected(const QVariantList& trackIds);
    Q_INVOKABLE void requestTrackGainPopup(const QString& trackId, double localX, double localY);
    Q_INVOKABLE void requestTrackGainAdjust(const QString& trackId, int wheelDelta, double localX, double localY);
    Q_INVOKABLE void requestTrackContextMenu(const QString& trackId, double localX, double localY);
    Q_INVOKABLE void requestAudioDropped(const QString& assetPath, double imageX, double imageY);
    Q_INVOKABLE void requestSelectNextNode();
    Q_INVOKABLE void requestSelectNextVisibleNode();
    [[nodiscard]] QPoint mapQuickLocalToGlobal(double localX, double localY) const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private slots:
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void openVideo();
    void importSound();
    void importAudioToPool();
    void trimSelectedNodeToSound();
    void toggleSelectedNodeAutoPan();
    void copySelectedNode();
    void pasteNode();
    void cutSelectedNode();
    void undoNodeEdit();
    void redoNodeEdit();
    void deleteAllEmptyNodes();
    void selectNextVisibleNode();
    void moveSelectedNodeUp();
    void moveSelectedNodeDown();
    void moveSelectedNodeLeft();
    void moveSelectedNodeRight();
    void handleLoopStartShortcut();
    void handleLoopEndShortcut();
    void clearLoopRange();
    void handleNodeStartShortcut();
    void handleNodeEndShortcut();
    void updateFrame(const QImage& image, int frameIndex, double timestampSeconds);
    void updateMemoryUsage();
    void updateDebugText();
    void refreshOverlays();
    void updateInsertionFollowsPlaybackState(bool enabled);
    void updatePlaybackState(bool playing);
    void updateMotionTrackingState(bool enabled);
    void updateSelectionState(bool hasSelection);
    void updateTrackAvailabilityState(bool hasTracks);
    void handleVideoLoaded(const QString& filePath, int totalFrames, double fps);
    void updateDebugVisibility(bool enabled);
    void updateAudioPoolVisibility(bool visible);
    void updateTimelineVisibility(bool visible);
    void updateClipEditorVisibility(bool visible);
    void updateMixVisibility(bool visible);
    void detachVideo();
    void attachVideo();
    void detachTimeline();
    void attachTimeline();
    void detachClipEditor();
    void attachClipEditor();
    void detachMix();
    void attachMix();
    void detachAudioPool();
    void attachAudioPool();
    void refreshAudioPool();
    void updateAudioPoolPlaybackIndicators();
    void updateVideoAudioRow();
    void showStatus(const QString& message);
    void updateNativeViewportVisibility(bool visible);

private:
    struct TimelineThumbnailGenerationRequest
    {
        QString projectRootPath;
        QString videoPath;
        int totalFrames = 0;
        double fps = 0.0;
    };

    friend class MainWindowActions;
    friend class ProjectWindowController;
    friend class PanelLayoutController;
    friend class DebugUiController;
    friend class MediaImportController;
    friend class ActionRegistry;
    friend class AudioPoolQuickController;

    void showNodeContextMenu(const QUuid& trackId, const QPoint& globalPosition, bool includeSoundActions);
    void showLoopContextMenu(const QPoint& globalPosition);
    void buildMenus();
    void buildUi();
    void refreshTimeline();
    void requestProjectTimelineThumbnailsGeneration();
    void startProjectTimelineThumbnailsGeneration(const TimelineThumbnailGenerationRequest& request);
    void handleProjectTimelineThumbnailsGenerationFinished(
        quint64 generationId,
        const TimelineThumbnailGenerationRequest& request,
        bool success,
        const QString& errorMessage);
    void refreshClipEditor();
    void refreshMixView();
    void updateEditActionState();
    void updateOverlayPositions();
    void showCanvasTipsOverlay();
    void hideCanvasTipsOverlay();
    void nudgeSelectedNode(const QPointF& delta);
    void beginHeldNodeNudge(int key);
    void endHeldNodeNudge(int key);
    void applyHeldNodeNudge();
    [[nodiscard]] bool shouldIgnoreNodeMovementShortcuts() const;
    [[nodiscard]] bool shouldApplyNodeShortcutToAll() const;
    void armClearAllShortcut();
    void clearPendingClearAllShortcut();
    void syncMainVerticalPanelSizes();
    void adjustTrackMixGainFromWheel(const QUuid& trackId, int wheelDelta, const QPoint& globalPosition);
    void showTrackMixGainPopup(const QUuid& trackId, const QPoint& globalPosition);
    void hideTrackMixGainPopup();
    void updateTrackMixGainPopupValue(float gainDb);
    void updateDetachedVideoUiState();
    void updateDetachedPanelUiState();
    void resetOutputFpsTracking();
    [[nodiscard]] std::optional<int> timelineLoopTargetFrame() const;
    void createLoopRangeFromShortcutFrames(int startFrame, int endFrame);
    [[nodiscard]] bool hasOpenProject() const;
    [[nodiscard]] bool ensureProjectForMediaAction(const QString& actionLabel);
    [[nodiscard]] bool promptToSaveIfDirty(const QString& actionLabel);
    [[nodiscard]] bool saveProjectToCurrentPath();
    [[nodiscard]] bool saveProjectToPath(const QString& projectFilePath, const QString& projectName);
    [[nodiscard]] bool saveProjectAsNewCopy();
    [[nodiscard]] bool loadProjectFile(const QString& projectFilePath);
    [[nodiscard]] bool openProjectFileWithPrompt(const QString& projectFilePath, const QString& actionLabel);
    [[nodiscard]] bool createProjectAt(const QString& projectName, const QString& parentDirectory);
    [[nodiscard]] std::optional<QString> copyMediaIntoProject(
        const QString& sourcePath,
        const QString& subdirectory,
        QString* errorMessage = nullptr) const;
    [[nodiscard]] dawg::project::UiState snapshotProjectUiState() const;
    void applyProjectUiState(const dawg::project::UiState& state);
    void setCurrentProject(const QString& projectFilePath, const QString& projectName);
    void clearCurrentProject();
    void setProjectDirty(bool dirty);
    void updateWindowTitle();
    [[nodiscard]] QStringList recentProjectPaths() const;
    void storeRecentProjectPaths(const QStringList& projectPaths);
    void addRecentProjectPath(const QString& projectFilePath);
    void removeRecentProjectPath(const QString& projectFilePath);
    void rebuildRecentProjectsMenu();
    [[nodiscard]] QString chooseOpenFileName(
        const QString& title,
        const QString& directory,
        const QString& filter) const;
    [[nodiscard]] QString chooseExistingDirectory(
        const QString& title,
        const QString& directory = {}) const;
    void updateMixMeterLevels();
    [[nodiscard]] bool needsMixRebuild(const std::vector<MixLaneStrip>& laneStrips) const;
    void syncMixStripStates();
    void rebuildMixStrips();
    void updateMixQuickDiagnostics();
    void handleMixQuickStatusChanged();
    void syncShellLayoutViewport();
    void syncShellPanelGeometry();
    void handleTimelineQuickStatusChanged();
    void handleClipEditorQuickStatusChanged();
    void clearTimeline();
    void setTimelineVideoPath(const QString& videoPath);
    void setTimelineState(int totalFrames, double fps);
    void setTimelineCurrentFrame(int frameIndex);
    void setTimelineSeekOnClickEnabled(bool enabled);
    void setTimelineThumbnailsVisible(bool visible);
    [[nodiscard]] std::optional<int> timelineLoopShortcutFrame() const;
    [[nodiscard]] bool timelineHasSelectedLoopRange() const;
    [[nodiscard]] bool timelineHasFocus() const;
    [[nodiscard]] int timelineMinimumHeight() const;
    void updateTimelineMinimumHeight();
    void syncClipWaveformItem();

    PlayerController* m_controller = nullptr;
    std::unique_ptr<MainWindowActions> m_actionsController;
    std::unique_ptr<ProjectWindowController> m_projectWindowController;
    std::unique_ptr<PanelLayoutController> m_panelLayoutController;
    std::unique_ptr<DebugUiController> m_debugUiController;
    std::unique_ptr<MediaImportController> m_mediaImportController;
    ActionRegistry* m_actionRegistry = nullptr;
    WindowChromeController* m_windowChromeController = nullptr;
    QQuickItem* m_shellRootItem = nullptr;
    QQuickItem* m_titleBarItem = nullptr;
    QQuickItem* m_contentAreaItem = nullptr;
    ShellLayoutController* m_shellLayoutController = nullptr;
    QQuickItem* m_videoViewportQuickWidget = nullptr;
    VideoViewportQuickController* m_videoViewportQuickController = nullptr;
    VideoViewportQuickController* m_detachedVideoViewportQuickController = nullptr;
    bool m_nativeVideoPresentationAllowed = false;
    QQuickItem* m_timelineQuickWidget = nullptr;
    TimelineQuickController* m_timelineQuickController = nullptr;
    QQuickItem* m_clipEditorQuickWidget = nullptr;
    ClipEditorQuickController* m_clipEditorQuickController = nullptr;
    ClipWaveformQuickItem* m_clipWaveformItem = nullptr;
    QQuickItem* m_mixQuickWidget = nullptr;
    MixQuickController* m_mixQuickController = nullptr;
    DebugOverlayWindow* m_debugOverlay = nullptr;
    QWidget* m_nativeViewportWindow = nullptr;
    NativeVideoViewport* m_nativeViewport = nullptr;
    QQuickView* m_detachedVideoWindow = nullptr;
    QByteArray m_detachedVideoWindowGeometry;
    QQuickView* m_detachedTimelineWindow = nullptr;
    QByteArray m_detachedTimelineWindowGeometry;
    QQuickView* m_detachedClipEditorWindow = nullptr;
    QByteArray m_detachedClipEditorWindowGeometry;
    QQuickView* m_detachedMixWindow = nullptr;
    QByteArray m_detachedMixWindowGeometry;
    QQuickView* m_detachedAudioPoolWindow = nullptr;
    QByteArray m_detachedAudioPoolWindowGeometry;
    QQuickItem* m_audioPoolQuickWidget = nullptr;
    AudioPoolQuickController* m_audioPoolQuickController = nullptr;
    QQuickItem* m_contextMenuQuickWidget = nullptr;
    ContextMenuController* m_contextMenuController = nullptr;
    QQuickItem* m_dialogOverlayQuickWidget = nullptr;
    DialogController* m_dialogController = nullptr;
    QQuickItem* m_filePickerQuickWidget = nullptr;
    FilePickerController* m_filePickerController = nullptr;
    QQuickItem* m_shellOverlayQuickWidget = nullptr;
    ShellOverlayController* m_shellOverlayController = nullptr;
    QUuid m_trackGainPopupTrackId;
    QUuid m_contextMenuTrackId;
    QString m_contextMenuNodeLabel;
    QAction* m_newProjectAction = nullptr;
    QAction* m_openProjectAction = nullptr;
    QAction* m_saveProjectAction = nullptr;
    QAction* m_saveProjectAsAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_quitAction = nullptr;
    QAction* m_goToStartAction = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_stepForwardAction = nullptr;
    QAction* m_stepBackAction = nullptr;
    QAction* m_stepFastForwardAction = nullptr;
    QAction* m_stepFastBackAction = nullptr;
    QAction* m_insertionFollowsPlaybackAction = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_cutAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_selectAllAction = nullptr;
    QAction* m_unselectAllAction = nullptr;
    QAction* m_setNodeStartAction = nullptr;
    QAction* m_setNodeEndAction = nullptr;
    QAction* m_setLoopStartAction = nullptr;
    QAction* m_setLoopEndAction = nullptr;
    QAction* m_clearLoopRangeAction = nullptr;
    QAction* m_selectNextNodeAction = nullptr;
    QAction* m_moveNodeUpAction = nullptr;
    QAction* m_moveNodeDownAction = nullptr;
    QAction* m_moveNodeLeftAction = nullptr;
    QAction* m_moveNodeRightAction = nullptr;
    QAction* m_trimNodeAction = nullptr;
    QAction* m_autoPanAction = nullptr;
    QAction* m_loopSoundAction = nullptr;
    QAction* m_toggleNodeNameAction = nullptr;
    QAction* m_showAllNodeNamesAction = nullptr;
    QAction* m_importSoundAction = nullptr;
    QAction* m_detachVideoAction = nullptr;
    QAction* m_detachTimelineAction = nullptr;
    QAction* m_detachClipEditorAction = nullptr;
    QAction* m_detachMixAction = nullptr;
    QAction* m_detachAudioPoolAction = nullptr;
    QAction* m_showClipEditorAction = nullptr;
    QAction* m_showTimelineAction = nullptr;
    QAction* m_showMixAction = nullptr;
    QAction* m_showTimelineThumbnailsAction = nullptr;
    QAction* m_timelineClickSeeksAction = nullptr;
    QAction* m_mixSoloModeAction = nullptr;
    QAction* m_audioPoolAction = nullptr;
    QAction* m_deleteNodeAction = nullptr;
    QAction* m_deleteEmptyNodesAction = nullptr;
    QAction* m_clearAllAction = nullptr;
    QAction* m_motionTrackingAction = nullptr;
    QAction* m_toggleDebugAction = nullptr;
    QAction* m_showNativeViewportAction = nullptr;
    QShortcut* m_playPauseShortcut = nullptr;
    QShortcut* m_startShortcut = nullptr;
    QShortcut* m_numpadStartShortcut = nullptr;
    QShortcut* m_stepBackShortcut = nullptr;
    QShortcut* m_stepForwardShortcut = nullptr;
    QShortcut* m_stepFastForwardShortcut = nullptr;
    QShortcut* m_stepFastBackShortcut = nullptr;
    QShortcut* m_insertionFollowsPlaybackShortcut = nullptr;
    QShortcut* m_copyShortcut = nullptr;
    QShortcut* m_pasteShortcut = nullptr;
    QShortcut* m_cutShortcut = nullptr;
    QShortcut* m_undoShortcut = nullptr;
    QShortcut* m_redoShortcut = nullptr;
    QShortcut* m_selectAllShortcut = nullptr;
    QShortcut* m_nodeStartShortcut = nullptr;
    QShortcut* m_nodeEndShortcut = nullptr;
    QShortcut* m_selectNextNodeShortcut = nullptr;
    QShortcut* m_showTimelineShortcut = nullptr;
    QShortcut* m_showClipEditorShortcut = nullptr;
    QShortcut* m_showMixShortcut = nullptr;
    QShortcut* m_trimNodeShortcut = nullptr;
    QShortcut* m_autoPanShortcut = nullptr;
    QShortcut* m_audioPoolShortcut = nullptr;
    QShortcut* m_toggleNodeNameShortcut = nullptr;
    QShortcut* m_deleteShortcut = nullptr;
    QShortcut* m_unselectAllShortcut = nullptr;
    bool m_clearAllShortcutArmed = false;
    bool m_debugVisible = false;
    int m_audioPoolPreferredWidth = 320;
    bool m_audioPoolShowLength = true;
    bool m_audioPoolShowSize = true;
    int m_timelinePreferredHeight = 148;
    int m_clipEditorPreferredHeight = 224;
    int m_mixPreferredHeight = 368;
    QString m_clipName;
    QString m_memoryUsageText;
    QString m_processorUsageText;
    QString m_gpuUsageText;
    QString m_videoMemoryUsageText;
    QString m_qtQuickLoadText;
    QString m_qtQuickGraphicsApiText;
    std::optional<ClipEditorState> m_clipEditorState;
    std::optional<int> m_pendingLoopShortcutStartFrame;
    std::optional<int> m_pendingLoopShortcutEndFrame;
    float m_masterMixGainDb = 0.0F;
    bool m_masterMixMuted = false;
    std::vector<TimelineTrackSpan> m_timelineTrackSpans;
    std::vector<MixLaneStrip> m_mixLaneStrips;
    QImage m_lastPresentedFrame;
    QTimer m_clearAllShortcutTimer;
    QTimer m_memoryUsageTimer;
    QTimer m_mixMeterTimer;
    QTimer m_clipEditorPreviewTimer;
    QTimer m_statusToastTimer;
    QTimer m_canvasTipsTimer;
    QTimer m_nodeNudgeTimer;
    int m_activeNodeNudgeKey = 0;
    QPointF m_activeNodeNudgeDelta;
    bool m_nodeNudgeFastMode = false;
    QElapsedTimer m_outputFpsTimer;
    QElapsedTimer m_debugTextTimer;
    QElapsedTimer m_audioPoolPlaybackRefreshTimer;
    QElapsedTimer m_timelinePlaybackUiTimer;
    int m_outputFpsFrameCount = 0;
    int m_lastTimelinePlaybackUiFrame = -1;
    double m_outputFps = 0.0;
    double m_uiViewportUpdateMs = 0.0;
    double m_uiTimelineUpdateMs = 0.0;
    double m_uiChromeUpdateMs = 0.0;
    double m_uiClipEditorUpdateMs = 0.0;
    double m_uiDebugTextUpdateMs = 0.0;
    bool m_videoDetached = false;
    bool m_timelineDetached = false;
    bool m_clipEditorDetached = false;
    bool m_mixDetached = false;
    bool m_audioPoolDetached = false;
    bool m_shuttingDown = false;
    QThread* m_timelineThumbnailGenerationThread = nullptr;
    std::optional<TimelineThumbnailGenerationRequest> m_pendingTimelineThumbnailGenerationRequest;
    quint64 m_timelineThumbnailGenerationId = 0;
    QString m_currentProjectFilePath;
    QString m_currentProjectRootPath;
    QString m_currentProjectName;
    bool m_projectDirty = false;
    bool m_projectStateChangeInProgress = false;
};
