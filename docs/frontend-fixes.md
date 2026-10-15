# Frontend controls, audio meters and glass chrome

October 6, 2026. Workspace verified as `C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip`. Changes and generated artifacts stay inside PandaClip. No personal PandaClip/Geometry Dash window was controlled or closed. The previous Release executable was locked by a running process, so this delivery uses **`build\frontend-fixes\Release\PandaClip.exe`**. VS Code task names/F5 are retained and point to this build.

## Confirmed problems and fixes

1. **Source errors were hidden.** The Qt status chose capture text and recognized recording errors through a small set of keywords. “Choose an open window first” was not one of them. Recording already checked the target before consent; replay checked consent first. Replay could consequently wait on permission with no usable source. Both paths now validate first, display an actionable message, and the visible Qt action opens Capture properties when nothing is selected. Qt source changes reach the controller's actual window handle. The signal connections existed; they were not missing.
2. **Pending consent was unbounded.** A pending start disabled native compatibility controls and their Qt counterparts without a timeout. Stopping a pending start did not cancel the outstanding operation. There is now one pending action, explicit Cancel Start, a generation guard against stale async completion, cancellation, and a 30-second consent limit (serviced by the 500 ms health poll). Every terminal result releases the controls. Allowed permits requesting no border; denial, cancellation, unsupported API, errors and timeout allow ordinary capture with the Windows outline. An explicit Capture-settings request can retry. Neither Allowed nor a property setter proves the visible outline disappeared.
3. **Quiet microphone input was rounded to zero.** WASAPI calculated RMS, truncated it to an integer in 0–1000, then the frontend read a hidden progress control refreshed by the 500 ms health poll. Captured mic RMS values around `0.00009–0.00025` all became zero. The old scale also missed bursts between polls. Qt now reads a coherent bounded snapshot of floating-point stereo RMS and sample peak directly from the capture worker. A 300 ms monotonic release envelope retains real bursts. RMS includes both channels without summing opposite-phase signals. Bars span −90 to 0 dBFS; a separate peak marker reflects actual samples. Gain/mute affect recorded audio, while meters describe raw input. Source labels explicitly distinguish disabled, inactive, unavailable/opening, open/silent and signal. These values do not establish audible speech.
4. **Chrome/material lifecycle needed native verification.** Main/Media used system captions; initial custom-caption tests found Settings/Diagnostics still had native caption insets despite widget screenshots hiding them. Chrome now follows the current Qt platform window and installs a native window subclass so non-client sizing/hit tests cover dialogs too. It retains Windows system styles, work-area maximization, edge/corner resize, caption dragging/double-click/system commands and `HTMAXBUTTON` for snap layouts. Buttons have accessible names, keyboard focus, hover/pressed styling and red Close hover. Main Close still uses tray/drain behavior; Media Close affects playback only.

The attachment directory contained request text but no screenshot image. The reported screenshot could not be compared directly, and a genuinely pending Windows prompt on the user's active desktop was not reproduced. Code defects and owned frontend states above were investigated independently.

## Materials and limits

The main recording workspace remains Qt Widgets with real docks and native D3D11 preview. Caption, dock headers and primary actions sit above a **single stable Qt Quick GPU layer per workspace/dialog**. Its local underlay contains the selected source/session information. Each small region samples only that information surface at half resolution, using the existing blur/refraction/rim/shadow shader. It is real local GPU sampling, not a gradient substituted for glass, a screenshot of the desktop, or a blurred live preview. Source/session/layout changes update the bounded samples; static material stops sampling after its settling update. Technical counters remain in Diagnostics.

An early design moved individual Quick widgets with dock headers and crashed in Qt's GPU resource lifecycle when a dock floated. It was replaced by the stable window layer. Detached docks use a readable **solid header**; moving GPU material between top-level dock render interfaces is avoided. View Media continues sampling its own actual decoded video/thumbnails. Reduced transparency disables sampling/blur and selects solid surfaces; there is no continuous decorative animation. Recorded pixels and the native preview stay sharp.

## Verification scope

Quiet automation uses an owned inactive desktop without switching it, input injection, foreground activation, audible playback or personal session control. It drives the same Qt source dropdown, recording/replay/save/check-mic buttons, source toggles, sliders and window buttons used by the shipping frontend.

For recording-start checks, the **capture availability/GPU-frame boundary** is replaced only in `PandaClipQuietApp` by an owned generated GPU source. Source validation, consent waiting/completion/cancellation, controller commands, native worker messages, direct NVENC, WASAPI, AAC, replay muxing and frontend state remain the production paths. Earlier tests started the encoder directly through a fixture helper, so they did not exercise this startup path; these checks do. Shipping has no generated-source seam. Simulated consent results do not prove real Windows consent or WGC access.

