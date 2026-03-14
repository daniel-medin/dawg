# Native Viewport Handoff

## Goal

Move DAWG toward:

- GPU video presentation
- later GPU overlays
- minimal CPU `QImage` presentation work

without destabilizing the main canvas.

## Current safe state

- The main app canvas currently stays on the stable CPU/Qt presentation path by default.
- The debug viewport remains available behind `Debug > Native Video Viewport Test`.
- `RenderService::presentFrame()` still returns a CPU `QImage` for fallback and non-native consumers.

## Working outcome

The experimental viewport now plays correctly.

What worked:

- `RenderService` / D3D11 device, swap chain, render target, texture upload, draw, and `Present()`
- direct presentation to the `NativeVideoViewport` top-level native HWND

What did not work:

- embedding the native path inside a separate Qt host widget
- presenting into a child native target inside the debug window
- enabling the same child-surface model in the main canvas by default

The black-window failure was not a decode or draw failure. GPU readback confirmed the backbuffer content was correct before final on-screen composition. The problem was the Qt child-HWND handoff.

## Current code shape

### Main canvas

- `src/ui/VideoCanvas.*`
- remains the production path
- `VideoOverlayLayer` keeps interaction, labels, welcome state, and CPU fallback drawing
- native-surface scaffolding exists in code, but it is not the default presentation path
- this is a presentation-path improvement, not full end-to-end GPU decode / tracking / UI acceleration

### Native test viewport

- `src/ui/NativeVideoViewport.*`
- top-level `QWidget` with `WA_NativeWindow`, `WA_PaintOnScreen`, `WA_OpaquePaintEvent`, and `WA_NoSystemBackground`
- `paintEngine()` returns `nullptr`
- presents directly through `RenderService::presentToNativeWindow(this, ...)`
- still receives the same frame and overlay data as the main canvas

### Main window integration

- `src/app/MainWindow.*`
- creates `NativeVideoViewport` itself as the top-level debug window
- shows/hides it from `Debug > Native Video Viewport Test`
- refreshes the viewport on frame updates, clip loads, overlay refreshes, and reopen

### D3D backend

- `src/core/render/D3D11RenderBackend.cpp`
- keeps the real fix for transparent fallback overlay upload
- keeps core init / swapchain / render target / present diagnostics
- temporary video-sample and backbuffer-readback probes have been removed

## Diagnostics that remain useful

Short-path runtime log:

- `C:\dawg-dev\out\Debug\.watch-out.log`

Useful categories:

- `d3d_init_ok`
- `d3d_swapchain_ok`
- `d3d_rtv_ok`
- `d3d_present_ok`
- `d3d_present_fail`
- `native_viewport_fallback`

`d3d_present_ok` is intentionally low-noise now and only marks the first successful present after startup or recovery.

## Baseline note

Current playback-hitch samples from `C:\dawg-dev\out\Debug\.watch-out.log` show:

- queue depth staying full (`queued=8/8`)
- no decoder wait (`waitMs=0`)
- no synchronous fallback (`syncFallback=no`)
- occasional long UI / presentation-side spikes still occurring

That means the sampled hitch behavior is not currently pointing at decoder starvation as the primary bottleneck.

## Next perf task

If performance work resumes, the next serious step should not be another child-HWND experiment inside the main canvas.

Preferred direction:

- keep the stable CPU/Qt main canvas for production until replacement is proven
- treat the working top-level native viewport as the known-good direct-present reference
- design a production presentation path that avoids the failing child-window composition model
- measure before and after with the existing playback-hitch logs instead of assuming wins

## Manual verification checklist

After rebuilding and launching:

1. Confirm the main canvas still shows video during normal playback.
2. Toggle `Fast Playback` and compare playback smoothness and responsiveness on the main canvas.
3. Resize the main window and confirm the canvas continues to render correctly.
4. Open `Debug > Native Video Viewport Test` and confirm it still plays correctly.
5. Load a different clip and confirm both the main canvas and debug viewport update.
6. Start and stop playback and confirm both views remain on the current frame.

## Build / run

Use:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Invoke-DawgDev.ps1 -Configuration Debug -KillRunning -Launch
```

## Guardrail

Do not assume this work makes the whole app GPU-accelerated. The main change is native GPU presentation. Decode, tracking, timeline, and other UI work still need to be evaluated separately. Keep the CPU fallback path until the main canvas native path is proven across normal editing flows.
