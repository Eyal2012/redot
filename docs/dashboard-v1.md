# PandaClip v1 dashboard redesign (superseded)

The new reference and actual-window repair supersede this report. Use [repair-v1.md](repair-v1.md) and `build/repair-v1/Release/PandaClip.exe`. These historical internal render checks did not establish correct GPU material presentation in the deployed native window.

The supplied final mockup was inspected directly. This pass replaces the default dock workspace with its dashboard arrangement, preserving the working engine and earlier frontend fixes. The default is Capture/navigation/selected-source bar/native preview/two audio cards/right Controls/status. Optional Advanced docks retain real Qt docking. Main audio-card height stays bounded when returning from a floated advanced layout.

## Presentation and settings

Five distinct gears open Recording, Replay, Save Clip, Game audio and Microphone Quick Settings. A separate source-bar popover handles capture selection/start/stop/cancellation. These are actual Qt editors bound to the existing controller preferences, not a duplicate settings store. Custom duration/RAM are validated, Replay/Save Clip share one duration, and custom shortcuts use the existing binding/conflict path. Encoder options lock during active sessions; audio source/gain changes keep their existing session boundaries, while mute is live. The popovers explain this policy.

One scrollable popup is open at a time; placement respects window/screen bounds and avoids the opener. Smaller windows shorten the viewport, rather than hiding preferences. Wheel input over choices/editors scrolls without changing values. Escape/outside dismissal and queued focus restoration preserve keyboard use. Advanced Settings retains its moving category viewport, debounced persistence, explicit device refresh and lightweight updates.

The native D3D11 preview retains aspect ratio and ungraded pixels. Rounded native-window clipping is geometry-only; no shipping frame readback/CPU blur was added. Meter values remain coherent floating-point PCM RMS/peak, with raw activity before recording gain/mute. Mic checks remain worker-based, pinned to the selected endpoint, without an encoder or speaker monitoring.

## Real backdrop implementation and limits

`DesktopBackdrop` requests documented DWM **Desktop Acrylic**, using `DWMSBT_TRANSIENTWINDOW`, not wallpaper-only Mica. Qt native roots are prepared with supported frameless/translucent flags before native creation. The D3D11 Quick target has alpha and a transparent clear color. One stable GPU foreground layer per workspace/dialog draws bounded lavender tint, rounded highlights and shadows. There is no internal wallpaper, background screenshot, desktop capture or preview texture used as styling input.

Windows owns scene acquisition and blur. Policy is cached and refreshed on native setting/composition/theme changes; painting/scrolling does not repeatedly read policy. High contrast, disabled Windows transparency, unavailable composition/API, or Reduce Transparency select the deliberate solid fallback. Status reports the request/fallback honestly. Native menus and detached advanced docks remain solid; Media retains its existing local-content glass/refraction.

The representative panel/button preflight passed alpha/render checks before the full conversion. **DWM reports composition disabled on the inactive owned desktop. Visible desktop transmission is therefore unverified.** Alpha/API success does not prove that physically underlying windows show through. No synthetic background was substituted for this check. Optical displacement/refraction of desktop pixels is not implemented; DWM blur plus foreground rim/tint/shadow is the supported approximation. Background transmission, native-preview compatibility and active-display appearance remain acceptance checks below.

