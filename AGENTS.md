# DAWG Agent Notes

## Build And Run

- Use this repo as the source of truth: `C:\Users\danie\source\repos\daniel-medin\dawg`
- The current in-repo build output is:
  - `build/windows-msvc-current/Debug/dawg.exe`
- Helper scripts should target the in-repo build tree by default, not a mirrored `C:\dawg-dev` workspace.

## Project Startup

- DAWG is now project-first.
- Startup should rely on:
  - last opened `.dawg` project restore
  - normal `New Project`, `Open Project`, `Save Project`, `Save Project As`
- Do not reintroduce debug-only local bootstrap behavior such as scanning a `.dev` folder for video/audio on startup.

## Architecture

### Main UI

- `MainWindow` is the widget shell and event host.
- Feature-specific UI/application slices are split into:
  - `ProjectWindowController`
  - `MediaImportController`
  - `PanelLayoutController`
  - `DebugUiController`
  - `MainWindowActions`

### Runtime

- `PlayerController` is still the public runtime facade, but much of its logic has been moved into:
  - `VideoPlaybackCoordinator`
  - `AudioPlaybackCoordinator`
  - `TimelineLayoutService`
  - `TrackEditService`
  - `SelectionController`
  - `ClipEditorSession`
  - `MixStateStore`
  - `ProjectSessionAdapter`
  - `NodeController`

### Nodes

- Node workflow logic now lives primarily in `NodeController`.
- Keep low-level track storage and motion tracking in `MotionTracker`.
- Do not fold future node-edit workflow back into `MainWindow`.
- Motion-tracking-specific workflow has intentionally not been moved into `NodeController` yet.

## Media And Projects

- Imported media should live inside the project folder.
- Use project-local `audio/` and `video/` copies, not external absolute paths where avoidable.
- Project UI state, panel state, and detached video state are intended to persist with the project.

## Performance Work

- Native/top-level video presentation experiments exist, but the attached child-window path has been fragile.
- Be careful changing the main attached playback path; preserve visible playback first.
- For current playback/debug checks, use the in-app debug overlay and `FPS Output`, not just source `Video FPS`.

## Documentation

- If architecture or workflow changes materially, update `README.md`.
- Keep docs aligned with the current project-first flow and the controller split.
