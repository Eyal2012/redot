# Glass Lab — experimental custom desktop optics

Standalone engineering prototype, October 9, 2026. This is an explicit experimental monitor-capture path requested for Glass Lab. It is not used by PandaClip's capture, recording, audio, replay or main frontend. PandaClip preferences and VS Code configuration remain unchanged. The earlier lab session and deployed folders are preserved.

## Launch

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\build\glass-lab-desktop\Release\PandaClipGlassLab.exe'
```

Keep the complete Release directory, including Qt DLLs/QML/plugins, CRT and license/source archives. No extra recorder or OBS library is used. The normal launch uses **Custom Desktop**, unless this prototype's own saved preset chooses another mode. Its preset is `GlassLabData/approved-material-v1.json` beside this executable. No earlier lab preset is changed.

The Material viewport offers **Custom Desktop / Native Acrylic / Solid**. Custom blur radius (0–24) and edge refraction (0–16) are **physical pixels**, independent of DPI. Desktop surface fill is a neutral dark overlay, not whole-window opacity. Lavender accent changes its tint as well as the control material. These controls are disabled outside Custom mode. Clear/Soft Frost select custom blur 1/10 px and fill 18/24%; Solid forces the fallback. All live values can be saved, including background mode and the application Reduce Transparency preference.

Windows Transparency effects are not a gate for Custom Desktop: it uses its own pixels and shaders. Windows high contrast and the explicit app Reduce Transparency/solid preferences disable it. Native Acrylic retains the existing Windows policy, including inactive-window fallback. No global preference is changed. The Windows settings action is user initiated.

The footer reports source state, age of the captured/displayed frame, exclusion status and failures. Frames older than 500 ms, crop changes awaiting a matching GPU frame, failed affinity, unsupported capture, and GPU-transfer failures show a solid fallback. It does not label an old frame as live. Retries require switching away and back; there is no endless automatic retry loop.

## Actual source and GPU method

1. `IGraphicsCaptureItemInterop::CreateForMonitor` creates a Windows Graphics Capture monitor item. Cursor capture is disabled. The monitor outline may remain; the prototype does not claim borderless consent.
2. Before showing any owned Qt top-level HWND, `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` is applied and checked with `GetWindowDisplayAffinity`. Newly created Qt top levels are covered too. Item-based popovers share the already excluded root HWND. Unrelated HWNDs are never changed. A failed affinity disables Custom mode.
3. A worker owns a separate D3D11 device on the Qt device's adapter, a two-frame WGC pool, and three reusable keyed-mutex shared textures. It drains excess frames and publishes only the latest cropped GPU frame. A small compute pass crops and converts BGRA input to RGBA; no full-monitor CPU readback/upload occurs in presentation.
4. Physical native client coordinates (`ClientToScreen`/`GetClientRect`) map to the monitor origin, including negative origins. A clipped 48-physical-pixel margin supports the maximum blur/refraction footprint. Crop tags prevent presenting a frame from the previous position. A monitor change recreates capture. Spanning multiple monitors deliberately falls back; move the whole window onto one monitor.
5. On Qt's scene-graph thread, `beforeRendering` acquires the shared texture with its keyed mutex, copies to a Qt-device texture inside `beginExternalCommands`/`endExternalCommands`, and releases only after the GPU copy is enqueued. D3D11 mutex synchronization protects reuse. Native GPU commands do not run during `updatePaintNode` synchronization. `QNativeInterface::QSGD3D11Texture::fromNative` imports the caller-owned RGBA texture. One bounded latest-frame slot prevents accumulating work.
6. Two separable 17-tap Qt Quick GPU passes apply custom Gaussian blur. A separate shader displaces samples only in a 30-physical-pixel band around the outer rounded window edge. It leaves the center unmoved. Text/icons/controls render afterward; their opacity is not tied to the material. The old bounded in-app lens is still labeled separately.

This is **live desktop sampling**, not native Acrylic or desktop optical transmission through a transparent shader. The custom shader draws a processed copy of the scene underneath an excluded window. There is no wallpaper, stale screenshot input, internal-image substitution or whole-window opacity. Refraction currently belongs to the **outer rounded window**, not every button or popover.

Capture is throttled to a maximum of approximately 30 deliveries/s. Minimize or mode/accessibility changes stop WGC and release its frames. The Qt source and offscreen blur layers are retired on the render thread. Shutdown stops the worker and releases capture. GPU loss is reported rather than endlessly restarted. The experimental path is SDR BGRA/RGBA; HDR fidelity, cross-adapter monitors, display-mode changes and other Windows/driver combinations are not certified.

## Evidence and what it proves

The source gate first placed an opaque excluded magenta test HWND over a **real separate desktop window** containing stripes, a moving rectangle and a frame counter. WGC monitor frames revealed that window underneath: 56 sampled frames, 55 changing, effective affinity `0x11`, zero magenta pixels and zero black pixels in the fully owned ROI. The same gate passed on the actual shipping Qt HWND. These are pixel/content checks, not just successful API calls. An alternative source was unnecessary because WGC exclusion worked here.

Reports and raw owned ROI screenshots: `build/glass-lab-desktop/evidence/source-gate` and `qt-source-gate`. The exact final shipping Custom-mode recheck is `shipping-custom-final`: 56 frames, 55 changing, affinity `0x11`, zero black/magenta pixels, zero owned activations and orderly exit code 0. The preceding `shipping-custom-source` run passed with 57/56 frames. Only owned background regions are saved. Full-monitor frames stay transient on the GPU; no personal desktop recording or upload feature exists in the shipping lab.

Exclusion hides the lab from ordinary monitor screenshots. **The demonstration and comparison images are asynchronous Qt root-item `grabToImage` results from a focused verification executable using the same shipping QML and GPU path. This is an offscreen rerender of that scene, not an OS/compositor screenshot or swapchain readback.** Exclusion stays enabled throughout. The background is the actual owned desktop HWND captured through WGC, not an injected QML fixture or picture. Native geometry/affinity and the separate source gate provide additional checks; render-output readback alone does not prove final compositor presentation on every PC.

### Focused checks

The full reports live under `build/glass-lab-desktop/evidence/buffered-final-100` and `buffered-final-150`: **21/21 checks passed at each scale**. They cover live monitor import, exclusion, physical grid alignment (99/99 and 250/250 sampled grid pixels), actual desktop blur/refraction pixels, unchanged opaque foreground glyph pixels, moving input, popover/Escape, move/resize, mode switches, controlled invalid-monitor failure/retry, minimize/resume and bounded buffers. After adding the adapter name to diagnostics, the exact deployed executable's shared rendering path was repeated in `deployed-fast-100` and `deployed-fast-150`, **21/21 with process exit 0**. The controls-only suite uses inactive private desktops at 100%/150%, without monitor capture; final reports are `deployed-controls-100/profile/checks.json` and `deployed-controls-150/profile/checks.json`, **43/43 at each scale**, launcher exit 0 and zero owned activations. External pointer movement was observed during private checks; no pointer movement or global input was injected by the harness.

Early failures are retained honestly. The first renderer had correct GPU source pixels but black shader output because the provider was created after its consumer bound; creating the provider on first render-thread lookup fixed it. A DPI property lookup was also corrected. Synchronous window readback stalled the test source/UI and triggered the honest stale fallback, invalidating effect comparisons; asynchronous item readback fixed the measurement. Fully opaque glyph pixels are compared, because antialiased glyph edges naturally blend with changing backgrounds. An early test's `showNormal` restoration activated its owned HWND; later checks use nonactivating native restoration and verify zero owned activations. No personal-window input, physical pointer motion, global shortcut, sound or unrelated session control is used.

Early 150% verification processes also encountered QtCore access violations. Native texture-copy commands were moved from scene-graph synchronization into the supported render-phase command scope, transient stale frames no longer destroy/recreate the shader graph, and an unused native readback hook was removed. The subsequent checks repeat that path; they are not a claim that an uncollected crash stack identified one definitive cause. Scaling uses the effective window DPR, including `QT_SCALE_FACTOR`, rather than the screen DPR. The shorter 150% layout keeps the existing sample/controls above the footer.

`final-render-150` retained a failed center-pixel comparison: its edge-refraction image showed a partially painted GDI fixture (vertical stripes stopped midway and horizontal lines were absent). WGC can observe the live window's incremental drawing. The final owned fixture draws into a memory DC and publishes one completed blit; comparisons use that complete moving scene. The renderer is not allowed to hide malformed source pixels or substitute a picture.

### Performance scope

The report includes a 30-second readback-free Custom sample and 10-second Solid baseline at each scale. The owned moving GDI background shares the test process, so CPU figures include that fixture and diagnostic polling. CPU is expressed as a percentage of **one logical processor**, not total-machine utilization. Working-set changes over this short interval do not prove absence of long-term leaks.

Measured with blur **4 physical px**, edge displacement **8 physical px**, fill **18%**, at a **900 × 650 DIP** window. The scaling check uses `QT_SCALE_FACTOR=1/1.5`; global Windows scaling is unchanged. Machine: Windows 11 Pro build 26200, Ultra 9 285K, Qt 6.8.3/MSVC Release; final diagnostics identify the Qt/capture adapter as **NVIDIA GeForce RTX 5070 Ti**, driver 32.0.16.1088. These are controlled fixture measurements, not gameplay or integration performance.

| Scale | Custom process CPU | Solid process CPU | Delivered frames/s | Crop GPU ms | Qt bracketed interval ms | Working set, start → end | Three crop buffers |
|---|---:|---:|---:|---:|---:|---:|---:|
| 100% | 16.67% | 12.95% | 21.43 | 0.0166 | 0.730 | 186.3 → 178.8 MiB | 8.47 MiB |
| 150% | 14.89% | 7.19% | 21.79 | 0.0254 | 0.689 | 200.3 → 190.4 MiB | 17.67 MiB |

End-of-sample source/display age was **81.6/87.5 ms**. CPU baselines vary and include the fixture's bitmap construction and desktop composition interaction; their difference is not a controlled estimate of material-only CPU. No growing crop allocation was observed during either 30-second sample. `evidence/measurements.json` contains the raw summary, and the original reports retain query counts/means. GPU table values are calculated from the measurement interval's count/sum differences, rather than cumulative means including screenshots.

GPU timestamps measure the crop compute interval and the Qt render interval bracketed by native command synchronization. The Qt interval includes submission gaps and the UI; it is not isolated fragment cost or whole-system GPU utilization, and does not include all DWM/WGC work. Memory counters show three crop buffers only; WGC's two full-monitor buffers, Qt's imported output, blur render targets, driver caches and other Qt allocations add to this. No integration recommendation follows from this controlled test.

### View the demonstration

`build/glass-lab-desktop/evidence/custom-desktop-demo.gif` is a 36-frame, approximately 12-second render-output demonstration over the moving owned desktop window. It retains captured sample timing: first blur/refraction **0/0**, then **20/0**, then **0/16** physical px. This low-rate evidence capture is not the presentation update rate. The separate in-app sample is static and explicitly labeled. Comparison PNGs in `buffered-final-100` and `buffered-final-150` retain zero effects, strong blur, edge refraction, popover, moved/resized, solid and controlled failure states. Foreground text remains sharp; no white framing artifact was visible in the inspected outputs.

## Build / reproduce quietly

```powershell
cmake -S . -B build/glass-lab-desktop -G 'Visual Studio 17 2022' -A x64 -DPANDACLIP_BUILD_GLASS_LAB=ON -DPANDACLIP_GLASS_LAB_CHECKS=ON
cmake --build build/glass-lab-desktop --config Release --target PandaClipGlassLab PandaClipGlassDesktopCheck PandaClipGlassSourceProbe PandaClipGlassLabCheck PandaClipGlassLabLauncher --parallel 4
```

Only standalone Release targets were built. Qt 6.8.3, MSVC and the existing Windows SDK/toolchain are reused. Generated source/readback/fault seams are compiled only into verification targets. Physical-desktop tests must run one at a time so their owned backing windows do not overlap one another. They show nonactivating, input-transparent owned windows; private controls tests never call `SwitchDesktop`. No main recording suites are run.

## Remaining checks

Different PCs/Windows builds, HDR, cross-adapter or multi-monitor spanning, physical dragging/snap, long-term memory/thermal behavior and integration with gameplay remain unverified. Native Acrylic's active transmission still needs its separate visual check. Custom monitor sampling can include black/protected content from other applications; affinity is an OS capture feature, not a security guarantee. Check your actual desktop visually before approving integration. The main app is unchanged.

The observed Windows Transparency preference was enabled; it was never changed. Custom mode's capture/shader policy does not depend on that preference, but actual global-toggle-off behavior remains a manual check. High contrast and app Reduce Transparency deliberately disable Custom mode. For a short visual review: launch, keep the lab fully on one monitor, move a normal window behind it, choose Custom Desktop, compare blur **0/20** and edge strength **0/16**, then open the gear. Confirm moving background shapes and crisp foreground text. This is your own input; the automated checks do not activate your windows.

Official references: [Microsoft CreateForMonitor](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createformonitor), [display affinity](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity), [Qt native D3D11 texture import](https://doc.qt.io/qt-6.8/qnativeinterface-qsgd3d11texture.html), and [Qt native command synchronization](https://doc.qt.io/qt-6.8/qquickwindow.html#beginExternalCommands).