Check Mic opens the explicitly selected Realtek endpoint with the actual WASAPI converter, without an encoder or speaker monitoring. Recording tests also open actual process-loopback for the owned launcher's process tree and the named microphone. The controlled source has no real gameplay audio. Noise/quiet decoded media/process samples do not establish game-audio correctness or speech intelligibility.

### Checks

- No-source Start Recording and Enable Replay: actionable visible error, Capture selector, no encoder/pending permission.
- Qt selected source reaches the controller. Recording and replay start without a separate Start Capture action; recording continues with preview hidden.
- Pending consent blocks duplicate actions. Qt Cancel Start clears the request. Simulated allowed, denied, failed, cancelled, unsupported and timeout outcomes release controls; simulated denial/failure startup paths continue to direct NVENC with the generated capture boundary. The timeout test injects the terminal result; it does not measure an actual 30-second Windows permission wait.
- Check Mic retains its selected endpoint, opens no encoder, produces real captured-sample statistics, and delivers the exact floating-point snapshot to the Qt meter. Deterministic meter tests separately cover stereo/channel math, quiet signals, peak retention, decay, empty input and concurrent bounded snapshots.
- Both enabled, game-only and microphone-only frontend sessions. Ordinary and replay exports finalize; recording/replay share one encoder output; Media browsing/Close leaves replay running.
- Saved H.264/AAC files independently decoded with the verifier. 48 kHz stereo AAC, Mix plus enabled stems, monotonic/continuous AAC timestamps and fixed-rate video are checked. Separate deterministic fixtures retain video-only/all-source-combination coverage. These short checks do not establish perceptual sync or ten-minute stability.
- Native client/window geometry confirms captions are removed for Main, Settings, Diagnostics and Media. Native corner/caption/maximize hit tests, custom minimize/maximize/restore, monitor work-area bounds, resize, actual dock floating/hiding/restoration, settings wheel/keyboard navigation and two-process layout persistence are checked.
- SDK-free shipping smoke uses Windows-only PATH with Qt/QML search overrides removed, sees the visible Qt workspace and exits cleanly. Dynamic Qt Quick Widgets, other DLLs/plugins/QML and corresponding license/source folders are deployed alongside the executable.

Development failures remain under `build\quiet-tests`; failed artifacts were not deleted. A Debug scrolling assertion caught an earlier asynchronous appearance save completing during its file-timestamp sample. Verification now waits, with its existing phase deadline, for the prior debounce/worker save to settle before measuring scrolling. Production settings remain asynchronous; no blocking flush was added.

## Final artifacts and measured results

The final source built successfully with MSVC in both configurations. The tested Release executable SHA-256 is `C3232D89ADBF03F2B5F5B025FAE4EEEF63938BBAA6E47B014B758474568E9DCE`. Build logs are `build/frontend-fixes-build-release.txt` and `build/frontend-fixes-build-debug.txt`. Qt shader dependency warnings involving the spaced workspace path and the existing `menu` shadow warning remain; compilation and linking succeeded.

| Run | Evidence | Result |
| --- | --- | --- |
| Release full quiet suite | `build/quiet-tests/run-20261006-211950-Release`, `build/frontend-fixes-verified-release.txt` | PASS; 198 frontend assertions, 0 failures, 96 DPI |
| Debug full quiet suite | `build/quiet-tests/run-20261006-211950-Debug`, `build/frontend-fixes-verified-debug.txt` | PASS; 0 frontend failures, 96 DPI |
| Release process-local 150% scaling | `build/quiet-tests/frontend-fixes-scale150-verified`, `build/frontend-fixes-scale150-verified.txt` | PASS; 0 failures, 144 DPI |
| Second-process layout restore | Release and Debug `ui-generated/restore-result.txt` | 0 failures |
| SDK-free deployed shipping executable | Release and Debug `ui-shipping/shipping-smoke.txt` | Visible workspace; clean exit; 0 failures |

The Release transcript contains **21 successful independent decode passes**: eight component fixture files, twelve files in the frontend output directory (eight copied library fixtures plus four newly finalized frontend recordings/exports), and the AAC-delay probe. Fixtures use 1280×720 at 30 FPS to keep verification bounded; the ordinary/replay component pairs contain 180/120 decoded video frames over 6/4 seconds. All enabled AAC tracks decode as 48 kHz stereo; both-source files retain Mix, Game and Microphone, single-source files retain Mix plus that stem, and video-only files have no audio. This is short-session decoding evidence, not proof of audible speech, real game acquisition, long-run synchronization or gameplay performance.

Actual Check Mic captured 215 WASAPI packets in the Release frontend check. One coherent snapshot had 480 sample frames, summed squares `6.603615193e-06`, RMS `8.293832798e-05` and peak `0.0002805033`. The displayed RMS envelope was `0.0002019993844`, retaining a previous real burst as designed. The meter assertion compares the exact applied snapshot with the Qt meter; it does not compare unsynchronized reads of a changing worker. During recording, the actual process-loopback/mic workers delivered 377/372 packets. Windows reported the selected Realtek input at 72% volume. Quiet/noise-level signal is not identified as speech.

