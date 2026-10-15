# Shared View Media glass and capture handoff

Verified workspace: `C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip`.

Delivery: `build\shared-glass\Release\PandaClip.exe`. Keep the deployed Qt DLLs, plugins, QML and license/source folders alongside it. Older outputs and personal sessions were preserved. VS Code build/F5 configurations now use this directory; task labels are retained.

Final Release SHA-256: `A89C078920A57E5D04276FAB32718BCC6D9700DD59481D880EE463DAE73C48AC`.

## Confirmed causes and fixes

### Live preview, but no recording frames

The personal diagnostic log at 22:09:47 on October 6 showed the first WGC image arriving before NVENC attached its recording ingress. Disabling replay roughly four seconds later produced the actual no-frames failure. This was a worker failure, rather than merely old status text. A damage-driven capture source can stop delivering changes while the preview still displays its retained image.

Capture now seeds a newly attached ingress with that retained **owned GPU copy and its lease**, timestamped at the new session's current monotonic time. It never borrows a returned WGC pool surface or reuses the old image's compositor timestamp as a new recording origin. An in-flight WGC copy re-reads the attached ingress when publishing. Publication and attachment use the same state lock; the existing bounded timestamp-ordered recording queue and encoder clock remain.

Diagnostics distinguish cached startup images from fresh WGC availability. Normal CFR behavior repeats the last image when no newer image is available. A completely missing first image fails within ten seconds after encoder/audio initialization, even while audio continues arriving. The failure releases controls, does not report Saved, and permits a successful retry. The quiet fixture verified retained-image startup, the real ten-second wait, visible failure, responsive Settings during that wait and retry/finalization. Actual moving WGC remains blocked in the isolated environment below.

### Title-bar input and coordinates

The earlier native frame stripped Windows' caption while Qt still calculated standard frame margins: a 1320×860 Qt workspace had a 1338×907 native client. This made the visible layout and native coordinate system disagree. The new windows declare Qt's supported `FramelessWindowHint` before native creation; the existing chrome restores Windows sizing/system styles and supplies caption, corner, maximize and work-area hit tests. Qt and native client sizes now agree.

Client title presses explicitly call `QWindow::startSystemMove`; client and native caption double-clicks toggle the owning widget's maximize/restore state. Native caption/edge presses enter Windows' move/resize handler rather than the dock layout's client event filter. Caption buttons, native hit targets, maximized work-area bounds, minimize, restore and close/drain were checked. Quiet movement testing requests then cancels the **owned** Windows move loop. It does not prove physical dragging, drag-from-maximized restoration or snapping on the active desktop.

One final test crash was caused by the test itself calling `QMainWindow::menuBar()` after the real menu bar had moved into the custom header. Qt created a second empty bar; the test accessed its empty action list. Verification now locates the actual visible bar by its object name. Its File popup and keyboard navigation pass.

## Shared material implementation

`qml/GlassPanel.qml` and the existing `qml/glass.frag` remain the common material renderer. `GlassTokens.qml` extracts Media's palette, blur, refraction and interaction values; the widget foreground stylesheet also reads those tokens. Media's existing layout/material behavior is preserved.

- Half-resolution, bounded local `ShaderEffectSource` sampling; coalesced refresh and no refresh when content is unchanged.
- The same GPU blur, adaptive smoked tint, curved rim reflections, rounded masking, 2.6 px edge refraction and elevation shadow.
- Main surfaces sample a restrained violet/teal application backdrop. Media surfaces sample their own thumbnails/decoded video. Neither samples the desktop or the live preview.
- One stable `QQuickWidget` material layer per workspace/dialog, behind the actual interactive Qt Widgets. It has no focus and is transparent to mouse input. Real docks, meters, settings controls and native D3D preview retain their ownership.
- At most 128 visible regions. Structure discovery is coalesced after structural changes; the 100 ms geometry check updates QML only when geometry/material mode changes. Scroll handlers do not discover devices, rebuild rows, write settings or resize capture.
- Main caption/buttons, menu bar/status, Sources/Mixer/Controls panels and controls, dock headers, Settings and Diagnostics use the shared renderer. Reduce Transparency selects the shared solid fallback; Reduce Motion remains supported.

