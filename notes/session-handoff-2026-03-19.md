# Session Handoff - 2026-03-19

## Current State

- Active repo: `C:\Users\danie\source\repos\daniel-medin\dawg`
- Current runnable build: `build/windows-msvc-current/Debug/dawg.exe`
- Latest successful local rebuild: after the new `Mixer` top-level menu + solo-mode work
- Current running app at handoff time: `dawg.exe` PID `5776`
- Latest committed checkpoint before this uncommitted pass: `19cb138` `Simplify audio backend and add stereo lane meters`

## What Is Uncommitted Right Now

### Mixer Meter Pass

- `src/qml/MixStrip.qml`
  - segmented ladder meters are much denser now
  - `0 dB` text label was removed
  - fader visual `0 dB` position was aligned to the meter `0 dB` line
  - meter ballistics are now handled in QML:
    - fast rise
    - slower fall
    - `3s` peak hold
    - clip latch
    - clickable peak reset
- `src/qml/MixScene.qml`
  - passes playback/reset state into each strip
- `src/ui/MixQuickController.h`
- `src/ui/MixQuickController.cpp`
- `src/app/MainWindow.cpp`
  - added playback/reset plumbing so meters clear/reset correctly on playback restart

### Backend Meter Change

- `src/core/audio/JuceAudioEngine.cpp`
  - meter backend was changed from RMS-ish smoothed measurement to true per-channel peak detection
  - display ballistics are now intentionally in QML instead of the JUCE meter source

### Mixer Solo Mode Menu

- `src/app/ActionRegistry.cpp`
  - added a new top-level `Mixer` menu after `Timeline`
- `src/app/MainWindow.h`
- `src/app/MainWindowActions.cpp`
- `src/app/MainWindow.cpp`
  - added one dynamic action whose text flips between:
    - `Solo Mode: Latch`
    - `Solo Mode: X-OR`
  - menu action is hooked to the real controller state
  - UI refreshes when solo mode changes so the strips/timeline stay in sync

### Solo Mode Behavior + Persistence

- `src/app/MixStateStore.h`
- `src/app/MixStateStore.cpp`
  - added `SoloMode::{Latch, Xor}`
  - in `Xor` mode, soloing one lane clears other soloed lanes
- `src/app/PlayerController.h`
- `src/app/PlayerController.cpp`
  - added `setMixSoloXorMode(bool)` and `isMixSoloXorMode()`
  - emits `mixSoloModeChanged(bool)`
- `src/app/ProjectDocument.h`
- `src/app/ProjectDocument.cpp`
- `src/app/ProjectSessionAdapter.h`
- `src/app/ProjectSessionAdapter.cpp`
  - solo mode now persists with the project via `mixSoloXorMode`

### README

- `README.md`
  - updated to better match the current Quick shell, JUCE-only audio backend, D3D11 viewport path, Audio Pool preview flow, and mixer status

## Files Currently Modified

- `README.md`
- `src/app/ActionRegistry.cpp`
- `src/app/MainWindow.cpp`
- `src/app/MainWindow.h`
- `src/app/MainWindowActions.cpp`
- `src/app/MixStateStore.cpp`
- `src/app/MixStateStore.h`
- `src/app/PlayerController.cpp`
- `src/app/PlayerController.h`
- `src/app/ProjectDocument.cpp`
- `src/app/ProjectDocument.h`
- `src/app/ProjectSessionAdapter.cpp`
- `src/app/ProjectSessionAdapter.h`
- `src/core/audio/JuceAudioEngine.cpp`
- `src/qml/MixScene.qml`
- `src/qml/MixStrip.qml`
- `src/ui/MixQuickController.cpp`
- `src/ui/MixQuickController.h`

## Verification Already Done

- Build:
  - `cmake --build build/windows-msvc-current --config Debug` succeeded after stopping the running app
- Launch:
  - relaunched successfully from `build/windows-msvc-current/Debug/dawg.exe`
- Menu wiring:
  - the new `Mixer` menu compiles and launches
- README:
  - updated but not committed

## Things To Re-Check In The UI

- `Mixer > Solo Mode: ...`
  - confirm it appears right after `Timeline`
  - confirm the label flips correctly
  - confirm `X-OR` keeps solo exclusive
- mixer strip solo buttons
  - confirm switching from `Latch` to `X-OR` updates visible solo state immediately
- master/lane meters
  - confirm the new peak backend feels better with mono/stereo material
  - confirm the denser segments are the right size
  - confirm the `0 dB` alignment still feels right on the fader

## Known Notes

- The linker will fail with `LNK1168` if `dawg.exe` is still open during rebuild. That happened once in this pass and was resolved by stopping the process before rebuilding.
- The README is now closer to the current state, but if more mixer behavior changes land, it may want one more cleanup pass before commit.
- This handoff does not include a new commit yet. Everything in this note is still in the working tree.

## Recommended Next Step

1. Load the other project and sanity-check the new `Mixer` menu plus solo behavior there.
2. If the behavior feels right, commit this pass as one mixer-focused checkpoint.
3. If anything is off, the main files to revisit first are:
   - `src/app/MixStateStore.cpp`
   - `src/app/PlayerController.cpp`
   - `src/app/MainWindow.cpp`
   - `src/qml/MixStrip.qml`