Official references: [Microsoft system backdrops](https://learn.microsoft.com/en-us/windows/apps/develop/ui/system-backdrops), [DWM backdrop types and Windows requirement](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type), [DWM frame integration](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmextendframeintoclientarea), [Qt Quick Widget transparency](https://doc.qt.io/qt-6.8/qquickwidget.html).

## Actual screenshots and visual review

These are running Qt views from an inactive desktop, not mockups or composites with desktop wallpaper. Main-preview readback is verification-only and labeled as an owned GPU fixture, not Geometry Dash/WGC. DWM's backdrop is absent from these captures; transparent pixels retain alpha. Media pictures contain independently encoded/decoded fixture media.

| View | Screenshot |
| --- | --- |
| Capture / replay, synthetic native preview | [Capture](dashboard-v1-screenshots/capture.png) |
| Empty source | [Empty](dashboard-v1-screenshots/empty.png) |
| Recording / Replay / Save Clip gears | [Recording](dashboard-v1-screenshots/recording-quick.png), [Replay](dashboard-v1-screenshots/replay-quick.png), [Save](dashboard-v1-screenshots/save-quick.png) |
| Audio gears | [Game](dashboard-v1-screenshots/game-audio-quick.png), [Mic](dashboard-v1-screenshots/microphone-quick.png), [Small mic viewport](dashboard-v1-screenshots/microphone-quick-small.png) |
| View Media | [Media](dashboard-v1-screenshots/media.png), [Playback](dashboard-v1-screenshots/playback.png) |
| Reduced transparency | [Solid](dashboard-v1-screenshots/solid.png) |
| Scaled dashboard | [150%](dashboard-v1-screenshots/scaled.png) |

Review against the supplied image corrected stock colored icons, duplicate control borders, navigation styling, the source-bar border, folder presentation, oversized Recording popup and audio-card growth after advanced docking. Layout, readable controls and real states were inspected; blank fixture pillarboxing is intentional aspect preservation. The render passes alone do not establish active-desktop glass, physical pointer behavior or gameplay performance.

## Verification evidence

Debug and Release built successfully with the deployed dynamic Qt runtime. Both full frontend runs finished with **zero failures at 96 DPI**. A separate final Release run at **144 DPI / 150%** passed popup placement, small-window viewports and native/Qt client-size agreement. Shipping Debug/Release smoke tests passed with an SDK-free PATH, and a second launch restored layout, floating advanced docks, recording preferences and reduced appearance settings.

Full runs exercised actual Qt controls for missing/selected source, simulated consent outcomes/cancellation, ordinary recording, replay saving, simultaneous recording/replay, hidden preview, all five gears, canonical quick/advanced agreement, custom duration/shortcut input, mic selection/check, raw meter delivery, scrolling/resizing, optional dock floating/restoration, caption buttons/hit tests, Media playback/track selection while capture continued, bounded first-image failure and retry, and async shutdown. The engine input was a labeled GPU fixture. A quiet real WASAPI check opened **Microphone (Realtek(R) Audio)** and confirmed low-level PCM/coherent meter delivery; this does not establish speech or audible application-loopback audio.

Independent verification decoded **28 MP4 files** across the final profiles (14 each: eight copied deterministic fixtures and six files newly produced by each UI run). H.264/AAC streams, enabled track layouts, 48 kHz audio, timestamps and durations passed. The test profile used **1280×720 / 30 FPS**. Ordinary/replay fixture decoding does not validate real WGC/gameplay cadence or perceptual synchronization. New frontend ordinary/replay outputs included all-enabled, game-only, mic-only and video-only cases; deterministic PCM fixtures remain distinct from actual WASAPI/listening.

Replay byte/duration/keyframe stress, simulated border policy, floating-point/concurrent PCM meters, bounded logging and controlled unavailable-log-folder handling, settings persistence and media services also passed before the frontend checks. The initial all-in-one runner then stopped on the demonstrated cancellation bug; its overall run is **not** claimed as passing. Corrected full frontend runs and final decoding/deployment checks are the passing evidence. Earlier failed artifacts remain intact.

| Evidence | Project-local artifact |
| --- | --- |
| Release / Debug builds | `build/dashboard-build-release-final.txt`, `build/dashboard-build-debug-final.txt` |
| Final full Release / Debug UI | `build/quiet-tests/dashboard-v1-release-complete/checks.txt`, `build/quiet-tests/dashboard-v1-debug-last/checks.txt` |
| 150% scaling / composition preflight | `build/quiet-tests/dashboard-v1-scale150-final/checks.txt`, `build/quiet-tests/dashboard-v1-composition-final/composition-preflight.txt` |
| SDK-free shipping / restore | `build/dashboard-shipping-release.txt`, `build/dashboard-shipping-debug.txt`, `build/dashboard-restore-release.txt`, `build/dashboard-restore-debug.txt` |
| Independent MP4 decoding | `build/dashboard-decoding-final.txt` |
| Earlier component checks and failed runner | `build/dashboard-verification-release.txt` |
| Actual WGC attempt | `build/quiet-tests/dashboard-v1-wgc/checks.txt` — **BLOCKED: selected window has no capturable client area on the inactive desktop**, no generated substitution |

The separate reduced-transparency run in `build/quiet-tests/dashboard-v1-solid` passed; its actual QWidget PNG has opaque root alpha (255), compared with foreground-glass alpha (128). Legacy unchanged-frame RGB comparisons in the full-run logs are **not** background-change/compositor evidence. Their label has been corrected in the verifier to describe static foreground stability; the assertion and frontend behavior are unchanged.

### Measured Release responsiveness and rendering

Measurements are from the final Release run above, on an inactive desktop. Each wheel case sends 100 targeted messages to actual Qt editors; these timings are synchronous handling cost, not physical input-to-photon latency. The 200 ms health/status tick averaged **1.608 ms**, maximum **19.176 ms**, over 364 samples including the full test sequence.

| Settings condition | Wheel median / p95 (ms) |
| --- | --- |
| Idle | 0.0138 / 0.0471 |
| Recording | 0.0036 / 0.0093 |
| Replay | 0.0040 / 0.0066 |
| Both | 0.0043 / 0.0135 |
| Playback glass / solid | 0.0038 / 0.0077; 0.0040 / 0.0075 |

Paired workspace stages used the same 1380×860 window, a verification-only foreground-alpha fixture and a forced changing probe pixel for about 3.1 seconds each. D3D11 timestamp queries are bounded/nonblocking, exclude Present/vsync/DWM, and have no skipped/disjoint samples. They measure Qt scene submission/rendering, not total capture cost.

| Workspace | CPU render median / p95 (ms) | GPU median / p95 (ms) | Whole test-process CPU, one-core equivalent | Private / working-set snapshot (MiB) |
| --- | --- | --- | --- | --- |
| Foreground glass | 0.1943 / 0.2521 | 0.0286 / 0.0320 | 5.56% | 899.16 / 495.82 |
| Solid | 0.1884 / 0.2224 | 0.0233 / 0.0245 | 5.51% | 786.28 / 438.14 |

These memory figures cover the whole verification process after repeated Media/encoder sessions, including decoder/driver caches. They are not fresh-start memory, VRAM accounting, a leak measurement or a Lite performance promise. The short sequential snapshots cannot isolate every allocation to glass. DWM composition cost was unavailable. During active decoded Media playback, GPU median/p95 was **0.1804/2.1278 ms with local glass**, **0.1478/0.5558 ms solid**. The stages had different presented-frame counts; these are descriptive measurements, not proof of equal workload or game overhead. No ten-minute stability/gameplay benchmark was repeated for this presentation change.

## Manual acceptance checklist

- On Windows 11 build 22621+, enable Windows Transparency effects and PandaClip transparency. Place an owned colorful window behind PandaClip. Change its colors/position: the transmitted backdrop must change. Move PandaClip over contrasting windows; repeat with preview visible/hidden. Test Capture, every popup, Settings and Diagnostics. If the badge says requested but the backdrop is absent, report that result; API acceptance is insufficient.
- Inspect foreground readability, black patches, clipped controls, rounded edges, hover/focus, Reduce Transparency and Reduce Motion on the active display. Preview pixels should stay sharp and ungraded.
- Physically drag/resize/snap the window, double-click its caption, drag from maximized, use Alt+Space and move between monitors/scales. Check work-area bounds. Quiet tests cancel only their owned move loop and do not establish physical dragging/snap flyouts.
- Select real Geometry Dash, confirm borderless consent/status and visible outline behavior, then continuously moving gameplay recording and replay. Compare game frame times/capture smoothness with capture off vs replay on. Synthetic fixtures are not gameplay evidence.
- Speak into the exact selected mic, confirm its opened name, Windows volume/mute and raw meter, then listen separately to Mix/Game/Mic. Check selected-process audio audibly, mic disconnection/reconnection and real-device changes. Quiet input noise/nonzero samples do not validate speech or listening.
- Check physical hotkey delivery/conflicts, outside clicks, scrollbar dragging and source changes. Observe warming/reset notices at configuration boundaries. Close Media while recording/replay continues, then test configured tray/exit finalization.

## Roadmap, not implemented here

Full and Lite editions will share one recording core. Lite focuses on clipping with minimal interface/resource use. Later scenes/sources and audio routing are planned. Streaming, direct multistreaming and Linux support are planned for v2. This pass does not implement Lite packaging, scenes, streaming, Linux capture, a new logo, automatic highlights or trimming.
