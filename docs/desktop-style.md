# Desktop material correction — 7 October 2026

Workspace verified: `C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip`. All changes, private profiles, outputs and artifacts are within PandaClip. The existing personal process `49188`, running `build/repair-v1/Release/PandaClip.exe`, was not controlled, replaced or terminated.

## What changed

- Capture's arrangement is unchanged. Source capsule, actions, five independent gears, unified audio strip and gains retain their existing behavior. Purple tint, lavender rims, typography, hover/focus states and caption buttons now match Media.
- The existing `DesktopBackdrop` implementation supports both QWidget and QWindow. Media previously had no compositor attachment and painted a full-window thumbnail; that source/property has been removed. Its outer background now follows the same effective Windows/application policy as Capture.
- `GlassPanel` separates material styling from background source. Library panels use the shared GPU foreground material; only bounded playback controls retain local decoded-video sampling. Removed the opaque shadow source; no screenshot/desktop/preview sampling was introduced for styling.
- Both top-level windows retain alpha/transparent clear surfaces. Custom chrome preserves Acrylic frame margins for QWindow as well as QWidget. Native preview and the non-native Qt material layer protections remain in place.
- Windows transparency, high contrast, composition availability, application Reduce Transparency and backdrop API results select the effective mode. Policy refresh is coalesced after native notifications, with HWND/generation/error diagnostics. The owned HWND subclass observes notifications before Qt's platform handler consumes them.
- The Windows-settings action lives in the existing Capture footer. Short windows reduce the same action-column spacing/heights, preventing the status/action from overlapping audio gains; five new geometry assertions check this at 860x620. Normal dashboard arrangement is preserved.
- Capture, Media and Appearance provide **Open Windows transparency settings** (`ms-settings:easeofaccess-visualeffects`). Quiet verification suppresses opening OS UI. Appearance and Diagnostics expose detailed fallback reasons. No global preference is changed.

## Actual deployed windows

These are external bounded compositor screenshots of the **shipping Release executable**, not internal QML renders. Media shows eight private generated fixture clips; no personal library was opened. Capture enumerated a source name but did not capture that application in these shipping checks.

![Actual Capture, Windows transparency disabled](screenshots/desktop-style/Capture.png)

![Actual View Media, Windows transparency disabled](screenshots/desktop-style/Media.png)

Also inspected: [Capture moved/resized over red background](screenshots/desktop-style/Capture-red-resized.png), [Media moved/resized over red background](screenshots/desktop-style/Media-red-resized.png), [Capture at 150%](screenshots/desktop-style/Capture-150.png), [Media at 150%](screenshots/desktop-style/Media-150.png), [resized Capture at 150%](screenshots/desktop-style/Capture-150-resized.png), [resized Media at 150%](screenshots/desktop-style/Media-150-resized.png).

**Visible desktop transmission: BLOCKED / NOT VERIFIED.** The effective system value was `EnableTransparency=0`, composition was available, high contrast was off, and the private app preference requested transparency. Both helpers honestly reported Windows transparency off. Blue and red owned background HWNDs were placed behind each window, with movement/resizing and Media opening checked at 100% and 150%. Their dark-purple solid fallback stayed unchanged. Neither API success nor translucent foreground pixels is evidence of visible desktop blur.

Windows Desktop Acrylic supplies compositor blur/tint. Optical desktop refraction is not implemented. Windows can also choose solid material for inactive windows or other compositor policy, even when the request API succeeds.

## Checks performed

| Check | Actual result / scope |
| --- | --- |
| Debug and Release MSVC/CMake | Successful deployed builds in `build/desktop-style`; existing nonfatal warnings remain |
| Full Release frontend, 100% | 377 assertions passed, zero failed; `desktop-style-compact-release` |
| Full Debug frontend, 100% | 377 passed, zero failed; `desktop-style-compact-debug` |
| Full Release frontend, 150% | 377 passed, zero failed; `desktop-style-compact-150` |
| Exact final shipping executable | SDK-free launch; actual Capture/Media HWND screenshots over two owned scenes at both scales; five gears invoked through UI Automation on an inactive desktop; clean shutdown |
| Effective-policy refresh | Native settings notification re-queried real policy in both windows: Capture generation 3→4, Media 1→2; system values were not altered |
| Controls/preferences | Five correct bounded anchored popovers, Escape/outside dismissal, focus restoration, small-window reachability, wheel over editors, scrollbar/keyboard routing, settings retention and independent layout restoration |
| Recording/audio/replay | Generated GPU/PCM fixtures exercise recording, replay, shared exports, live mute worker atomics, floating-point meters, drain/finalization and no-frame retry; actual WASAPI Check Mic can open the selected endpoint, but audible speech is not established |
| Media | Eight fixture clips, search/sort/filter, decoded GPU video, position-preserving track selection, volume/mute, fullscreen/Escape, errors and playback-local glass |
| Captions | Native client/Qt geometry, corner resize/maximize hit tests, native and Qt maximize/restore/minimize, taskbar work-area bounds and supported client-title system-move request; owned move loop cancelled, no physical drag/snap claim |
| Controlled write failures | 17 assertions passed: no false Saved/final MP4 on failure, controls recover, healthy replay continues, two successful retry files finalize |
| Independent decoding | 47 final-run files passed: 23 newly produced recording/replay/retry/WGC files plus 24 copied fixture files; all streams decoded, video rate/timestamps and AAC track layout/sample rates checked |

