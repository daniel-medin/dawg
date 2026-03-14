# Native Viewport Handoff

## Goal

Move DAWG toward:

- GPU video presentation
- later GPU overlays
- minimal CPU `QImage` presentation work

without breaking the stable main canvas.

## Current safe state

- The main app video path is still the stable Qt path.
- `VideoCanvas` remains visible and usable in the app.
- The D3D diagnostics remain available.
- Experimental native presentation work is isolated to a separate debug test viewport.

## Current experiment

A separate debug window exists:

- `Debug > Native Video Viewport Test`

This uses:

- `src/ui/NativeVideoViewport.h`
- `src/ui/NativeVideoViewport.cpp`

and is intentionally **not** wired in as the live production canvas.

## Important findings so far

### Confirmed working

- D3D11 device initialization works.
- Swap chain creation works.
- Render target view creation works.
- `Present()` succeeds.

This is visible in `.watch-out.log` / short-path log output via:

- `d3d_init_ok`
- `d3d_swapchain_ok`
- `d3d_rtv_ok`
- `d3d_present_ok`

### Not working yet

- The separate native debug viewport still does not show the expected video output reliably.
- Earlier approaches caused flicker or black output.
- Replacing the live `VideoCanvas` directly was the wrong loop and should not be repeated.

## Current code shape

### Main canvas

- `src/ui/VideoCanvas.*`
- Stable visible Qt video path is preserved.
- D3D probing can be disabled while the native test viewport is open.

### Native test viewport

- `src/ui/NativeVideoViewport.*`
- Separate QWidget host
- Separate native child target for D3D present
- Own `RenderService` by default
- Receives the same presented frame and overlays as the main canvas

### D3D backend diagnostics

- `src/core/render/D3D11RenderBackend.cpp`

Adds logging like:

- `d3d_init_ok`
- `d3d_swapchain_ok`
- `d3d_rtv_ok`
- `d3d_present_ok`
- `d3d_present_fail`

## Very latest checkpoint

The native debug viewport now includes a deliberate visible marker in its overlay rendering:

- a bright `NATIVE TEST` badge
- a bright border with corner markers

Purpose:

- if the badge/border appears but video does not, the problem is likely video texture/content
- if the whole window is still black, the problem is likely broader native-surface composition/presentation

## Build / run

Use:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Invoke-DawgDev.ps1 -Configuration Debug -KillRunning -Launch
```

## Useful log command

```powershell
rg -n "\[d3d_|\[playback_hitch|\[play|\[stop" C:\dawg-dev\out\Debug\.watch-out.log | Select-Object -Last 120
```

## Next recommended step after restart

1. Build and launch.
2. Open `Debug > Native Video Viewport Test`.
3. Check whether the test viewport shows:
   - `NATIVE TEST` + border but no video
   - or pure black
4. Use that result to decide whether to debug:
   - video texture/content path
   - or native surface visibility/composition

## Files currently involved

- `CMakeLists.txt`
- `src/app/MainWindow.cpp`
- `src/app/MainWindow.h`
- `src/ui/VideoCanvas.cpp`
- `src/ui/VideoCanvas.h`
- `src/ui/NativeVideoViewport.cpp`
- `src/ui/NativeVideoViewport.h`
- `src/core/render/D3D11RenderBackend.cpp`

## Guardrail

Do not swap the experimental D3D path back into the main live canvas until the separate debug viewport is visually correct and stable.
