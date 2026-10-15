# ReDot Scenes preview padding repair — 2026-10-10

## Cause and scope

The user's screenshot shows a 16:9 Geometry Dash image and selection outline inside a larger, nearly black native preview rectangle. The selected saved scene was inspected **read-only**: Scene 3 is 1920×1080; Geometry Dash is a Game Capture definition with saved intrinsic size 2559×1440. Its visual is 1919.25×1080 at x=0.75, y≈0, scale=1, rotation=0 and zero crop, with aspect preservation enabled. This is essentially a fitted scene; it does not explain the large surrounding bars. The subpixel saved position/aspect difference is retained, not silently normalized.

`Canvas::viewport()` fitted the scene inside the full expanding editor widget, reserving 12 DIP at the sides/bottom and 38 DIP above. `Presentation::render()` created an **opaque HWND swapchain covering that entire widget** and cleared it to RGB approximately (4,3,6). Only the fitted scene rectangle received video. The remaining editor space therefore became a black rectangle, including extra aspect-fit gutters. No duplicate layout or source-scale correction is required.

The compositor independently clears its canonical output to black and renders source transforms into it. Its aspect fitting can also produce genuine output padding when scene/output aspects differ. The editor presentation already removes only that canonical output padding when mapping scene coordinates; that logic is preserved. The encoder does not receive the editor swapchain or handles. Thus the broad bars in this screenshot are **editor-only**, not additional pixels encoded into MP4. Genuine empty scene areas, intrinsic source padding and the saved 0.75-pixel offset can still be present in recorded output.

## Correction

- Keep the existing expanding editor layout and native D3D11 presentation. Apply a native widget region containing the scene plus selected outlines, resize/rotation handles and guides. Unused parts of the native child are actually clipped, revealing the editor's existing material; another opaque panel is not placed over them. [Qt documents native widget masking](https://doc.qt.io/qt-6.8/qwidget.html#setMask-1), including its input-region implications.
- Cache the viewport/outline/handle/DPR inputs; recompute the region only when they change. Reapply the physical window region on a DPR change even if the logical region is unchanged. Clip stroked paths to widget bounds before converting them into a region, including extreme rotated/off-canvas transforms.
- Reserve 6 DIP sides/bottom and 34 DIP above: a small editing safety area for the 24-DIP rotation stem and handle hit target. Fit using the actual scene aspect. The scene remains fully visible and undistorted.
- Any tiny native islands needed for handles use the shared neutral `surfaceSolid` token, instead of near-black. The actual scene's pixels remain untouched. Scale GPU handle size with native DPI; all scene/input/guide positions continue through the same DIP-to-physical mapping.
- No scene transform, crop, scene resolution, output setting, Fit/Center algorithm or persistence format is changed. No recording/replay/audio/appearance-policy modification.
- Preserve click-to-deselect in the exposed editor space through the existing parent widget, without intercepting Fit/Center or requiring activation changes.

At the same 1404×828 deployed window size, the owned filling-grid border measured 612×344 pixels before and 630×354 after (~5.9% larger area; integer sample boundaries). The extra aspect-fit space now shows editor material rather than black. A different-shaped viewport still needs unused space to preserve 16:9; it is not filled by stretching or cropping gameplay.

## Evidence and verification

Release candidate: `build/redot-scenes-preview/Release/ReDot.exe`. The previous engineering fallback remains `build/redot-final/Release/ReDot.exe`, original SHA-256 `61bf5a96caebb54fac02242216ba1141fd68b0aabecdb141315422230f2e8726`. Existing personal ReDot processes were not controlled. Source checkpoint: `build/checkpoints/redot-scenes-preview-20261010/source.zip` and manifest.

The deployed shipping executable was inspected with owned static scene fixtures and fresh profiles: filling source, intentionally smaller source, multiple rotated layers, 1280×720, 1920×1080 and 2560×1440 scenes; normal, small and maximized windows; Qt scaling 100%, 125%, 150%. These are actual HWND screenshots, not internal QML grabs. The test never records Geometry Dash or produces audio, moves the personal cursor or requests foreground activation. Background/material colors vary with native compositor conditions; this task does not make a new active-Acrylic or physical two-monitor claim.

Initial screenshot attempts were blocked by the existing overlapping-window safety guard and were recorded as zero. Owned-HWND WGC evidence was explicitly labeled as such, not presented as desktop-transmission proof. Preliminary UIA list-selection attempts changed accessible selection without establishing Qt's current item; they do **not** establish that alternate scenes were displayed. Final evidence uses a separate persisted selected scene in each profile and checks its visible heading/content. Earlier exploratory folders remain preserved.

Actual before and after at the same size, with the same owned image:

![Before — native black editor rectangle](../build/scenes-evidence-before/scenes-deployed.png)
![After — unused area reveals editor material](../build/scenes-evidence-delivery/scenes-deployed.png)

Deliberate scene content is preserved:

![Smaller source — real scene black remains](../build/scenes-evidence-delivery-smaller/scenes-deployed.png)
![Multiple sources — rotated composition retained](../build/scenes-evidence-delivery-multiple/scenes-deployed.png)

### Final results