Native checks measured **client origin delta 0,0** and client dimensions equal to window dimensions for Main, Settings, Media and Diagnostics at both tested scales. Thus no duplicate native caption is hidden above the custom title bar. Physical snap flyout, mouse dragging and monitor transitions remain manual.

### Bounded local timing samples

These are Release frontend handler timings from the same owned run, not before/after gameplay measurements. Debug and Release verification overlapped and background PC load was uncontrolled. Wheel timings exclude the separate health poll, capture/encoder work and display latency.

| State | Wheel handler median / p95 (ms) |
| --- | --- |
| Idle | 0.0037 / 0.0083 |
| Recording | 0.0211 / 0.1050 |
| Replay | 0.0051 / 0.0625 |
| Recording + replay | 0.0041 / 0.0160 |
| Media playback, glass | 0.0036 / 0.0066 |
| Media playback, solid | 0.0042 / 0.0358 |

The lightweight UI snapshot tick averaged 1.846 ms, maximum 8.639 ms over 205 samples. The Media render probe observed glass GPU median/p95 0.2363/1.9238 ms and solid 0.1764/0.6724 ms; respective CPU medians were 0.1753/0.1564 ms. These four-second decoded-media samples include rendering work and illustrate that glass has a cost. They do not isolate the main material layer or establish Geometry Dash frame-time overhead. No preview or desktop CPU-copy blur was added.

### Inspected screenshots

These are actual final Release frontend captures on the owned desktop, not visual mockups. The preview is the clearly labelled generated GPU fixture; screenshot readback is verification-only. The recording screenshot uses the bounded 720p/30 test profile, not shipping defaults. Settings and Diagnostics remain solid/readable with the same custom caption controls.

| Window/state | Screenshot |
| --- | --- |
| Workspace, actual pre-recording mic samples | [Mic check](frontend-fixes-screenshots/workspace-mic-check.png) |
| Workspace, recording | [Recording](frontend-fixes-screenshots/workspace-recording.png) |
| View Media, glass over decoded thumbnails | [Media](frontend-fixes-screenshots/media-glass.png) |
| Playback glass over actual decoded video | [Playback](frontend-fixes-screenshots/playback.png) |
| Bright video/material update | [Bright frame](frontend-fixes-screenshots/playback-bright.png) |
| Reduced transparency | [Solid media](frontend-fixes-screenshots/media-solid.png) |
| Settings | [Settings](frontend-fixes-screenshots/settings.png) |
| Diagnostics | [Diagnostics](frontend-fixes-screenshots/diagnostics.png) |
| Dock floating | [Floating dock](frontend-fixes-screenshots/workspace-floated.png) |
| 150% process-local scaling | [Workspace](frontend-fixes-screenshots/workspace-150.png), [Settings](frontend-fixes-screenshots/settings-150.png) |

## Manual checklist

- Select the real Geometry Dash window; start recording/replay directly with preview visible and disabled. Check actual moving gameplay and decoded recording smoothness.
- Answer real Windows borderless consent, inspect Capture properties and the actual yellow outline. If denied on this unpackaged EXE, use the documented MSIX package identity plus `graphicsCaptureWithoutBorder` capability. Outline removal is not claimed by these tests.
- Select your intended mic, click Check Mic, speak, and confirm the actual opened name. Check Windows input permission/mute/volume, PandaClip recording gain/mute, and listen to Mix and each enabled stem. Physically unplug/reconnect the mic. Quiet sample activity is not a listening check.
- Test physical title dragging/double-click, edge resize, snap flyout/snapping, Alt+Space, keyboard caption controls and movement between monitors with different DPI. Process-local 150% scaling is distinct from a physical transition.
- Confirm main Close obeys your tray/exit preference and active sessions finalize, and Media Close leaves recording running.

## Launch

Keep all runtime files next to the executable. From any PowerShell directory:

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\build\frontend-fixes\Release\PandaClip.exe'
```

Select the game in Source Properties > Capture, configure Audio, use Check Mic, then Start Recording or Enable Replay. Preview need not be started separately. Wait for history before Save Clip and for successful finalization before trusting Saved. The older running app does not update in place; exit it yourself when ready, especially to release its registered hotkeys.

## Primary implementation references

[Microsoft custom DWM frames/non-client sizing](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe), [native window subclassing](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-setwindowsubclass), [Qt QWindow native window operations](https://doc.qt.io/qt-6.8/qwindow.html), [Microsoft borderless access request](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.graphicscaptureaccess.requestaccessasync), [Qt Quick Widgets rendering/lifecycle](https://doc.qt.io/qt-6.8/qquickwidget.html). Quick Widgets add a GPU offscreen pass and render on the UI thread; the small material regions are coalesced, and capture/encoding remain separate. Gameplay overhead has not been measured.
