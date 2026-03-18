# Quick Shell Migration Log

## 2026-03-18T20:45:00+01:00

- Subsystem: Runtime warning cleanup and final migration notes
- Status: Cleanup pass complete
- Blocker: No shell-architecture blocker remains. The shell migration is complete; remaining work is manual smoke coverage and any future conversion of the small utility QWidget surfaces if desired.
- Attempted fix: Removed the startup `videoViewport/frame/0` image-provider request by clearing the default frame source and only binding the viewport `Image` source when a real frame exists, then refreshed the README and migration notes so they match the Quick-root architecture and current residual widget usage.
- Verification: Rebuilt `build/windows-msvc-current` successfully and relaunched the app after the cleanup pass.
- Next action: Keep the wrapper files deleted, leave `dawg.code-workspace` untracked unless explicitly wanted, and use manual smoke testing for the remaining user-facing polish checks.

## 2026-03-18T21:05:00+01:00

- Subsystem: Track gain popup
- Status: Reached parity
- Blocker: The last normal shell interaction that still used a widget popup was the per-track gain control.
- Attempted fix: Moved the gain popup onto `ShellOverlayController` and `ShellOverlay.qml`, replaced the old `QFrame` and `QSlider` popup in `MainWindow`, and kept the same Ctrl-open, live gain-update, and Ctrl-release dismissal workflow.
- Verification: Rebuilt `build/windows-msvc-current` successfully and launched `build/windows-msvc-current/Debug/dawg.exe` with no startup console output in this session.
- Next action: Treat the track gain popup as part of the Quick shell now; only the native helper/debug surfaces remain intentionally QWidget-based.

## 2026-03-18T20:20:00+01:00

- Subsystem: Final shell collapse and cleanup
- Status: Reached parity for the shell migration target
- Blocker: No shell-architecture blocker remains. Residual polish work is limited to existing runtime warnings in the timeline thumbnail/image-provider path and some small QWidget utility surfaces that are no longer part of the root shell architecture.
- Attempted fix: Replaced the `QMainWindow` root with a frameless `QQuickView`, loaded a single `AppShell.qml` `ApplicationWindow` scene, moved the attached and detached viewport hosting onto Quick-owned windows/items, pinned Qt Quick to `Direct3D11` on Windows, removed the widget-era shell stylesheet, deleted stale compatibility files (`VideoCanvas`, `TimelineView`, `ClipEditorView`, `MixView`, `QuickMixStripWidget`), moved remaining shared view structs into neutral `ClipEditorTypes.h` and `MixTypes.h`, and removed the unused `Qt6::QuickWidgets` dependency.
- Verification: Regenerated and rebuilt `build/windows-msvc-current` successfully, launched `build/windows-msvc-current/Debug/dawg.exe`, observed startup project UI-state restore in the runtime log, and confirmed the runtime line `Qt Quick graphics API: D3D11`.
- Regression pass: Startup restore and shell bring-up were verified from the launched build and runtime log. The session environment did not expose an interactable app window handle back to automation, so attach/detach video, recent-project menu interaction, file-picker flows, context menus, audio drag/drop, and debug-overlay toggles remain user-facing smoke checks rather than automated checks from this session.
- Next action: Treat the migration as complete, then do a polish pass on the existing timeline/image-provider warnings and any remaining widget utility popups you still want converted.

## 2026-03-16T14:50:00+01:00

- Subsystem: VideoCanvas QWidget boundary
- Status: Reached partial parity
- Blocker: The attached/detached video is now Quick-owned directly in MainWindow, but the shell still carries widget tooltip/status scaffolding and child-widget host containers rather than a pure Quick ApplicationWindow.
- Attempted fix: Replaced VideoCanvas (QWidget containing QQuickWidget) with direct QQuickWidget + VideoViewportQuickController in MainWindow, added Q_INVOKABLE methods for QML bridge (requestImportVideo, requestSeedPoint, requestTrackSelected, etc.), moved audio drag/drop handling to MainWindow::eventFilter, updated PanelLayoutController to use the new Quick widget for attach/detach video.
- Next action: Continue removing remaining widget shell scaffolding.

## 2026-03-16T15:45:00+01:00