| Check | Result / evidence |
|---|---|
| MSVC Debug and Release builds | Passed; `debug-build-accepted.log` and `release-build-accepted.log` in the repair checkpoint. Nonfatal test-source warnings remain. |
| Full quiet Debug suite | Passed; `build/quiet-tests/run-20261010-185625-Debug`, 39 independent MP4 decode passes. |
| Full quiet Release suite | Passed; `build/quiet-tests/run-20261010-190021-Release`, 39 independent MP4 decode passes. |
| Scene editor / native region / coordinate mapping | 117 checks, zero failures, at each Qt scale 1.0 / 1.25 / 1.5 in both builds. Includes moving, aspect/free resizing, rotation, crop, locking/hiding, Fit/Center, persistence, off-canvas bounds and editor-space deselection. |
| Scene GPU / NVENC / AAC / recording / replay integration | 110 checks passed in each build. The `metrics.txt` count of 107 is taken before three final metadata/timer checks; the final suite log has the complete count. |
| Synthetic 120 FPS deadline regression | Debug: 362 compositions / 3 seconds, zero late slots. Release: 361 / 3 seconds, zero late slots. Worker cancellation within 100 ms. This is generated composition, not Geometry Dash cadence or game frame times. |
| Deployed final Release screenshots | Normal, small and maximized at Qt 100%, 125%, 150%: `build/scenes-evidence-delivery`, `build/scenes-evidence-delivery-125-final`, `build/scenes-evidence-delivery-150`. Each reported three successful guarded desktop-compositor screenshots and zero launcher failures. |
| Final Release smaller / multiple source screenshots | `build/scenes-evidence-delivery-smaller`, `build/scenes-evidence-delivery-multiple`; three screenshots and zero failures each. Visible content and headings were inspected. |
| Other scene resolutions | Final Release 1280×720 and 2560×1440 profiles checked in `build/scenes-evidence-delivery-720p`, `build/scenes-evidence-delivery-1440p`; three guarded screenshots and zero failures each. Earlier exploratory evidence remains preserved. |
| Preserved functionality | Quiet suites passed audio PCM continuity, replay bounds/export failures, Media/player controls, settings/scenes persistence, branding/taskbar and tray lifecycle checks. These do not establish physical Explorer tray interaction or audible quality. |
| Actual new GD capture / recordings / physical display scaling | Not Verified. No personal capture, input, foreground activation or audio stimulus was used. |

Final shipping Release SHA-256: `47e0aefaa2ae0b6a19699ff9b37c64aacf7be34d57979835e3ca03576b7f609b`. Debug: `3c1b9ec165e92aa1f5f268e60e366056c9af301b41882a805dc292f194fd5f2c`.

The final checks did not repeat the engineering stage's ten-minute performance run, physical microphone listening or live game benchmark. No new leak-free, gameplay-performance or hardware-recovery claim is made. Focused screenshots use owned static scenes; interaction tests use process-local input on an inactive owned desktop. The physical desktop appearance policy remains unchanged.

### Test boundaries

- Source movement, aspect/free resize, rotation/snapping, interactive crop, guides, hiding/locking, Fit/Center, source-list synchronization and persistence are verified by process-local Qt input on an inactive owned desktop. Native mask and HWND region tests check physical viewport mapping, rotation-handle coverage, resizing and extreme off-canvas outlines at all three Qt scales. These are not physical cursor/drag/snap tests.
- GPU/decoded MP4 integration verifies layer order, aspect fit, crop, rotation, scene switching and encoder view reuse. The new single-smaller-source recording/replay test checks black empty scene pixels and exact source position in **both** saved files, ensuring editor material/handles do not enter them.
- The preserved 120 FPS scheduling check measures composition, not CFR repeat counts; its results are included below. Audio continuity, compressed replay accounting, taskbar/tray lifecycle and failure tests remain in the full quiet suite.
- Geometry Dash is discoverable on the physical desktop; the read-only enumeration retained foreground focus. Its actual new capture/editor/recording was **not** exercised against the user's running session. A fresh user-operated GD spot check and physical Windows display-scale changes remain pending. Existing user-approved gameplay/audio/focus behavior is historical acceptance, not new verification.

## Modified files

Production: `src/scene_editor.h`, `src/scene_editor.cpp` (viewport and cached native clipping), `src/scene_runtime.cpp` (**Presentation only**, shared gutter color/native-DPI handles; compositor worker unchanged).

Tests: `tests/scenes_test.cpp` (native-region and mapping regressions), `tests/scene_integration.cpp` (ordinary/replay smaller-source pixel validation), `tests/qt_quiet_launcher.cpp` (focused deployed scene screenshots with honest evidence methods), `tests/prepare_scene_preview_profile.py` (owned fixture profiles). Documentation: `README.md`, this report.

The recording/replay/audio implementations, private compositor timer, buffer SRVs/leases, Settings, Acrylic policy, taskbar/tray, scene model, Media and VS Code configuration are unchanged. The previous engineering Release is preserved rather than rebuilt.

## Launch and short physical check

Finish current saves and close the older app normally yourself before opening this candidate with your usual profile:

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\build\redot-scenes-preview\Release\ReDot.exe'
```

Open Scenes → Scene 3, select Geometry Dash and confirm that only actual scene content is black, while unused editor space shows the existing material. Drag/resize/crop/rotate a disposable duplicated scene, use Fit/Center, resize/maximize the app, then save a short recording and replay. Neither editor handles nor the surrounding material should appear in those files. Restore the duplicate or remove it yourself when finished; this repair does not edit the original scene.

Rebuild and quiet verification:

```powershell
cmake -S . -B build/redot-scenes-preview -G 'Visual Studio 17 2022' -A x64 -DPANDACLIP_BUILD_REPLAY_STRESS=ON -DPANDACLIP_BUILD_GLASS_LAB=OFF
cmake --build build/redot-scenes-preview --config Debug --parallel
cmake --build build/redot-scenes-preview --config Release --parallel
& tests/run_quiet_tests.ps1 -BuildDirectory build/redot-scenes-preview -Configuration Debug -SkipBuild
& tests/run_quiet_tests.ps1 -BuildDirectory build/redot-scenes-preview -Configuration Release -SkipBuild
```
