# Scenes Stage 1: application selection and visual editing

This update keeps the existing Scenes / canvas / Sources / Audio Sources arrangement and shared glass. **Stage 2 capture/compositing is not enabled.** Scene selection saves an association; Capture continues to record its existing selected window using the unchanged video/audio/replay engines. Editor handles and guides are not sent to any encoder.

Recoverable checkpoint: `build/checkpoints/scenes-editing-20261010-011716/source.zip`. Existing builds, profiles, recordings, evidence and running sessions remain intact. Current delivery: `build/scenes-editing/Release/PandaClip.exe`.

## Select a running application

1. Scenes → Sources **+** → Game Capture or Window Capture.
2. Search by window title or executable name. Each top-level window has its own row, an icon, title and executable name. Select Geometry Dash and click **Add Source**. The source name follows the selected title unless you type your own name.
3. Refresh updates explicitly; automatic refresh runs every two seconds while the picker is visible. Discovery and icon requests run on one background job; repeated refreshes cannot queue more jobs. Search operates on the latest bounded list locally.
4. Properties **…** reopens the same picker. Existing unavailable selections retain their association. A window closing just before Add Source is rejected with an error and refreshed; no capture is opened. An unambiguous restart can match executable path, exact title and window class. Multiple matching windows are reported unavailable/ambiguous rather than selecting another window silently.

Application Audio uses the same picker. This remains future scene audio configuration, separate from Capture's working audio mode. Window titles can change; an exact saved title that no longer exists requires choosing the intended window again. Protected processes whose executable cannot be queried are omitted with a skipped-window count. Untitled, invisible, cloaked, tool/owned windows, shell surfaces, this process and other shipping PandaClip windows are excluded. Minimized eligible windows can remain listed. This does not claim universal discovery of elevated/protected/UWP application identities.

The Windows implementation uses [EnumDesktopWindows](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdesktopwindows), [QueryFullProcessImageNameW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamew), DWM cloaking and [bounded WM_GETICON requests](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessagetimeoutw). Stable metadata is saved in existing source settings: executable path/name, title, class, identity version. Initial rectangle aspect is taken from the client size when available. Transient HWND, PID and process-start time are used only for validation, never stored for reconnect. Stage 2 still needs to resolve these associations and open WGC inputs.

## Edit directly on the canvas

| Operation | Control |
| --- | --- |
| Select | Canvas click or Sources row; frontmost visible source wins |
| Move | Drag inside an unlocked source; arrows move 1 scene pixel, Shift+arrows 10 |
| Resize | Eight corner/edge handles; preserve aspect by default, **Shift** allows free resizing |
| Rotate | Circular handle extending from the top edge; rotation is around the rectangle center; **Shift** snaps to 15° |
| Crop | **Crop mode** button or right-click → Crop mode, then drag handles; **Alt+handle** also crops without a persistent mode |
| Finish crop | Escape exits crop and keeps selection; another Escape clears selection |
| Snap | Drag near canvas edges or center (7 logical pixel tolerance); guides show the selected alignment; **Ctrl** bypasses snapping |
| Align | Right-click: Center horizontally, Center vertically, Fit, Stretch, Reset |
| Precise values | **Advanced Transform…** opens scrollable numeric fields; Apply validates and saves, Close discards unapplied fields |
| Hide / lock / layers | Existing Visible, Lock and layer arrows in Sources; tools scroll at small sizes |

Cropping changes local margins without shifting or stretching surviving pixels. Cropped-away content cannot be hit. Normal resize handles outline the full source rectangle; crop mode outlines the visible crop. Alignment centers visible cropped bounds. Fit clears rotation/scale/crop and fits the source definition's aspect; Stretch clears those transforms and fills the canvas. Reset restores definition dimensions at the origin. Images map to the instance rectangle so deliberate free resize/Stretch actually stretches their pixels. Text uses its saved font size in the transformed rectangle.

All canvas gestures operate in scene pixels through a single letterbox coordinate mapping, independent of preview size and Qt scale. Rotation and corner resizing retain the opposite anchor/center. Locked sources are selectable but reject gestures and alignment. Hidden sources remain in the list but not the preview. Transform edits use the existing coalesced 350 ms background JSON writer; no file writes or application enumeration occur in paint/drag/layout. Precise numeric fields are absent from the permanent interface.

## Implementation boundaries

- `src/application_picker.h/.cpp`: real Windows discovery, bounded worker, live search/refresh, validation and reconnect metadata.
- `src/scene_editor.h/.cpp`: direct canvas editing, guides, context actions, advanced dialog, picker integration and small-size tools scrolling. Existing root material/chrome helpers are reused.
- `src/scene_model.cpp`: visible-bounds centering and native-aspect Fit; persistence/schema remain version 1.
- `CMakeLists.txt`: the picker compiles into shipping and verification targets using the existing Qt Concurrent dependency.
- `tests/scenes_test.cpp`: owned-window discovery/restart, editor gestures, persistence and small-size/scale regressions. Generated verification helpers exist only in this target.
- `tools/review_scenes.ps1`: launches this candidate with an isolated profile. VS Code tasks/launch configuration and native backdrop helpers are unchanged.