Full frontend fixtures use the shipping presentation source compiled into `PandaClipQuietApp`, with verification-only GPU/PCM seams. Their foreground-alpha fixture is **not** an OS backdrop override or desktop-transmission proof. All test profiles/output/logs are under `build/quiet-tests`. The exact shipping executable contains no generated-source seam.

The full-run lightweight UI health tick averaged 2.094 ms Release/100%, 2.033 ms Release/150%, and 5.361 ms Debug/100%. These are instrumented test-process measurements, not game frame times, an Acrylic benchmark or a before/after performance comparison.

### Real owned-window WGC check

The isolated `desktop-style-compact-wgc` run used an actual moving owned HWND, WGC-owned GPU copies and direct NVENC, with no generated video substitution, personal game control, game audio or audible stimuli. A passive external [native-preview screenshot](screenshots/desktop-style/Owned-WGC-preview.png) confirms sharp D3D preview alongside the corrected material. This screenshot is from the verification executable, distinguished from the shipping-window images above.

Decoded files:

- Ordinary recording: 1,995 frames, **66.500 s**, 1280×720 at 30 FPS.
- Shared-session replay: 1,773 frames, **59.100 s**, same format.
- Fresh warm-up replay: 94 frames, **3.133 s**, same format.

Capture/encoding continued across asynchronous export; the actual Qt Advanced Capture Stop action worked after encoder drain. This verifies an owned moving window, not Geometry Dash cadence/performance. Audio was intentionally disabled in this WGC run; the separate generated-PCM frontend runs checked audio stream packaging.

An additional 47 files from the earlier material iteration also decoded; headline counts above describe the selected final runs, not unique source fixtures.

## Failed or limited checks retained honestly

- The first `desktop-style-shipping-materials` screenshot harness observed its owned app becoming foreground. Native `WS_EX_NOACTIVATE`/`WS_EX_TRANSPARENT` are now set **before first show**, and the launcher fails on observed owned foreground activation. Corrected shipping screenshot runs at both scales reported zero. No global mouse/key injection, physical pointer movement, beeps or control of a personal process was used. The first failure is preserved; it is not called an isolated pass.
- Initial full-run policy assertions using a posted settings message failed for Capture; the actual synchronous native settings notification is now observed at the owned HWND before Qt consumes it. Subsequent full runs passed both generation checks. Original failed artifacts remain.
- The first real-WGC attempt ran concurrently with the owned-background screenshot harness and selected that temporary, identically named fixture; it closed during capture. The isolated retry above passed. Active-desktop fixture runs must be serialized; this is not evidence of a personal-window or real-game closure failure.
- Early passive resize screenshots were cropped because the launcher virtualized DPI coordinates. The launcher now declares process-local per-monitor DPI awareness, uses the owned UI Automation transform to resize Qt consistently, respects minimum tracking sizes, and records requested/native client bounds. The corrected 150% client sizes were Capture 1410x1010 and Media 1200x890. Actual screenshots then exposed the small-window transparency-action overlap; moving the action to the footer and compacting short-window spacing fixed it. Final screenshots and five extra geometry assertions passed.
- Actual desktop Acrylic transmission, physical dragging/snapping/sliders, real global shortcut delivery/consent, audible microphone speech, listening to every track, physical device disconnection, perceptual AV sync and moving GD performance remain manual. Quiet generated PCM or monotonic timestamps cannot establish these.

## Manual acceptance

1. Launch the Release below. Click **Open Windows transparency settings** and enable **Transparency effects** yourself. In PandaClip Appearance leave Reduce Transparency unchecked. The status should refresh without restarting the encoder. Check high-contrast/reduced-transparency fallbacks too.
2. Activate and move Capture and Media over two different browser/editor scenes. Both should blur their own underlying scene, with dark-purple readable tint, sharp thumbnails/text/video and no full-window media image. Resize/open Media and repeat. Windows may use solid Acrylic for inactive windows.
3. Use the five gears, gain sliders and mute buttons at the current scale and 150%; drag/snap/minimize/maximize both captions. Record with replay active and save while opening Media/Settings; play the finalized output.
4. Check the selected mic's opened name; speak during Check Mic, record game-only/mic-only/both and listen separately to Mix/Game/Mic. Test physical microphone removal/reconnection. Benchmark continuously moving Geometry Dash against capture-off using the same level/settings and game frame-time overlay.

## Exact tested Release launch

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\build\desktop-style\Release\PandaClip.exe'
```

SHA-256: `160A17B6A9A828519B60B1B0DC45C08A4C693B1B53F1D18D9DDEC45277483567`.

Keep the entire deployed folder. VS Code **Ctrl+Shift+B** builds Debug; **F5 → PandaClip (Debug)** uses `build/desktop-style/Debug/PandaClip.exe`; **Run Task → CMake: Build Release** builds this Release. The older personal executable remains available; exit it through its own controls before starting another personal instance with competing hotkeys.

Reproduction uses fresh profiles from `tests/prepare_qt_profile.py`. `PandaClipQtQuietLauncher.exe <shipping exe> <profile> --active-materials` opens Media and captures only owned windows/scenes with passive input flags. Run it alone, not alongside another active-desktop fixture. Full frontend checks use the verification executable on an inactive desktop; set child `QT_SCALE_FACTOR=1.5` for scaling. `--shipping-ui` checks actual deployed controls without activating the input desktop; `--fault-only` uses only process-local hooks/project output. No runner changes Windows transparency.
