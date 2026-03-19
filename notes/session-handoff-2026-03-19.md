# Session Handoff - 2026-03-19

## Current State

- Active repo: `C:\Users\danie\source\repos\daniel-medin\dawg`
- Current runnable build: `build/windows-msvc-current/Debug/dawg.exe`
- Frontend direction: Qt Quick owns the normal shell, panels, menus, overlays, and attached viewport path
- Remaining QWidget usage: narrow helper/debug surfaces only, not the main frontend
- Audio backend: JUCE-only
- Video path: FFmpeg decode with the attached viewport running through the Qt Quick + D3D11 path on Windows

## Latest Completed Work

- Quick shell migration completed and the old embedded QuickWidget shell was removed from the normal app path
- Qt Quick is explicitly pinned to `Direct3D11` on Windows
- Attached viewport playback was moved onto the Quick/D3D11 path and stabilized
- Timeline playback updates were reduced enough to get back to stable `25/25` playback in the tested case
- Node overlay rendering was collapsed into a dedicated painter item instead of per-node QML rebuilds
- Audio now uses a single JUCE backend path; the old MCI fallback was removed
- Mixer strips now switch between mono and stereo meters based on the lane audio
- Master metering now uses real left/right values
- Mixer metering now has:
  - denser segmented ladders
  - fast rise and slower fall
  - `3s` peak hold
  - clip latch
  - clickable meter reset
- Added a top-level `Mixer` menu after `Timeline`
- `Mixer > Solo Mode` is now one dynamic item that flips between `Latch` and `X-OR`
- Solo mode persists with the project
- `X-OR` solo behavior was fixed so the strip UI no longer breaks its binding to controller state when you click solo

## Key Files For Recent Mixer Work

- `src/qml/MixStrip.qml`
- `src/qml/MixScene.qml`
- `src/ui/MixQuickController.h`
- `src/ui/MixQuickController.cpp`
- `src/app/MainWindow.cpp`
- `src/app/ActionRegistry.cpp`
- `src/app/MainWindowActions.cpp`
- `src/app/MixStateStore.h`
- `src/app/MixStateStore.cpp`
- `src/app/PlayerController.h`
- `src/app/PlayerController.cpp`
- `src/core/audio/JuceAudioEngine.cpp`
- `README.md`

## Verification Already Done

- `cmake --build build/windows-msvc-current --config Debug` succeeds
- The app launches from `build/windows-msvc-current/Debug/dawg.exe`
- `Mixer > Solo Mode` appears after `Timeline`
- `Tab` behavior is split correctly between timeline and viewport workflows
- Audio pool press-and-hold preview and row-specific preview indicators are working
- Timeline-open playback was verified back at `25/25` in the tested case after the timeline playback-mode reduction

## Things Still Worth Re-Checking

- `Mixer > Solo Mode: X-OR`
  - solo one strip, then solo another
  - confirm the first strip clears both audibly and visually
- Mixer meter feel
  - confirm the current segment density feels right
  - confirm the release speed feels right on stop
  - confirm master stereo metering looks correct with clearly stereo material
- Timeline playback with heavier node animation
  - verify whether more optimization is still needed there

## Known Notes

- `LNK1168` on rebuild still means `dawg.exe` is open; close or stop it before rebuilding
- The debug overlay's `FPS Output` is the performance number to watch, not the source clip fps
- The README now reflects the current Quick shell / JUCE-only / D3D11 setup and should be kept aligned if the architecture shifts again

## Recommended Next Steps

1. Keep profiling the cases that still drop with heavier animated-node scenes.
2. If mixer polish continues, the next likely area is meter behavior refinement rather than more structural UI work.
3. If the attached viewport path changes again, preserve visible playback first and treat the CPU-image path as the stability fallback.