**Supported-composition limitation:** detached docks use the solid palette because an experimental Quick-backed dock crashed Qt 6.8 when returning to the main hierarchy. The safe implementation never reparents a GPU scene across those top-level render interfaces. Native menu/list popups also use the solid palette; the main menu bar itself uses glass. This delivery therefore does not claim GPU glass in every native popup or detached dock.

## Actual screenshots and visual inspection

These are running-app captures on an owned inactive desktop, with generated GPU video and quiet captured WASAPI samples. They are not Geometry Dash screenshots. Main preview readback/compositing exists only in the verification target; shipping preview stays native D3D11. No user screenshot file accompanied this attachment, so a direct pixel comparison against that original screenshot was unavailable.

| View | Screenshot |
|---|---|
| Existing Media baseline | [Before Media](shared-glass-screenshots/before-media.png) |
| Previous main workspace | [Before main](shared-glass-screenshots/before-workspace.png) |
| New recording workspace and populated mixer | [Recording](shared-glass-screenshots/workspace-recording.png) |
| Replay active | [Replay](shared-glass-screenshots/workspace-replay.png) |
| Shared recording/replay active | [Both](shared-glass-screenshots/workspace-recording-replay.png) |
| Real pre-record microphone check | [Mic check](shared-glass-screenshots/workspace-mic-check.png) |
| Settings | [Settings](shared-glass-screenshots/settings.png) |
| Diagnostics | [Diagnostics](shared-glass-screenshots/diagnostics.png) |
| Floating dock, solid fallback | [Mixer](shared-glass-screenshots/mixer-floated.png) |
| Native File popup, solid fallback | [File menu](shared-glass-screenshots/menu-solid-fallback.png) |
| Same Media reference after extraction | [Media](shared-glass-screenshots/media-glass.png) |
| Decoded video under Media glass | [Playback](shared-glass-screenshots/playback.png) |
| Main contrasting backdrop probes | [Bright](shared-glass-screenshots/workspace-pattern-a.png), [Dark](shared-glass-screenshots/workspace-pattern-b.png) |

Inspected full-window captures for text, controls, rounded surfaces, caption buttons, dock geometry, popup usability and sharp preview/video. Source checkboxes no longer have unexplained black squares; the preview's dark pillarboxing and meter troughs are intentional. Main uses broad restrained color variation; Media's colorful video produces stronger local translucency through the **same shader**. Main caption, recording-button and panel samples changed by RGB distances 167, 167 and 106 under contrasting backdrops; all three changed by zero in solid mode. Media separately changed when decoded white video was presented. These checks establish real sampling rather than a fixed decorative texture.

## Verification scope and artifacts

All verification uses owned processes/profiles/output beneath `build`, targeted APIs and an inactive desktop without `SwitchDesktop`. No pointer movement, global input, foreground activation, audible playback or personal app/session control. Generated-source seams are absent from shipping. Failed exploratory artifacts were retained.

| Check | Evidence/outcome |
|---|---|
| MSVC/C++20 Debug and Release; deployed dynamic Qt/QML | `build/shared-glass-build-debug.txt`, `build/shared-glass-build-release.txt`: builds succeeded |
| Final Release full suite | `build/quiet-tests/run-20261006-234040-Release`, `build/shared-glass-delivery-release-final.txt`: PASS; actual Qt checks have zero failures |
| Final Debug full suite | `build/quiet-tests/run-20261006-234030-Debug`, `build/shared-glass-delivery-debug-final.txt`: PASS; actual Qt checks have zero failures |
| Real moving WGC attempt | `build/quiet-tests/shared-glass-wgc-delivery`, `build/shared-glass-wgc-delivery.txt`: **BLOCKED**, selected owned moving HWND has no capturable client area on inactive desktop; fixture substitution disabled |
| Release process-local 150% scaling | `build/quiet-tests/shared-glass-scale150-delivery`, `build/shared-glass-scale150-delivery.txt`: PASS; zero failures, 144 DPI. Workspace 1320×860 logical = 1980×1290 native; Settings 900×700 = 1350×1050; Diagnostics 900×650 = 1350×975 |