- Subsystem: QFrame panel wrappers (complete removal)
- Status: Reached partial parity
- Blocker: The shell still depends on QMainWindow widget infrastructure.
- Attempted fix: Completely removed all QFrame panel wrappers (m_canvasPanel, m_timelinePanel, m_clipEditorPanel, m_mixPanel, m_audioPoolPanel). All QQuickWidgets are now direct children of m_mainContent, with geometry managed by ShellLayoutController. Updated all references in MainWindow.cpp, DebugUiController.cpp, and PanelLayoutController.cpp.
- Next action: Delete stale compatibility files, then consider final shell collapse to Quick ApplicationWindow.

## 2026-03-15T23:32:55+01:00

- Subsystem: Foundation
- Status: Started
- Blocker: None
- Attempted fix: Verified local Qt runtime includes `FluentWinUI3` and confirmed the app shell is still widget-heavy.
- Next action: Add migration logging, Fluent bootstrap, Quick title-bar resources, and shell controller foundation.

## 2026-03-15T23:43:01+01:00

- Subsystem: Frameless shell foundation
- Status: Reached partial parity
- Blocker: The current app shell is still anchored in `MainWindow`, `VideoCanvas`, widget dialogs, and widget audio pool layout, so a full single-pass replacement would require invasive runtime and interaction rewrites beyond a safe one-shot conversion.
- Attempted fix: Added `FluentWinUI3` bootstrap, Quick shell resources, `ActionRegistry`, `WindowChromeController`, and a visible Quick frameless title bar hosted by `MainWindow`.
- Next action: Continue replacing widget-owned surfaces and workflows in this order: `VideoCanvas` -> dialogs/project/media prompts -> audio pool -> remove hidden widget menu bar and final widget shell scaffolding.

## 2026-03-16T00:11:36+01:00

- Subsystem: Audio pool
- Status: Reached partial parity
- Blocker: The panel is now Quick-owned, but it is still hosted by `MainWindow` and the surrounding panel layout remains QWidget-based.
- Attempted fix: Replaced the widget-built audio-pool header, video-audio row, and item list with `AudioPoolScene.qml` backed by `AudioPoolQuickController`, while preserving the existing `PlayerController` actions and saved show-length/show-size state.
- Next action: Move on to the remaining shell blockers in this order: widget dialogs/project/media prompts -> `VideoCanvas` replacement -> removal of the remaining widget wrapper surfaces and shell scaffolding.

## 2026-03-16T00:28:41+01:00

- Subsystem: Mixer
- Status: Reached partial parity
- Blocker: The mixer strips now live in a single Quick scene, but the scene is still hosted by `MixView` inside the QWidget shell and the standalone `QuickMixStripWidget` compatibility class still exists in the tree.

## 2026-03-16T01:00:54+01:00

- Subsystem: Dialogs and prompts
- Status: Reached partial parity
- Blocker: Confirmation/message/text-input flows are now Quick-backed, but file and folder picking still route through `QFileDialog`, and the dialog host is still layered over the QWidget shell rather than owned by a Quick `ApplicationWindow`.
- Attempted fix: Added `DialogController` plus `DialogOverlay.qml`, hosted the overlay from `MainWindow`, and migrated the project/media `QMessageBox` and `QInputDialog` call sites to the new Quick modal API.
- Next action: Replace file and folder picking, then move on to the main viewport migration and removal of the remaining widget host boundaries.

## 2026-03-16T01:11:52+01:00

- Subsystem: File and folder picking
- Status: Reached partial parity
- Blocker: Project/media file and folder selection is now Quick-backed, but the picker is still hosted as an overlay inside `MainWindow` rather than owned by a full Quick shell, and it currently implements only the existing open/select-folder workflow rather than a full save dialog/browser stack.
- Attempted fix: Added `FilePickerController` plus `FilePickerOverlay.qml`, replaced the `QFileDialog` path in `MediaImportController`, and routed `MainWindow`/project workflows through the new Quick picker.
- Next action: Move on to the main viewport migration and then collapse the remaining widget-hosted shell boundaries into a real Quick application shell.

## 2026-03-16T10:16:52+01:00

