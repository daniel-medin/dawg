#pragma once

#include <QAction>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QPoint>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

class PlayerController;
class DebugOverlayWindow;
class TimelineView;
class VideoCanvas;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void openVideo();
    void importSound();
    void importAudioToPool();
    void trimSelectedNodeToSound();
    void toggleSelectedNodeAutoPan();
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
    void refreshAudioPool();
    void updateAudioPoolPlaybackIndicators();
    void updateVideoAudioRow();
    void showStatus(const QString& message);

private:
    void showNodeContextMenu(const QUuid& trackId, const QPoint& globalPosition, bool includeSoundActions);
    void buildMenus();
    void buildUi();
    void refreshTimeline();
    [[nodiscard]] bool shouldApplyNodeShortcutToAll() const;
    void syncMotionTrackingUi(bool enabled);
    void tryOpenLocalDevVideo();
    void populateAudioPoolFromLocalDevDirectory();
    void armClearAllShortcut();
    void clearPendingClearAllShortcut();

    PlayerController* m_controller = nullptr;
    VideoCanvas* m_canvas = nullptr;
    TimelineView* m_timeline = nullptr;
    QSplitter* m_contentSplitter = nullptr;
    QSplitter* m_mainVerticalSplitter = nullptr;
    QWidget* m_mainContent = nullptr;
    QFrame* m_timelinePanel = nullptr;
    QLabel* m_frameLabel = nullptr;
    DebugOverlayWindow* m_debugOverlay = nullptr;
    QFrame* m_audioPoolPanel = nullptr;
    QWidget* m_videoAudioRow = nullptr;
    QLabel* m_videoAudioLabel = nullptr;
    QToolButton* m_videoAudioMuteButton = nullptr;
    QWidget* m_audioPoolListContainer = nullptr;
    QVBoxLayout* m_audioPoolListLayout = nullptr;
    QPushButton* m_audioPoolCloseButton = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_goToStartAction = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_stepForwardAction = nullptr;
    QAction* m_stepBackAction = nullptr;
    QAction* m_insertionFollowsPlaybackAction = nullptr;
    QAction* m_selectAllAction = nullptr;
    QAction* m_unselectAllAction = nullptr;
    QAction* m_setNodeStartAction = nullptr;
    QAction* m_setNodeEndAction = nullptr;
    QAction* m_trimNodeAction = nullptr;
    QAction* m_autoPanAction = nullptr;
    QAction* m_toggleNodeNameAction = nullptr;
    QAction* m_showAllNodeNamesAction = nullptr;
    QAction* m_importSoundAction = nullptr;
    QAction* m_showTimelineAction = nullptr;
    QAction* m_timelineClickSeeksAction = nullptr;
    QAction* m_audioPoolAction = nullptr;
    QAction* m_deleteNodeAction = nullptr;
    QAction* m_clearAllAction = nullptr;
    QAction* m_motionTrackingAction = nullptr;
    QAction* m_toggleDebugAction = nullptr;
    QShortcut* m_playPauseShortcut = nullptr;
    QShortcut* m_startShortcut = nullptr;
    QShortcut* m_numpadStartShortcut = nullptr;
    QShortcut* m_stepBackShortcut = nullptr;
    QShortcut* m_stepForwardShortcut = nullptr;
    QShortcut* m_insertionFollowsPlaybackShortcut = nullptr;
    QShortcut* m_selectAllShortcut = nullptr;
    QShortcut* m_nodeStartShortcut = nullptr;
    QShortcut* m_nodeEndShortcut = nullptr;
    QShortcut* m_showTimelineShortcut = nullptr;
    QShortcut* m_trimNodeShortcut = nullptr;
    QShortcut* m_autoPanShortcut = nullptr;
    QShortcut* m_audioPoolShortcut = nullptr;
    QShortcut* m_toggleNodeNameShortcut = nullptr;
    QShortcut* m_deleteShortcut = nullptr;
    QShortcut* m_unselectAllShortcut = nullptr;
    bool m_clearAllShortcutArmed = false;
    bool m_debugVisible = true;
    int m_audioPoolPreferredWidth = 320;
    int m_timelinePreferredHeight = 148;
    QString m_clipName;
    QString m_memoryUsageText;
    QString m_processorUsageText;
    QString m_videoMemoryUsageText;
    QTimer m_clearAllShortcutTimer;
    QTimer m_memoryUsageTimer;
};
