# Scenes Stage 1

Historical initial delivery. The application picker, direct editing controls, current executable and latest checks are documented in [Scenes editing update](scenes-editing.md). Its Advanced Transform dialog supersedes the permanent numeric inspector described below.

## Scope

Scenes is a dedicated page in the existing frontend. Navigation is Capture, Scenes, Media, Settings. Capture retains its arrangement; Media and Settings retain their existing separate windows. Diagnostics stays in the application menu. The checkpoint before this change is `build/checkpoints/scenes-stage1-20261009-230214/source.zip`.

**Scene-based recording is not implemented in Stage 1.** Existing recording, replay, audio, microphone checks and playback continue through Capture. Editing/selecting a scene does not reset an encoder, change recording settings, open an audio endpoint, or redirect output. Opening Scenes hides the native preview; its existing HWND-visibility guard skips presentation while capture/encoding continue independently.

## Use the editor

1. Open Scenes. Create/rename/duplicate/delete scenes using the left panel or New Scene. The last scene cannot be deleted. Up/down buttons reorder scenes.
2. Add a visual source using the right + menu: Game Capture, Window Capture, Display Capture, Video Capture Device, Image or Text. Capture/device types explicitly say Stage 2. Text and Image have genuine local editor previews. Image decoding is asynchronous; invalid/missing files display an error representation.
3. Select by clicking the canvas or Sources list. Drag inside the source; drag a corner handle to resize. Arrow keys move by one scene pixel; Shift+arrow moves by ten. Delete requests confirmation; Escape clears the canvas selection. Sources list Delete also requests confirmation.
4. Use the layer arrows (list is front-to-back), Visible, Lock, Center and Fit. Locked sources remain selectable but cannot be dragged/resized/centered/fitted. Hidden sources remain configurable in the list and are excluded from canvas hit testing.
5. The scrollable transform inspector stores X/Y, width/height, uniform scale, rotation in degrees, four crop margins, and aspect lock. Wheel input over its fields scrolls rather than accidentally changing values. Position/size/crop are in scene pixels, independent of window/DPI. Scale/rotation transform around the top-left origin. Crop clips local content; it does not move the origin. Fit resets rotation/scale/crop and preserves the source rectangle's aspect ratio; Center centers its transformed bounds. Images are fitted inside their instance rectangle without stretching.
6. Bottom Audio Sources assigns Microphone, explicit whole-system Desktop Audio or Application Audio. Gains 0–200%, mute, selected endpoint/target, and future per-scene sync are saved. Device Refresh reuses the bounded background endpoint catalog; missing selected endpoints remain selected and labeled unavailable. **Nothing is opened/monitored here; no live meters are fabricated.** Positive future sync delays and negative advances; Stage 1 does not apply this to Capture.
7. Source properties change the shared definition (name, asset/text, endpoint, target/executable). Reuse shared source adds another instance; duplicate scene gives all its instances/assignments new IDs while retaining definition IDs. Removing instances/scenes preserves definitions for reuse.

Source definitions do not have capture workers in Stage 1. Reusing a source cannot create an extra capture session. Separate endpoint associations are configuration, not a claim of an opened or pinned live endpoint; Stage 2 must resolve the Windows default input (eConsole) once and pin the resolved ID for the active engine session.

## Persistence and architecture

- `src/scene_model.h/.cpp`: QtCore value types, bounded model mutations, versioned JSON validation and coalesced worker persistence. `Model::snapshot()` returns a detached value snapshot containing stable definitions, ordered scenes/instances and selected scene. Stage 2 consumers can resolve one underlying source per definition ID and apply scene instances independently.
- `src/scene_editor.h/.cpp`: Qt Widgets page and nonnative QPainter canvas. Same `WorkspaceMaterial`, root GPU glass layer, design tokens/control stylesheet and custom source-dialog chrome as the existing frontend. Text/images are source content inside the editor canvas, never a UI backdrop.
- `src/frontend_qt.cpp`: central page stack and navigation. Capture dock layout/visibility is retained separately while Scenes is visible; saving workspace layout does not persist the temporarily hidden capture docks. Backend clock/encoder/audio code is unchanged.
- `scenes-v1.json` under `%LOCALAPPDATA%\PandaClip` is separate from recording preferences and workspace geometry. IDs, scene order, source references, transforms, flags, audio assignments and last selected scene persist. Initial scene identity is saved even without an edit.
- One worker reads/writes; pending saves coalesce to one latest snapshot after a 350 ms debounce. Writes use `QSaveFile` with direct-write fallback disabled. Close drains pending scene writes alongside existing backend shutdown. Save failures remain visible in the page.
- Limits: 64 scenes, 256 definitions, 64 visuals/16 audio assignments per scene, 32 KiB properties per source, 4 MiB scene file. Validation rejects invalid IDs/references, nonfinite transforms, empty crops, duplicate audio assignments and non-16:9 canvases. Oversized data reports an error rather than writing an unloadable file.
- Corrupt files load a default in memory without replacing the original. The first explicit edit archives the original to a uniquely named `.corrupt-…json` file before an atomic replacement. Archive failure blocks replacement. Unknown formats/future versions are preserved read-only. No recordings, preferences or evidence are deleted.
- Image work: one bounded decode worker, at most eight cached images, each bounded to 2048 pixels per dimension (128 MiB maximum pixel storage). No file I/O in paint, drag, scroll or layout. Device enumeration occurs only on explicit Refresh, off the UI thread.

## Verification and evidence

Final results on 2026-10-09:

