#include "app/MainWindowActions.h"

#include <QAction>
#include <QSignalBlocker>

#include "app/MainWindow.h"
#include "app/PlayerController.h"

MainWindowActions::MainWindowActions(MainWindow& window)
    : m_window(window)
{
}

void MainWindowActions::buildMenus()
{
    m_window.m_newProjectAction = new QAction(QStringLiteral("New Project..."), &m_window);
    m_window.m_newProjectAction->setShortcut(QKeySequence::New);
    m_window.m_openProjectAction = new QAction(QStringLiteral("Open Project..."), &m_window);
    m_window.m_openProjectAction->setShortcut(QKeySequence::Open);
    m_window.m_saveProjectAction = new QAction(QStringLiteral("Save Project"), &m_window);
    m_window.m_saveProjectAction->setShortcut(QKeySequence::Save);
    m_window.m_saveProjectAsAction = new QAction(QStringLiteral("Save Project As..."), &m_window);
    m_window.m_saveProjectAsAction->setShortcut(QKeySequence::SaveAs);
    m_window.m_openAction = new QAction(QStringLiteral("Import Video..."), &m_window);
    m_window.m_openAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_I));
    m_window.m_quitAction = new QAction(QStringLiteral("Quit (Ctrl+Q)"), &m_window);
    m_window.m_quitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    m_window.m_quitAction->setShortcutContext(Qt::ApplicationShortcut);

    m_window.m_goToStartAction = new QAction(QStringLiteral("Jump to Start (Enter)"), &m_window);
    m_window.m_playAction = new QAction(QStringLiteral("Play (Space)"), &m_window);
    m_window.m_stepForwardAction = new QAction(QStringLiteral("Step Forward (.)"), &m_window);
    m_window.m_stepBackAction = new QAction(QStringLiteral("Step Back (,)"), &m_window);
    m_window.m_stepFastForwardAction = new QAction(QStringLiteral("Step Fast Forward (-)"), &m_window);
    m_window.m_stepFastBackAction = new QAction(QStringLiteral("Step Fast Backward (M)"), &m_window);
    m_window.m_insertionFollowsPlaybackAction = new QAction(QStringLiteral("Insertion Follows Playback (N)"), &m_window);
    m_window.m_copyAction = new QAction(QStringLiteral("Copy (Ctrl+C)"), &m_window);
    m_window.m_pasteAction = new QAction(QStringLiteral("Paste (Ctrl+V)"), &m_window);
    m_window.m_cutAction = new QAction(QStringLiteral("Cut (Ctrl+X)"), &m_window);
    m_window.m_undoAction = new QAction(QStringLiteral("Undo (Ctrl+Z)"), &m_window);
    m_window.m_redoAction = new QAction(QStringLiteral("Redo (Ctrl+Y)"), &m_window);
    m_window.m_selectAllAction = new QAction(QStringLiteral("Select All (Ctrl+A)"), &m_window);
    m_window.m_unselectAllAction = new QAction(QStringLiteral("Unselect All (Esc)"), &m_window);
    m_window.m_setNodeStartAction = new QAction(QStringLiteral("Set Start (A)"), &m_window);
    m_window.m_setNodeEndAction = new QAction(QStringLiteral("Set End (S)"), &m_window);
    m_window.m_setLoopStartAction = new QAction(QStringLiteral("Set Loop Start (A)"), &m_window);
    m_window.m_setLoopEndAction = new QAction(QStringLiteral("Set Loop End (S)"), &m_window);
    m_window.m_clearLoopRangeAction = new QAction(QStringLiteral("Clear Loop Range"), &m_window);
    m_window.m_selectNextNodeAction = new QAction(QStringLiteral("Select Next (Tab)"), &m_window);
    m_window.m_moveNodeUpAction = new QAction(QStringLiteral("Move Up (Up)"), &m_window);
    m_window.m_moveNodeDownAction = new QAction(QStringLiteral("Move Down (Down)"), &m_window);
    m_window.m_moveNodeLeftAction = new QAction(QStringLiteral("Move Left (Left)"), &m_window);
    m_window.m_moveNodeRightAction = new QAction(QStringLiteral("Move Right (Right)"), &m_window);
    m_window.m_trimNodeAction = new QAction(QStringLiteral("Trim Node (Shift+T)"), &m_window);
    m_window.m_autoPanAction = new QAction(QStringLiteral("Auto Pan (R)"), &m_window);
    m_window.m_toggleNodeNameAction = new QAction(QStringLiteral("Toggle Node Name (E)"), &m_window);
    m_window.m_showAllNodeNamesAction = new QAction(QStringLiteral("Node Name Always On"), &m_window);
    m_window.m_useProxyVideoAction = new QAction(QStringLiteral("Use Proxy Video"), &m_window);
    m_window.m_importSoundAction = new QAction(QStringLiteral("Import Audio..."), &m_window);
    m_window.m_detachVideoAction = new QAction(QStringLiteral("Detach Video"), &m_window);
    m_window.m_detachTimelineAction = new QAction(QStringLiteral("Detach Timeline"), &m_window);
    m_window.m_detachMixAction = new QAction(QStringLiteral("Detach Mixer"), &m_window);
    m_window.m_detachAudioPoolAction = new QAction(QStringLiteral("Detach Audio Pool"), &m_window);
    m_window.m_showNodeEditorAction = new QAction(QStringLiteral("Show Node Editor"), &m_window);
    m_window.m_showTimelineAction = new QAction(QStringLiteral("Hide Timeline"), &m_window);
    m_window.m_showMixAction = new QAction(QStringLiteral("Show Mixer"), &m_window);
    m_window.m_showTimelineThumbnailsAction = new QAction(QStringLiteral("Hide Thumbnails"), &m_window);
    m_window.m_timelineClickSeeksAction = new QAction(QStringLiteral("Click Seeks Playhead"), &m_window);
    m_window.m_mixSoloModeAction = new QAction(QStringLiteral("Solo Mode: X-OR"), &m_window);
    m_window.m_audioPoolAction = new QAction(QStringLiteral("Show Audio Pool"), &m_window);
    m_window.m_deleteNodeAction = new QAction(QStringLiteral("Delete (Backspace)"), &m_window);
    m_window.m_deleteEmptyNodesAction = new QAction(QStringLiteral("Delete All Empty Nodes"), &m_window);
    m_window.m_clearAllAction = new QAction(QStringLiteral("Clear All (Ctrl+Shift+A, Backspace)"), &m_window);

    m_window.m_motionTrackingAction = new QAction(QStringLiteral("Motion Tracking"), &m_window);
    m_window.m_motionTrackingAction->setCheckable(true);
    m_window.m_autoPanAction->setCheckable(true);
    m_window.m_insertionFollowsPlaybackAction->setCheckable(true);
    m_window.m_insertionFollowsPlaybackAction->setChecked(false);
    m_window.m_showAllNodeNamesAction->setCheckable(true);
    m_window.m_showAllNodeNamesAction->setChecked(true);
    m_window.m_useProxyVideoAction->setCheckable(true);
    m_window.m_useProxyVideoAction->setChecked(false);
    m_window.m_showNodeEditorAction->setCheckable(true);
    m_window.m_showNodeEditorAction->setChecked(false);
    m_window.m_showTimelineAction->setCheckable(true);
    m_window.m_showTimelineAction->setChecked(true);
    m_window.m_showMixAction->setCheckable(true);
    m_window.m_showMixAction->setChecked(false);
    m_window.m_showTimelineThumbnailsAction->setCheckable(true);
    m_window.m_showTimelineThumbnailsAction->setChecked(true);
    m_window.m_timelineClickSeeksAction->setCheckable(true);
    m_window.m_timelineClickSeeksAction->setChecked(true);
    m_window.m_mixSoloModeAction->setCheckable(true);
    m_window.m_mixSoloModeAction->setChecked(false);
    m_window.m_audioPoolAction->setCheckable(true);
    m_window.m_audioPoolAction->setChecked(false);
    m_window.m_showTimelineAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    m_window.m_showMixAction->setShortcut(QKeySequence(QStringLiteral("Ctrl++")));
    m_window.m_audioPoolAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    m_window.m_importSoundAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    m_window.m_toggleDebugAction = new QAction(QStringLiteral("Toggle Debug"), &m_window);
    m_window.m_toggleDebugAction->setCheckable(true);
    m_window.m_toggleDebugAction->setChecked(true);
    m_window.m_showNativeViewportAction = new QAction(QStringLiteral("Native Video Viewport Test"), &m_window);
    m_window.m_showNativeViewportAction->setCheckable(true);
    m_window.m_showNativeViewportAction->setChecked(false);

    m_window.m_setNodeStartAction->setEnabled(false);
    m_window.m_setNodeEndAction->setEnabled(false);
    m_window.m_clearLoopRangeAction->setEnabled(false);
    m_window.m_selectNextNodeAction->setEnabled(false);
    m_window.m_moveNodeUpAction->setEnabled(false);
    m_window.m_moveNodeDownAction->setEnabled(false);
    m_window.m_moveNodeLeftAction->setEnabled(false);
    m_window.m_moveNodeRightAction->setEnabled(false);
    m_window.m_trimNodeAction->setEnabled(false);
    m_window.m_autoPanAction->setEnabled(false);
    m_window.m_toggleNodeNameAction->setEnabled(false);
    m_window.m_importSoundAction->setEnabled(false);
    m_window.m_copyAction->setEnabled(false);
    m_window.m_pasteAction->setEnabled(false);
    m_window.m_cutAction->setEnabled(false);
    m_window.m_undoAction->setEnabled(false);
    m_window.m_redoAction->setEnabled(false);
    m_window.m_deleteNodeAction->setEnabled(false);
    m_window.m_deleteEmptyNodesAction->setEnabled(false);
    m_window.m_selectAllAction->setEnabled(false);
    m_window.m_unselectAllAction->setEnabled(false);
    m_window.m_clearAllAction->setEnabled(false);
    m_window.m_saveProjectAction->setEnabled(false);
    m_window.m_saveProjectAsAction->setEnabled(false);
    m_window.m_showNodeEditorAction->setEnabled(false);

}