## Verification

Automated tests are quiet and use a never-switched owned desktop with process-local Qt events. Real input-desktop discovery is read-only. Physical mouse dragging, Windows display settings, gameplay capture and listening are separate manual checks.

Final verification on 2026-10-10:

| Check | Result / evidence |
| --- | --- |
| MSVC Debug and Release, shipping and verification tools | Both built; `build/scenes-editing/debug-build.log`, `release-build.log` |
| Scene/editor/application picker | **104 checks × 3 Qt scales × 2 configurations = 624 passes**, zero failures; final suite scene reports at 1, 1.25, 1.5 |
| Real Windows application discovery | Read-only input-desktop enumeration: 15 eligible windows; **Geometry Dash found, icon available, foreground unchanged**; `build/scenes-editing/windows-discovery-final.txt` |
| Owned real-window lifecycle | Two distinct windows of one executable; hidden/tool/own windows excluded; title/executable search; Refresh; automatic close/reopen; process restart; stale Add Source rejected; Game and Window dialogs; saved reconnect metadata |
| Editor behavior | Drag, default corner aspect lock, rotated opposite anchor, free resize, center-preserving rotation/15° snap, crop, cropped hit testing, Escape, lock/hide/layers, alignment/edge snapping/Ctrl bypass, Stretch/Fit/Reset, Advanced Apply and exact JSON restoration |
| Full Release quiet suite | PASS: `build/quiet-tests/run-20261010-013217-Release` |
| Full Debug quiet suite | PASS: `build/quiet-tests/run-20261010-013329-Debug` |
| Deployed Release | Actual compositor screenshots in `build/scenes-editing/deployed-final`: `scenes-deployed.png`, `scenes-maximized.png`, `scenes-small.png`; 860×620 client verified; five gears/Settings/Media/caption checks zero failures; **test foreground activation = 0** |
| Preservation | Recording/replay, audio capture/converter/PCM/timeline, frontend adapter, backdrop/chrome helpers, design tokens/stylesheet and VS Code configuration byte-identical to checkpoint; `build/scenes-editing/source-changes.json` |

Release SHA-256: `36A45A75F378954A6BD7E26E0E82BC7E9BE8133D54BDD00E53D0F692EF91BCD3`.

Regression suites cover generated GPU/NVENC recording/replay output and independent stream decoding, retained-frame startup, scene navigation while recording/replay stays active, audio continuity/format conversion and exact lossless checkpoint reconstruction, live mute/meters, playback PCM corruption/short-write/underrun detection, Settings/preferences, five gears and controlled write failures. These are generated/owned fixtures, not actual Geometry Dash capture or audible HyperX testing. The earlier user-confirmed desktop-audio fix is unchanged.

Actual screenshots establish deployed HWND presentation and small-size accessibility, not new active two-monitor Acrylic equivalence. The permanent numeric inspector is gone; the small right pane scrolls rather than compressing buttons. `component-picker.png` and `component-advanced.png` in scene test artifacts are specifically Qt component evidence, not compositor/transparency proof. Source helper windows exist only on an inactive owned desktop; no physical pointer/global input, focus request or sound is used.

Earlier failed artifacts remain available: `check-1` had an old unsnapped drag expectation; `final-check-125` had a stale fixture HWND after the helper process restarted. The tests now use Ctrl for unsnapped movement and resolve the restarted helper's real HWND. Initial compile errors (duplicate local names and ambiguous QPointF initializer overloads) were repaired before successful builds. A deployed small screenshot also exposed compressed action buttons; the final tool viewport fixes that cause and its minimum button-height checks pass.

### Manual review

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\tools\review_scenes.ps1' -Demo
```

This script uses fresh preferences/scenes/output/logs under build; global bindings and audio are disabled. It does not stop or change your existing PandaClip session. Close only the review window when finished.

- Start Geometry Dash yourself; search/select it in Game Capture and check title/icon, Add Source and restart persistence. Try two windows of another application and close/reopen one while the picker is open.
- Drag, corner/edge resize, Shift free resize, rotate/Shift snap, Alt crop/Crop mode, Escape and each context alignment action. Check source list selection, layer order, lock/hide, and Advanced Apply.
- Resize small/maximized and check physical 100%, 125%, 150% Windows scaling; scroll the Sources tool area as needed.
- Return to Capture for an audible recording/replay check. **Scenes content is not expected in the file yet.** Check both game and microphone stems using the existing devices; automated generated PCM does not prove audible HyperX or gameplay capture.

Stage 2 remains pending: capture registry, real source preview/compositor, composed GPU NVENC input, scene-audio routing and live scene switching. No Stage 2 work starts automatically.