- Subsystem: Main viewport
- Status: Reached partial parity
- Blocker: The attached viewport is now Quick-owned internally, but it is still hosted through the `VideoCanvas` QWidget boundary inside `MainWindow`, the detached-video path still reparents that widget shell, and the standalone native-present debug viewport remains QWidget-only.
- Attempted fix: Replaced `VideoCanvas`'s painted surface/overlay stack with a single Quick scene in `VideoViewportScene.qml`, added `VideoViewportQuickController` plus a Quick image-provider path for frame presentation, and preserved node select/drag/marquee/context-menu/gain-popup/audio-drop behavior through the existing `VideoCanvas` signal contract.
- Next action: Remove the remaining widget viewport shell by folding the attached and detached video windows into Quick-owned surfaces, then continue collapsing `MainWindow` panel layout and wrapper views into a real Quick application shell.

## 2026-03-16T10:16:52+01:00

- Subsystem: Project save/new flow
- Status: Reached partial parity
- Blocker: Project creation and save-as now use the Quick file picker in save mode, but the picker is still an overlay hosted by `MainWindow`, and shell-local widget context menus plus wrapper panels still keep the app from being a true Quick-owned `ApplicationWindow`.
- Attempted fix: Extended `FilePickerController` and `FilePickerOverlay.qml` with save-mode filename handling, then rewired `ProjectWindowController` so `New Project` and `Save Project As` use the Quick picker instead of the old text-input-plus-folder flow.
- Next action: Move the remaining widget popup paths, especially node/timeline context menus and any shell-only widget overlays, onto Quick controllers/popups before attacking the final shell layout rewrite.

## 2026-03-16T10:52:30+01:00

- Subsystem: Quick shell runtime and chrome state
- Status: Reached partial parity
- Blocker: The shell still depends on `MainWindow` and widget splitters/wrappers, but the hosted Quick scenes were also failing to find `QtQuick.Controls`, which blanked the title bar, mixer, audio pool, and dialog overlays and obscured the real migration state.
- Attempted fix: Added shared Quick-engine import-path configuration for every hosted Quick engine, fixed the dialog overlay's broken button bindings, switched the timeline/clip editor back to `QQuickWidget` to avoid native child-window overlap in the mixed shell, and moved the frame/time display into `WindowChromeController` so the Quick title bar no longer depends on a hidden menu-bar label for that shell state.
- Next action: Continue collapsing shell-only widget infrastructure by replacing widget context menus and shell overlays with Quick-owned controllers/popups, then reduce the remaining `MainWindow` splitter/panel scaffolding.

## 2026-03-16T10:58:10+01:00

- Subsystem: Context menus
- Status: Reached partial parity
- Blocker: The app shell still uses `MainWindow` and widget splitters, but the node and loop popup flows no longer need widget `QMenu`; remaining shell widget overlays are status/tips labels and the panel/splitter hierarchy itself.
- Attempted fix: Added `ContextMenuController` plus `ContextMenuOverlay.qml`, hosted it as a shared Quick overlay from `MainWindow`, and rewired the video/timeline node menu plus timeline loop delete menu onto that Quick path. Node rename now reuses the existing Quick text-input dialog instead of embedding a widget `QLineEdit` inside a `QMenu`.
- Next action: Replace the remaining shell-only widget overlays and then start reducing the `MainWindow` splitter/panel hierarchy into a Quick-owned layout scene.

## 2026-03-16T11:20:00+01:00

- Subsystem: Shell overlays
- Status: Reached partial parity
- Blocker: The app still depends on the `MainWindow` splitter/panel hierarchy, but the transient shell overlays no longer need widget `QLabel` surfaces layered over the root and video canvas.
- Attempted fix: Added `ShellOverlayController` plus `ShellOverlay.qml`, replaced the widget status toast and canvas tips label with one shared Quick overlay, and moved overlay positioning onto controller-backed shell state instead of widget `move()/raise()` calls.
- Next action: Remove more `MainWindow` shell scaffolding, starting with widget panel headers/layout chrome and any remaining hidden widget-only shell state.

## 2026-03-16T11:48:00+01:00

- Subsystem: Quick title-bar menus
- Status: Reached partial parity
- Blocker: The title bar itself is Quick-owned, but recent-project refreshes were still hanging off a hidden widget `QMenu`, which kept the hidden menu-bar shell alive for at least one real workflow.
- Attempted fix: Moved recent-project entries into `ActionRegistry` as callback-backed Quick menu items and changed `ProjectWindowController::rebuildRecentProjectsMenu()` to rebuild the Quick menu model instead of mutating a hidden `QMenu`.
- Next action: Continue trimming hidden widget menu-bar dependencies and then attack the larger `MainWindow` splitter/panel layout boundary.