void MainWindowActions::updateSelectionState(const bool hasSelection)
{
    m_window.m_unselectAllAction->setEnabled(hasSelection);
    m_window.m_setNodeStartAction->setEnabled(hasSelection);
    m_window.m_setNodeEndAction->setEnabled(hasSelection);
    m_window.m_selectNextNodeAction->setEnabled(hasSelection || m_window.m_controller->hasTracks());
    m_window.m_moveNodeUpAction->setEnabled(hasSelection);
    m_window.m_moveNodeDownAction->setEnabled(hasSelection);
    m_window.m_moveNodeLeftAction->setEnabled(hasSelection);
    m_window.m_moveNodeRightAction->setEnabled(hasSelection);
    m_window.m_trimNodeAction->setEnabled(hasSelection);
    m_window.m_autoPanAction->setEnabled(hasSelection);
    if (m_window.m_autoPanAction)
    {
        const QSignalBlocker blocker{m_window.m_autoPanAction};
        m_window.m_autoPanAction->setChecked(hasSelection && m_window.m_controller->selectedTracksAutoPanEnabled());
    }
    m_window.m_toggleNodeNameAction->setEnabled(hasSelection);
    m_window.m_importSoundAction->setEnabled(hasSelection);
    m_window.m_deleteNodeAction->setEnabled(hasSelection);
    m_window.m_showNodeEditorAction->setEnabled(hasSelection);
    m_window.refreshNodeEditor();
    updateEditActionState();
    m_window.updateDebugText();
}

