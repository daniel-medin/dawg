#include "app/MainWindowActions.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QShortcut>
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
    m_window.m_loopSoundAction = new QAction(QStringLiteral("Loop Sound"), &m_window);
    m_window.m_toggleNodeNameAction = new QAction(QStringLiteral("Toggle Node Name (E)"), &m_window);
    m_window.m_showAllNodeNamesAction = new QAction(QStringLiteral("Node Name Always On"), &m_window);
    m_window.m_importSoundAction = new QAction(QStringLiteral("Import Audio..."), &m_window);
    m_window.m_detachVideoAction = new QAction(QStringLiteral("Detach Video"), &m_window);
    m_window.m_showClipEditorAction = new QAction(QStringLiteral("Toggle Clip Editor (Ctrl+-)"), &m_window);
    m_window.m_showTimelineAction = new QAction(QStringLiteral("Show Timeline (T)"), &m_window);
    m_window.m_showMixAction = new QAction(QStringLiteral("Toggle Mix Window (Ctrl++)"), &m_window);
    m_window.m_timelineClickSeeksAction = new QAction(QStringLiteral("Click Seeks Playhead"), &m_window);
    m_window.m_audioPoolAction = new QAction(QStringLiteral("Audio Pool (P)"), &m_window);
    m_window.m_deleteNodeAction = new QAction(QStringLiteral("Delete (Backspace)"), &m_window);
    m_window.m_clearAllAction = new QAction(QStringLiteral("Clear All (Ctrl+Shift+A, Backspace)"), &m_window);

    m_window.m_motionTrackingAction = new QAction(QStringLiteral("Motion Tracking"), &m_window);
    m_window.m_motionTrackingAction->setCheckable(true);
    m_window.m_autoPanAction->setCheckable(true);
    m_window.m_loopSoundAction->setCheckable(true);
    m_window.m_insertionFollowsPlaybackAction->setCheckable(true);
    m_window.m_insertionFollowsPlaybackAction->setChecked(false);
    m_window.m_showAllNodeNamesAction->setCheckable(true);
    m_window.m_showAllNodeNamesAction->setChecked(true);
    m_window.m_showClipEditorAction->setCheckable(true);
    m_window.m_showClipEditorAction->setChecked(false);
    m_window.m_showTimelineAction->setCheckable(true);
    m_window.m_showTimelineAction->setChecked(true);
    m_window.m_showMixAction->setCheckable(true);
    m_window.m_showMixAction->setChecked(false);
    m_window.m_timelineClickSeeksAction->setCheckable(true);
    m_window.m_timelineClickSeeksAction->setChecked(true);
    m_window.m_audioPoolAction->setCheckable(true);
    m_window.m_audioPoolAction->setChecked(false);
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
    m_window.m_loopSoundAction->setEnabled(false);
    m_window.m_toggleNodeNameAction->setEnabled(false);
    m_window.m_importSoundAction->setEnabled(false);
    m_window.m_copyAction->setEnabled(false);
    m_window.m_pasteAction->setEnabled(false);
    m_window.m_cutAction->setEnabled(false);
    m_window.m_undoAction->setEnabled(false);
    m_window.m_redoAction->setEnabled(false);
    m_window.m_deleteNodeAction->setEnabled(false);
    m_window.m_selectAllAction->setEnabled(false);
    m_window.m_unselectAllAction->setEnabled(false);
    m_window.m_clearAllAction->setEnabled(false);
    m_window.m_saveProjectAction->setEnabled(false);
    m_window.m_saveProjectAsAction->setEnabled(false);

    auto* fileMenu = m_window.menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(m_window.m_newProjectAction);
    fileMenu->addAction(m_window.m_openProjectAction);
    m_window.m_openRecentMenu = fileMenu->addMenu(QStringLiteral("Open Recent..."));
    QObject::connect(m_window.m_openRecentMenu, &QMenu::aboutToShow, &m_window, &MainWindow::rebuildRecentProjectsMenu);
    fileMenu->addAction(m_window.m_saveProjectAction);
    fileMenu->addAction(m_window.m_saveProjectAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_window.m_openAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_window.m_quitAction);
    m_window.addAction(m_window.m_quitAction);

    auto* editMenu = m_window.menuBar()->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(m_window.m_copyAction);
    editMenu->addAction(m_window.m_pasteAction);
    editMenu->addAction(m_window.m_cutAction);
    editMenu->addAction(m_window.m_undoAction);
    editMenu->addAction(m_window.m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_window.m_insertionFollowsPlaybackAction);
    editMenu->addAction(m_window.m_selectAllAction);
    editMenu->addAction(m_window.m_unselectAllAction);

    auto* nodeMenu = m_window.menuBar()->addMenu(QStringLiteral("&Node"));
    nodeMenu->addAction(m_window.m_selectNextNodeAction);
    nodeMenu->addSeparator();
    nodeMenu->addAction(m_window.m_moveNodeUpAction);
    nodeMenu->addAction(m_window.m_moveNodeDownAction);
    nodeMenu->addAction(m_window.m_moveNodeLeftAction);
    nodeMenu->addAction(m_window.m_moveNodeRightAction);
    nodeMenu->addSeparator();
    nodeMenu->addAction(m_window.m_setNodeStartAction);
    nodeMenu->addAction(m_window.m_setNodeEndAction);
    nodeMenu->addAction(m_window.m_trimNodeAction);
    nodeMenu->addSeparator();
    nodeMenu->addAction(m_window.m_deleteNodeAction);
    nodeMenu->addAction(m_window.m_clearAllAction);

    auto* motionMenu = m_window.menuBar()->addMenu(QStringLiteral("&Motion"));
    motionMenu->addAction(m_window.m_motionTrackingAction);

    auto* audioMenu = m_window.menuBar()->addMenu(QStringLiteral("&Audio"));
    audioMenu->addAction(m_window.m_importSoundAction);
    audioMenu->addAction(m_window.m_loopSoundAction);
    audioMenu->addAction(m_window.m_autoPanAction);

    auto* timelineMenu = m_window.menuBar()->addMenu(QStringLiteral("&Timeline"));
    timelineMenu->addAction(m_window.m_goToStartAction);
    timelineMenu->addAction(m_window.m_playAction);
    timelineMenu->addAction(m_window.m_stepForwardAction);
    timelineMenu->addAction(m_window.m_stepBackAction);
    timelineMenu->addAction(m_window.m_stepFastForwardAction);
    timelineMenu->addAction(m_window.m_stepFastBackAction);
    timelineMenu->addAction(m_window.m_insertionFollowsPlaybackAction);
    timelineMenu->addSeparator();
    timelineMenu->addAction(m_window.m_setLoopStartAction);
    timelineMenu->addAction(m_window.m_setLoopEndAction);
    timelineMenu->addAction(m_window.m_clearLoopRangeAction);
    timelineMenu->addSeparator();
    timelineMenu->addAction(m_window.m_timelineClickSeeksAction);

    auto* viewMenu = m_window.menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(m_window.m_detachVideoAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_window.m_showTimelineAction);
    viewMenu->addAction(m_window.m_showClipEditorAction);
    viewMenu->addAction(m_window.m_showMixAction);
    viewMenu->addAction(m_window.m_audioPoolAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_window.m_toggleNodeNameAction);
    viewMenu->addAction(m_window.m_showAllNodeNamesAction);

    auto* shortcutsMenu = m_window.menuBar()->addMenu(QStringLiteral("&Shortcuts"));
    const auto addShortcutEntry = [](QMenu* menu, const QString& label, const QString& shortcutText)
    {
        menu->addAction(QStringLiteral("%1\t%2").arg(label, shortcutText));
    };
    auto* fileShortcutsMenu = shortcutsMenu->addMenu(QStringLiteral("File"));
    addShortcutEntry(fileShortcutsMenu, QStringLiteral("New Project"), m_window.m_newProjectAction->shortcut().toString(QKeySequence::NativeText));
    addShortcutEntry(fileShortcutsMenu, QStringLiteral("Open Project"), m_window.m_openProjectAction->shortcut().toString(QKeySequence::NativeText));
    addShortcutEntry(fileShortcutsMenu, QStringLiteral("Save Project"), m_window.m_saveProjectAction->shortcut().toString(QKeySequence::NativeText));
    addShortcutEntry(fileShortcutsMenu, QStringLiteral("Save Project As"), m_window.m_saveProjectAsAction->shortcut().toString(QKeySequence::NativeText));
    addShortcutEntry(fileShortcutsMenu, QStringLiteral("Import Video"), m_window.m_openAction->shortcut().toString(QKeySequence::NativeText));
    addShortcutEntry(fileShortcutsMenu, QStringLiteral("Import Audio"), m_window.m_importSoundAction->shortcut().toString(QKeySequence::NativeText));
    addShortcutEntry(fileShortcutsMenu, QStringLiteral("Quit"), m_window.m_quitAction->shortcut().toString(QKeySequence::NativeText));

    auto* playbackShortcutsMenu = shortcutsMenu->addMenu(QStringLiteral("Playback"));
    addShortcutEntry(playbackShortcutsMenu, QStringLiteral("Play / Pause"), m_window.m_playPauseShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(
        playbackShortcutsMenu,
        QStringLiteral("Jump to Start"),
        QStringLiteral("%1 / %2")
            .arg(m_window.m_startShortcut->key().toString(QKeySequence::NativeText))
            .arg(m_window.m_numpadStartShortcut->key().toString(QKeySequence::NativeText)));
    addShortcutEntry(playbackShortcutsMenu, QStringLiteral("Step Back"), m_window.m_stepBackShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(playbackShortcutsMenu, QStringLiteral("Step Forward"), m_window.m_stepForwardShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(playbackShortcutsMenu, QStringLiteral("Step Fast Forward"), m_window.m_stepFastForwardShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(playbackShortcutsMenu, QStringLiteral("Step Fast Backward"), m_window.m_stepFastBackShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(playbackShortcutsMenu, QStringLiteral("Insertion Follows Playback"), m_window.m_insertionFollowsPlaybackShortcut->key().toString(QKeySequence::NativeText));

    auto* editShortcutsMenu = shortcutsMenu->addMenu(QStringLiteral("Edit"));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Copy"), m_window.m_copyShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Paste"), m_window.m_pasteShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Cut"), m_window.m_cutShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Undo"), m_window.m_undoShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Redo"), m_window.m_redoShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Select All Visible Nodes"), m_window.m_selectAllShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Clear Selection"), m_window.m_unselectAllShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Delete Selected Node"), m_window.m_deleteShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Clear All Nodes"), QStringLiteral("Ctrl+Shift+A, then Backspace"));

    auto* nodeShortcutsMenu = shortcutsMenu->addMenu(QStringLiteral("Node And Timeline"));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Set Start / Loop Start"), m_window.m_nodeStartShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Set End / Loop End"), m_window.m_nodeEndShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Select Next Node"), m_window.m_selectNextNodeShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Move Selected Node"), QStringLiteral("Arrow Keys"));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Trim Node To Sound"), m_window.m_trimNodeShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Toggle Auto Pan"), m_window.m_autoPanShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Toggle Node Name"), m_window.m_toggleNodeNameShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Show Timeline"), m_window.m_showTimelineShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Toggle Clip Editor"), m_window.m_showClipEditorShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Toggle Mix Window"), m_window.m_showMixShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Toggle Audio Pool"), m_window.m_audioPoolShortcut->key().toString(QKeySequence::NativeText));

    auto* clipShortcutsMenu = shortcutsMenu->addMenu(QStringLiteral("Clip Editor"));
    addShortcutEntry(clipShortcutsMenu, QStringLiteral("Play / Stop Clip Preview"), QStringLiteral("Space"));
    addShortcutEntry(clipShortcutsMenu, QStringLiteral("Set Clip In"), QStringLiteral("A"));
    addShortcutEntry(clipShortcutsMenu, QStringLiteral("Set Clip Out"), QStringLiteral("S"));
    addShortcutEntry(clipShortcutsMenu, QStringLiteral("Waveform Zoom Horizontal"), QStringLiteral("Mouse Wheel"));
    addShortcutEntry(clipShortcutsMenu, QStringLiteral("Waveform Zoom Vertical"), QStringLiteral("Shift+Mouse Wheel"));

    auto* mouseShortcutsMenu = shortcutsMenu->addMenu(QStringLiteral("Mouse"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Adjust Selected Node Mixer Volume"), QStringLiteral("Ctrl+Mouse Wheel"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Open Node Mixer Volume Fader"), QStringLiteral("Ctrl+Click"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Reset Mixer Or Clip Fader To 0 dB"), QStringLiteral("Ctrl+Click"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Preview Audio Pool Item"), QStringLiteral("Ctrl+Left Hold"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Add Audio Pool Item To Stage Center"), QStringLiteral("Double-Click"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Open Clip Editor For Node"), QStringLiteral("Double-Click Node"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Create Loop Range"), QStringLiteral("Drag In Loop Bar"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Timeline Zoom Horizontal"), QStringLiteral("Mouse Wheel"));

    auto* debugMenu = m_window.menuBar()->addMenu(QStringLiteral("&Debug"));
    debugMenu->addAction(m_window.m_toggleDebugAction);
    debugMenu->addAction(m_window.m_showNativeViewportAction);
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
    m_window.refreshClipEditor();
    updateEditActionState();
    m_window.updateDebugText();
}

void MainWindowActions::updateTrackAvailabilityState(const bool hasTracks)
{
    m_window.m_selectAllAction->setEnabled(hasTracks);
    m_window.m_clearAllAction->setEnabled(hasTracks);
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
    if (m_window.m_loopSoundAction)
    {
        const auto selectedTrackId = m_window.m_controller->selectedTrackId();
        const auto hasSelectedTrackAudio =
            !selectedTrackId.isNull() && m_window.m_controller->trackHasAttachedAudio(selectedTrackId);
        const QSignalBlocker blocker{m_window.m_loopSoundAction};
        m_window.m_loopSoundAction->setEnabled(hasSelectedTrackAudio);
        m_window.m_loopSoundAction->setChecked(
            hasSelectedTrackAudio && m_window.m_controller->selectedTrackLoopEnabled());
    }
    if (m_window.m_copyAction)
    {
        m_window.m_copyAction->setEnabled(m_window.m_controller->hasSelection());
    }
    if (m_window.m_cutAction)
    {
        m_window.m_cutAction->setEnabled(m_window.m_controller->hasSelection());
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