The full suite covers actual Qt source selection; automatic recording/replay start requests; missing-source errors; simulated allowed/denied/cancelled/failed/timed-out borderless consent; duplicate-start prevention; microphone selection/open check; game/mic gains and mutes; recording/replay/Save Clip; hidden-preview recording; Media while encoding; independent decoding of every produced MP4 stream; solid/motion settings; wheel over controls, thumb-position and keyboard scrolling, resize/category access and persistence; dock float/redock/hide/reset/lock and second-launch restoration; diagnostics; native captions; SDK-free shipping launch and asynchronous close/drain. Component tests cover bounded compressed replay/snapshots, logging pressure/write failures, simulated border policy, AAC delay/adjustment and timestamp comparison regressions. Production hotkey registration/conflict was checked on an owned inactive-desktop window; physical global key delivery was not.

One intervening Debug run (`run-20261006-233741-Debug`) failed two no-image assertions because the test clicked Record before the visible button had been re-enabled after worker exit. It now waits for controller completion and confirms the next start was accepted. Both final suites passed the ten-second error/retry check. Parallel builds also exposed duplicated shipping `qrc` generation rules inherited by the quiet target; CMake now gives that target only its own Qt resources. Build failures and exploratory crashes remain in their original artifacts.

The microphone check opened the isolated profile's **Microphone (Realtek(R) Audio)**. A coherent 480-frame packet had RMS 0.00019399999 and peak 0.00046456727; the production converter's sample square/count matched its meter calculation and the exact floating-point snapshot reached the actual Qt widget. During recording, process-loopback and mic snapshots also reached Qt exactly (414 and 409 observed packets). Low-level noise/silence and nonzero samples do **not** establish audible microphone speech, game acquisition or listening quality. The separate generated PCM fixtures validate encoding/track layout, not real game audio.

## Release measurements

From the preceding passing Release measurement run `build/quiet-tests/run-20261006-233605-Release` (`build/shared-glass-delivery-release.txt`), same production UI/material implementation, same test profile/display and sequential glass/solid phases. The last changes affected only verification readiness/screenshots and CMake resource generation. Personal machine load is uncontrolled; this is not an isolated gaming benchmark. No active GD performance result is claimed.

| Measurement | Glass | Solid |
|---|---:|---:|
| Main cached GPU scene draw, median / p95 | 0.0428 / 0.1045 ms | 0.0348 / 0.0520 ms |
| Main render CPU, median / p95 | 0.2787 / 0.3660 ms | 0.2443 / 0.3041 ms |
| Media playback GPU, median / p95 | 0.1597 / 1.7991 ms | 0.1012 / 0.3692 ms |
| Media playback render CPU, median / p95 | 0.1513 / 0.5153 ms | 0.1180 / 0.4815 ms |
| Settings wheel dispatch during playback, median / p95 | 0.0047 / 0.0326 ms | 0.0045 / 0.0163 ms |

Main measurements use three-second phases (195 GPU samples each), forcing a one-pixel probe redraw of an otherwise cached material scene. They exclude QWidget backingstore composition, initial material refresh, full UI painting, native preview and encoding. Media measurements use four-second playback phases (451/367 GPU samples), with bounded nonblocking D3D queries. Wheel times measure event handling, not full presentation latency. Idle/record/replay/both wheel medians were 0.0039/0.0037/0.0045/0.0038 ms; lightweight UI polling averaged 1.789 ms, max 4.956 ms over 330 ticks. The preserved baseline UI tick averaged 1.454 ms; differing total phases/load prevent treating that as a controlled regression estimate. Baseline Media GPU medians were 0.1736/0.1100 ms.

## Remaining active-desktop checks

1. Launch the new executable, select **Geometry Dash** in Source Properties, and start recording/replay with preview visible, then hidden. Confirm moving gameplay, successful stop/save and playback. Report fresh WGC cadence separately from cached startup/CFR repeats in Diagnostics.
2. Physically drag empty title space; double-click; drag from maximized; resize edges/corners; snap; use Alt+Space/Alt+F4 and keyboard navigation. Repeat across monitors/scales. Quiet tests verify hit geometry and supported move dispatch, not actual mouse behavior.
3. Speak into the selected mic during Check mic and recording. Confirm the opened endpoint, gain/mute and meters; listen to Mix, Game and Mic tracks. Test device disconnection/reconnection without endpoint switching.
4. Check real Windows border consent and visible outline. Simulated policy results do not validate permission dialogs or disappearance of the outline.
5. Compare main/Media appearance, focus/hover/disabled/Close states, reduced transparency/motion and floating-dock fallback on your active display. Verify physical hotkey delivery/conflict with an older app instance.

No trimming or new milestone was added.