## 2026-03-16T12:04:00+01:00

- Subsystem: Hidden widget menu shell
- Status: Reached partial parity
- Blocker: The visible menu surface is now fully Quick-owned, but the app still kept constructing hidden widget menus just to host actions and recent-project refresh state.
- Attempted fix: Stopped building the hidden widget menu tree in `MainWindowActions`, attached actions directly to `MainWindow` for shortcut ownership, and removed the hidden `QMenu` recent-project path entirely in favor of Quick title-bar menu entries.
- Next action: Attack the larger shell boundary next: replace the widget splitter/panel layout and the remaining `QQuickWidget` view wrappers.

## 2026-03-16T12:42:00+01:00

- Subsystem: Mixer wrapper boundary
- Status: Reached partial parity
- Blocker: The mixer no longer needs the dedicated `MixView` wrapper in the active shell path, but the overall shell still depends on `MainWindow` panel frames/splitters and the timeline/clip editor wrappers remain in place.
- Attempted fix: Moved the mixer host directly into `MainWindow` as `QQuickWidget + MixQuickController`, preserved the existing signal contract to `PlayerController`, and moved the Qt Quick diagnostics state off `MixView` static storage and onto `MainWindow`.
- Next action: Continue collapsing the remaining wrapper boundary layer, starting with the thinner of `ClipEditorView` and `TimelineView`, then come back to the widget splitter/panel shell itself.

## 2026-03-16T12:56:00+01:00

- Subsystem: Clip editor wrapper boundary
- Status: Reached partial parity
- Blocker: The clip editor no longer needs the dedicated `ClipEditorView` wrapper in the active shell path, but the timeline wrapper and the overall widget splitter/panel shell are still in place.
- Attempted fix: Moved the clip editor host directly into `MainWindow` as `QQuickWidget + ClipEditorQuickController`, registered `ClipWaveformQuickItem` from the shell path, and rewired waveform/playhead/gain/loop/attach-audio interactions directly from `MainWindow`.
- Next action: Collapse `TimelineView` next, then return to the larger `MainWindow`/`PanelLayoutController` splitter boundary.

## 2026-03-16T13:05:00+01:00

- Subsystem: Reboot handoff
- Status: Ready to resume
- Blocker: The active shell still has one major wrapper boundary left before the panel-shell rewrite: `src/ui/TimelineView.cpp`. After that, the remaining heavy shell blocker is the widget splitter/panel hierarchy in `src/app/MainWindow.cpp` and `src/app/PanelLayoutController.cpp`.
- Attempted fix: None in this entry; this is a checkpoint note after successfully moving mixer and clip editor hosting directly into `MainWindow`.
- Next action: On next session, start with `TimelineView` removal by hosting `TimelineScene.qml` and `TimelineQuickController` directly from `MainWindow`, preserving the existing signal contract and thumbnail provider path. After that, attack the widget splitter/panel shell.

## 2026-03-16T13:21:34+01:00

- Subsystem: Timeline wrapper boundary
- Status: Reached partial parity
- Blocker: The active shell no longer routes through `src/ui/TimelineView.cpp`, but the app still depends on the widget splitter/panel hierarchy in `src/app/MainWindow.cpp` and `src/app/PanelLayoutController.cpp`, plus `src/ui/VideoCanvas.cpp` as a QWidget host boundary for the viewport. The old `TimelineView` compatibility file still exists in the tree, but it is no longer on the active shell path.
- Attempted fix: Hosted `TimelineScene.qml` and `TimelineQuickController` directly from `MainWindow`, moved the timeline thumbnail image-provider/bootstrap path into the shell, replaced timeline wrapper calls in `DebugUiController` and `PanelLayoutController` with `MainWindow` timeline helpers, and switched shared timeline data includes to `ui/TimelineTypes.h`.
- Next action: Attack the widget splitter/panel shell next in `src/app/MainWindow.cpp` and `src/app/PanelLayoutController.cpp`, then come back to the remaining `VideoCanvas` QWidget boundary.

## 2026-03-16T14:05:00+01:00