| Check | Result / evidence |
| --- | --- |
| MSVC Debug and Release, shipping app + verification tools | Both built successfully; `build/scenes-stage1/debug-build.log`, `release-build.log` |
| Scene model/editor checks | 64 checks per run, zero failures at 1.0/1.25/1.5 in both configurations: **384 passing checks** across six runs |
| Full Release quiet suite | PASS: `build/quiet-tests/run-20261009-233636-Release` |
| Full Debug quiet suite | PASS: `build/quiet-tests/run-20261009-233809-Debug` |
| Existing regressions | Generated GPU/PCM recording and replay, independent stream decode, retained-frame/no-frame startup, settings scrolling/persistence, audio meters/live mute, PCM continuity/conversion, AAC sync comparison, playback checkpoint corruption/short-write/underrun detection and controlled write failures passed |
| Scenes while existing sessions run | Ordinary recording/encoder remains active; replay flag and retained history remain intact; returning restores exact Capture dock state and preview preference |
| Deployed Release inspection | Actual compositor screenshots: `build/scenes-stage1-tests/deployed-final/scenes-deployed.png`, `scenes-maximized.png`, `scenes-small.png`; **860×620** client verified; five gears/Settings/Media/maximize/restore checks pass; foreground activation observed = **0** |
| Native focus policy | 12 thread activation checks on inactive owned desktop: native applies **1 → 1**, unchanged settings notification still **1**, zero failures; `build/scenes-stage1-tests/focus-policy/focus-policy.txt` |
| Preservation | Audio capture/SRC/PCM/timeline, recording/replay, backdrop/chrome and VS Code task/launch files byte-identical to checkpoint |

Tested Release executable: `C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\build\scenes-stage1\Release\PandaClip.exe`. SHA-256: `A5D71C86FA03EE624D80B5ACCDFA67D399DC01221BBD16F4A6EF7334ECDE6DE9`.

### Exact changed files

Added:

- `src/scene_model.h`, `src/scene_model.cpp`
- `src/scene_editor.h`, `src/scene_editor.cpp`
- `tests/scenes_test.cpp`, `tests/prepare_scene_profile.py`
- `tools/review_scenes.ps1`
- `docs/scenes-stage1.md`

Modified:

- `CMakeLists.txt` — scene sources, existing Qt Concurrent component, verification target.
- `src/frontend_qt.cpp` — page stack/navigation, capture-layout preservation, scene shutdown drain.
- `src/dashboard_icons.h` — matching layers icon.
- `qml/controls.qss` — shared styling for new numeric/text editor controls.
- `tests/qt_frontend_checks.inl` — navigation/editor/session preservation checks; balanced synthetic activation probe.
- `tests/qt_quiet_launcher.cpp` — deployed Scenes/size screenshots and navigation.
- `tests/run_quiet_tests.ps1` — new checks at three Qt scales.
- `README.md` — current delivery, instructions and Stage 1 limitations.

`build/scenes-stage1/source-changes.json` also lists the changes. No recording/replay/audio engine or native appearance helper was edited.

Build/test results are recorded under `build/scenes-stage1` and `build/quiet-tests`. `PandaClipScenesTest` exercises the real model/page on an inactive owned desktop with targeted Qt events; no cursor movement, global input, SwitchDesktop or sound. Scale variants use `QT_SCALE_FACTOR=1`, `1.25`, `1.5`; these are Qt logical scaling tests, not physical Windows display-setting changes.

The deployed Release was inspected through owned-window/compositor screenshots with an owned moving backing window, including Scenes, maximize/restore, five capture gears, Settings and Media. Test windows were nonactivating and mouse-transparent; the passive report recorded zero foreground activation. These screenshots establish actual HWND presentation, not active two-monitor perceptual equivalence. Source capture and recording are excluded from passive physical-desktop checks.

The first full Release regression recorded one Media reduced-appearance assertion failure. The new synthetic activation probe was balanced before leaving Scenes; the isolated full frontend rerun then reported zero failures. This was a verification-harness change, not a Media or native backdrop rewrite. Earlier failed artifacts remain preserved.

Automated regression recording/replay tests use owned generated GPU/PCM fixtures and independently decode MP4 streams. They do not prove actual moving Geometry Dash capture, audible HyperX input, or external-player listening equivalence. The earlier desktop-audio repair remains unchanged; its user listening result is documented in `desktop-audio.md`.

### Manual review

```powershell
& '.\tools\review_scenes.ps1' -Demo
```

This opens separate scene/preferences/output/logs under build, with global shortcuts and audio capture disabled. It does not control existing PandaClip sessions. Demo content is an owned generated image/text plus explicitly labeled Stage 2 definitions.

- Check drag/resize, layer/hide/lock, each management button, audio gains/mute/device association, and restart the printed profile.
- Check small/maximized windows and physical 100/125/150% Windows scaling.
- Click another application on monitor 2 with Scenes/menus open and verify the approved appearance stays stable. No active-desktop clicks were automated.
- For real recording/replay/audio listening, return to Capture and use the existing controls/devices. Verify speech/game audio in saved tracks; scene content must not appear in recordings yet.

## Stage 2 (pending review)

Connect a bounded source registry to real WGC/device inputs; resolve window executable/title and endpoint IDs; add the GPU compositor using these same transforms/crop/order; feed the composed GPU image to the existing preview/NVENC ingress at the existing monotonic clock; route audio assignments through the repaired WASAPI/timeline/mixer; implement safe live scene switching without unnecessary encoder restarts. Verify recorded/replay outputs, not just editor visuals. No Stage 2 compositor/capture/mixing integration is enabled by this delivery.
