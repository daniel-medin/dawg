# DAWG

<p align="center">
  <img src="assets/logo.png" alt="DAWG logo" width="760">
</p>

<p align="center">
  Video-first sound design for moving things in moving pictures.
</p>

## What DAWG Is

DAWG is a native desktop app for attaching sound to nodes inside the video frame instead of treating audio as a stack of ordinary timeline lanes.

The idea is simple:

- place a node in the picture
- let it follow motion or keyframe it manually
- attach sound to that node
- let picture position drive playback behavior like pan and, later, depth-aware spatial sound

The video is not just reference. It is part of the editing model.

## Current Direction

DAWG is moving toward a video-first editor where:

- nodes live in the frame and on the timeline
- audio belongs to nodes
- playback is tied to what happens in the picture
- the UI stays focused on the film, with utility panels around it

This is not intended to feel like a traditional DAW with lots of horizontal audio tracks first and picture second.

## Current Features

- open a video and play, seek, step, and scrub it
- create, save, load, and reopen `.dawg` projects
- keep imported project media inside project-local `audio/` and `video/` folders
- create and select nodes in the viewer
- show nodes as spans in the timeline
- drag node starts, ends, and full spans in the timeline
- attach audio to nodes and trim a node to the sound length
- auto-pan nodes from left to right screen position
- drag audio from the Audio Pool onto the video to create a node
- use an Audio Pool side panel for imported sounds
- show embedded video audio in the Audio Pool with mute/unmute
- use a floating debug window for runtime stats

## Tech Stack

- `Qt 6 Quick` for the main application shell and interactive panels, with a small amount of `Qt 6 Widgets` still used for utility popups and native helper windows
- `JUCE` for the audio backend
- `FFmpeg` for the main video decode path
- `OpenCV` for motion tracking and some fallback video utilities
- `Direct3D 11` for the Windows Qt Quick scene graph and native render path
- `CMake` + `vcpkg` on Windows

## Project Layout

- `src/app` - app-level orchestration and controller logic
- `src/core/audio` - audio engine, duration probing, video-audio extraction
- `src/core/video` - playback, decoder abstraction, FFmpeg/OpenCV paths
- `src/core/render` - render backend abstractions and D3D11 groundwork
- `src/core/tracking` - node models and motion tracking
- `src/ui` - Quick controllers, custom items, floating windows, and shared UI types

### App Layer Split

The app layer is being split into smaller controllers and services instead of keeping everything in `MainWindow` and `PlayerController`.

- `MainWindow` is now a frameless `QQuickView` root that loads a single `AppShell.qml` scene and exposes the shell controllers to that scene
- the Quick-owned shell now owns the title bar, panel layout, dialogs, file picker, context menus, shell overlays, Audio Pool, timeline, clip editor, mixer, attached video viewport, and detached video window
- `ProjectWindowController`, `MediaImportController`, `PanelLayoutController`, `DebugUiController`, and `MainWindowActions` own major UI/application slices
- `PlayerController` stays the public runtime facade
- `VideoPlaybackCoordinator`, `AudioPlaybackCoordinator`, `TimelineLayoutService`, `TrackEditService`, `SelectionController`, `ClipEditorSession`, `MixStateStore`, `ProjectSessionAdapter`, and `NodeController` now hold most feature-specific logic

Remaining widget usage is intentionally narrow: a few native helper/debug surfaces are still QWidget-based while the main shell runs through Qt Quick.

Startup no longer depends on a debug-only local `.dev` bootstrap path. Project open/save/restore is now the intended startup flow.

## Build

### Requirements

- Windows
- Visual Studio 2022 with C++ desktop tools
- CMake 3.27+
- `vcpkg`

### Standard build

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
```

### Recommended dev loop

The repo includes helper scripts that mirror the source into a short path to avoid Windows path-length pain with Qt, vcpkg, and generated artifacts:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Build-Dawg.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Run-Dawg.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Watch-Dawg.ps1
```

These scripts build in a short-path workspace like `C:\dawg-dev\src` and keep your normal checkout as the source of truth.

## Local Dev Notes

- build folders like `build/`, `b/`, `rel/`, and `rel2/` are ignored
- `.tools/vcpkg/` is ignored as well

That means the repo folder can be very large on disk without GitHub receiving all of that data.

## Performance Logging

DAWG now writes lightweight runtime performance events to `.watch-out.log`.

If you use the helper scripts, the active runtime log is typically written in the short-path build workspace, for example:

- `C:\dawg-dev\out\Debug\.watch-out.log`

That log can contain entries such as:

- `session` for clip/backend startup information
- `seek` and `seek_slow` for timeline jumps
- `playback_hitch` for frame pacing issues during playback
- `stop` for transport stop points

The repo-root `.watch-out.log` may not be the live one if the app is launched through the short-path scripts.

## What Is Actually Pushed To GitHub

Git only pushes tracked files, not every file sitting in the folder.

In this repo, heavy local folders such as:

- `build/`
- `b/`
- `rel/`
- `rel2/`
- `.vs/`
- `.dev/`
- `.tools/vcpkg/`

are ignored, so they should not be committed or pushed.

## Roadmap

- tighten playback and scrub performance further
- deepen the FFmpeg + GPU video path
- tighten the remaining Quick-shell polish work and manual smoke coverage around menus, resizing, and panel interactions
- extend node audio behavior beyond pan into richer spatial control
- move toward depth-aware and z-aware sound behavior

## Status

DAWG is already usable as a rough experimental editor, but it is still under active development and the internal architecture is evolving quickly.