- Subsystem: Quick panel shell layout
- Status: Reached partial parity
- Blocker: The old widget `QSplitter` shell is no longer on the active path, but the app still runs inside `MainWindow` with child widget panel hosts, and `src/ui/VideoCanvas.cpp` remains the biggest remaining QWidget boundary, including the detached-video path. Widget tooltip/status scaffolding also still exists in `MainWindow`.
- Attempted fix: Added `ShellLayoutController` plus `ShellLayoutScene.qml`, moved panel arrangement and drag-resize behavior for canvas/timeline/clip editor/mix/audio pool into a Quick-owned layout scene, rewired `PanelLayoutController` to drive Quick layout state instead of `QSplitter`, and kept the existing panel contents alive as hosted child widgets for this pass.
- Next action: Tackle the `VideoCanvas` QWidget host next so the attached and detached video surfaces become Quick-owned too, then delete the remaining widget-only shell scaffolding and stale compatibility files.

## 2026-03-16T12:38:33+01:00

- Subsystem: Startup project UI-state restore
- Status: Reached partial parity
- Blocker: Startup could open with only the canvas visible (panels looked like defaults) even when a project with saved panel visibility was restored.
- Attempted fix: Fixed `MainWindow::syncShellPanelGeometry()` so panel visibility follows the panel's actual requested visibility state during restore, rather than re-deriving visibility from action check states while those actions are still being synchronized.
- Next action: Validate startup restoration by reopening a project with non-default panel visibility, then continue with the remaining `VideoCanvas` QWidget host boundary migration.

## 2026-03-16T12:49:43+01:00

- Subsystem: Startup restore stabilization (Quick shell)
- Status: Reached partial parity
- Blocker: Startup restore state was still getting clobbered during early shell sync/layout passes, causing black/empty startup behavior, panels not showing until repeated toggles, and video attach/detach needed to recover.
- Attempted fix: Promoted shell visibility state to a single source of truth in `ShellLayoutController` during startup flows, removed visibility backflow from `PanelLayoutController::syncMainVerticalPanelSizes()` (which was re-reading transient widget `isVisible()`), switched UI-state snapshot visibility to controller-backed values, added a final `syncShellLayoutViewport()` at the end of project UI-state apply, and marked `ShellLayoutController::setViewportSize()` as `Q_INVOKABLE` so `ShellLayoutScene.qml` can reliably invoke it without runtime TypeError warnings.
- Verification: Built and launched `build/windows-msvc-current/Debug/dawg.exe`; runtime log now shows `Applying UI state ... true` and `Applied UI state ... true` for all restored panels, and the `ShellLayoutScene.qml` `setViewportSize` TypeError warning is gone.
- Next action: User-validate startup behavior with the same project that previously reproduced the issue, then continue migration focus on removing the remaining `VideoCanvas` QWidget boundary (attached/detached video path) and final widget shell scaffolding.

### Resume state

- Current running build in the active repo tree: `build/windows-msvc-current/Debug/dawg.exe`
- Current running PID holding the binary open: `29940`
- Latest build verification this session: rebuilt and relaunched successfully after startup-restore stabilization updates in `PanelLayoutController` and `ShellLayoutController`.
- Already removed from active shell path:
  - mixer wrapper `MixView`
  - clip editor wrapper `ClipEditorView`
  - timeline wrapper `TimelineView`
  - widget `QSplitter` panel shell for the active layout path
  - hidden widget menu tree
  - widget `QMenu` node/loop context menus
  - widget `QMessageBox` / `QInputDialog` prompt path
  - widget `QFileDialog` path for migrated project/media flows
  - VideoCanvas QWidget boundary (replaced with direct QQuickWidget + VideoViewportQuickController)
  - QFrame panel wrappers (canvas, timeline, clip editor, mix, audio pool - now direct QQuickWidget children of m_mainContent)
- Still remaining in active shell path:
  - no root-shell migration blocker remains

### Important runtime notes

- Popup darkening regression was fixed by moving Quick context menu/dialog/file picker hosts off full-window overlay widgets and into popup-sized top-level Quick widgets.
- Outside-click dismissal for the node context menu is already wired in `src/app/MainWindow.cpp`.
- The startup video image-provider warning was removed in the final cleanup pass by avoiding empty `image://videoViewport/...` requests before a frame exists.

## Remaining blockers

- No remaining blocker for the shell migration itself.
- Remaining work is user-facing smoke coverage and optional future conversion of the narrow QWidget utility surfaces that are still intentional.
