# dawg

![dawg logo](assets/logo.png)

Native desktop foundation for a motion-tracked audio tool.

## First milestone

The initial vertical slice is:

- open a video file
- display frames in a Qt canvas
- click the video to seed a tracking point
- propagate that point forward with OpenCV optical flow while the video plays
- keep the track model ready for later audio attachment

This repository is set up so the next milestone can bind an audio asset to a track and pan or spatialize it from the tracked position.

## Stack

- `Qt 6 Widgets` for the desktop UI and canvas
- `OpenCV` for decoding fallback and optical-flow tracking
- `FFmpeg` wired in at the build level for the later decoder/export path
- `CMake` + `vcpkg` for dependency management on Windows

## Project layout

- `src/app`: main window and playback/controller orchestration
- `src/core/video`: decoder interfaces and the current OpenCV-backed implementation
- `src/core/tracking`: track models and the motion tracker
- `src/ui`: custom video canvas and overlay rendering

## Prerequisites

Install these before building:

- Visual Studio 2022 with Desktop development for C++
- CMake 3.27+
- `vcpkg`

One workable Windows setup path is:

```powershell
winget install Kitware.CMake
winget install Microsoft.VisualStudio.2022.BuildTools
git clone https://github.com/microsoft/vcpkg $env:USERPROFILE\vcpkg
$env:VCPKG_ROOT="$env:USERPROFILE\vcpkg"
& "$env:VCPKG_ROOT\bootstrap-vcpkg.bat"
```

## Build

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
```

## Easiest way to start

If you are new to native C++ setup on Windows, use the launcher in the repo root:

- double-click `Open DAWG.cmd`

What it does:

- checks for Git, CMake, and Visual Studio C++ tools
- clones `vcpkg` into `.tools/vcpkg` on first run
- installs/builds the required libraries through the manifest
- builds the app
- opens `dawg.exe`

Notes:

- the first run can take a long time because `Qt`, `OpenCV`, and `FFmpeg` may need to build
- after the first build, launching is much faster
- if something required is missing, the script stops with a plain-English error

## Current behavior

- `Open Video` loads a clip through `cv::VideoCapture`
- clicking the frame creates a seeded track point on the current frame
- `Play` advances frames and runs Lucas-Kanade optical flow from one frame to the next
- tracked points are painted as overlays

## Limits of this scaffold

- no timeline yet
- no reverse tracking yet
- no persisted project/session format yet
- no audio engine yet
- FFmpeg is prepared at the build level, but the runtime decoder still uses OpenCV for the first milestone

## Next steps

1. Replace the temporary decoder path with an FFmpeg-backed frame cache and seek layer.
2. Add a track inspector with confidence state, retrack, and delete actions.
3. Attach an audio asset to each tracked target and map screen position to panning/spatial placement.
4. Add a timeline with keyframes so a user can start or stop tracking from explicit points in the clip.
