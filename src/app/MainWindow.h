#pragma once

#include <QAction>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QShortcut>
#include <QTimer>

class PlayerController;
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
    void showStatus(const QString& message);

private:
    void buildMenus();
    void buildUi();
    void refreshTimeline();
    [[nodiscard]] bool shouldApplyNodeShortcutToAll() const;
    void syncMotionTrackingUi(bool enabled);
    void tryOpenLocalDevVideo();
    void armClearAllShortcut();
    void clearPendingClearAllShortcut();

    PlayerController* m_controller = nullptr;
    VideoCanvas* m_canvas = nullptr;
    TimelineView* m_timeline = nullptr;
    QLabel* m_frameLabel = nullptr;
    QLabel* m_debugMenuLabel = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_goToStartAction = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_stepBackAction = nullptr;
    QAction* m_insertionFollowsPlaybackAction = nullptr;
    QAction* m_unselectAllAction = nullptr;
    QAction* m_setNodeStartAction = nullptr;
    QAction* m_setNodeEndAction = nullptr;
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
    QShortcut* m_nodeStartShortcut = nullptr;
    QShortcut* m_nodeEndShortcut = nullptr;
    QShortcut* m_deleteShortcut = nullptr;
    QShortcut* m_unselectAllShortcut = nullptr;
    bool m_clearAllShortcutArmed = false;
    bool m_debugVisible = true;
    QString m_clipName;
    QString m_memoryUsageText;
    QTimer m_clearAllShortcutTimer;
    QTimer m_memoryUsageTimer;
};