void MainWindowActions::updateTrackAvailabilityState(const bool hasTracks)
{
    m_window.m_selectAllAction->setEnabled(hasTracks);
    m_window.m_clearAllAction->setEnabled(hasTracks);
    if (m_window.m_showNodeEditorAction && !hasTracks)
    {
        m_window.m_showNodeEditorAction->setEnabled(false);
    }
    if (m_window.m_selectNextNodeAction)
    {
        m_window.m_selectNextNodeAction->setEnabled(hasTracks);
    }
    if (!hasTracks)
    {
        m_window.clearPendingClearAllShortcut();
    }
    updateEditActionState();
    m_window.updateDebugText();
}

void MainWindowActions::updateEditActionState()
{
    if (m_window.m_showNodeEditorAction)
    {
        m_window.m_showNodeEditorAction->setEnabled(m_window.m_controller->hasSelection());
    }
    if (m_window.m_copyAction)
    {
        m_window.m_copyAction->setEnabled(m_window.m_controller->hasSelection());
    }
    if (m_window.m_cutAction)
    {
        m_window.m_cutAction->setEnabled(m_window.m_controller->hasSelection());
    }
    if (m_window.m_deleteEmptyNodesAction)
    {
        m_window.m_deleteEmptyNodesAction->setEnabled(m_window.m_controller->hasEmptyTracks());
    }
    if (m_window.m_pasteAction)
    {
        m_window.m_pasteAction->setEnabled(m_window.m_controller->canPasteTracks());
    }
    if (m_window.m_undoAction)
    {
        m_window.m_undoAction->setEnabled(m_window.m_controller->canUndoTrackEdit());
    }
    if (m_window.m_redoAction)
    {
        m_window.m_redoAction->setEnabled(m_window.m_controller->canRedoTrackEdit());
    }
}
