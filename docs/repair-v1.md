# PandaClip dashboard repair and new reference

Verified workspace: `C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip`. Current launch: **`build\repair-v1\Release\PandaClip.exe`**. Earlier builds, personal settings, logs and saved recordings were preserved. The new reference supersedes [dashboard-v1.md](dashboard-v1.md).

## Demonstrated faults and repairs

1. **The GPU foreground was not presented correctly in the real native window.** A WGC screenshot of the deployed shipping HWND showed missing material surfaces. A bounded compositor screenshot over an owned blue background also showed the background directly through the purported solid fallback. Internal Qt `grabFramebuffer()` images had concealed this fault. Diagnostics reported the material `QQuickWidget` as native (`native=1`). The native preview was causing Qt to force its siblings native. Set `AA_DontCreateNativeWidgetSiblings` before `QApplication`, prevent native preview ancestors, and establish the root RHI backing store before creating the preview HWND/chrome. The material is now non-native (`native=0`, RHI owner surface), and an external screenshot of the shipping HWND shows the actual materials. [Qt documents native QQuickWidget rendering risks](https://doc.qt.io/qt-6.8/qquickwidget.html). Missing materials are confirmed; all of the user's physical click failures were not independently reproduced.
2. **An opaque purple source was rendered underneath translucent panels.** `MultiEffect` displayed its opaque shadow-source rectangle as well as the shadow. Removed this source/effect from desktop foreground panels, retaining restrained GPU rims and DWM window elevation. The actual action-center alpha is now 80/255, root 56/255 in the foreground-alpha verification mode. This measures foreground rendering, not desktop transmission.
3. **Live dashboard mutes were disabled during encoding.** The adapter refused edits to disabled compatibility controls, although the worker already supported atomic live mutes. Keep these controls enabled during recording/replay, and verify native-coordinate button input changes/restores worker mute bits without restarting capture. Gain and device changes retain next-session boundaries.
4. **The source capsule had dead click regions and replay claimed readiness too early.** The entire capsule is now one accessible button; status/arrow are mouse-transparent children. Replay explicitly shows available/requested warm-up history, and available-history exports explain keyframe/AAC boundaries. Missing-source feedback points to the visible source selector. Windows transparency policy now has a specific status rather than an ambiguous generic fallback.

The capture, QPC mapping, owned GPU leases, NVENC, AAC tracks, replay packet ownership/export, hotkeys and drain/finalization pipeline were preserved. No OBS/libobs, second recorder, desktop-copy styling, logo replacement or later roadmap feature was added.

## New-reference presentation

Thin caption; icon-above-label navigation; centered selected-source capsule; large rounded, opaque native D3D11 preview; three floating actions with their own gears; replay status/shortcut and plain elapsed clock; one shared audio strip with inline raw meters, two mute buttons, two gears and visible gains; compact footer. Removed Capture/Controls headings and enclosing control-card material. Neutral gray-blue tint/rims replace thick purple bevels. Recording Quick Settings is narrower, anchored with a pointer, and retains one bounded scroll viewport.

Five distinct popovers use canonical preferences: Recording, Replay, Save Clip, Game Audio and Microphone. Apply, Advanced, Escape, outside dismissal and focus restoration are retained. Settings wheel handling still moves one category viewport and neither enumerates devices nor writes preferences. Optional advanced docks and separate View Media remain available through the caption menu.

**Differences remaining:** Windows has `EnableTransparency=0` on this PC, so the observed main window is the deliberate dark solid fallback, not the reference's light desktop-transmitting appearance. The source uses a monitor icon rather than a scraped game logo. Live status is in the source capsule, rather than a red badge over the native preview. Hide Preview remains a small button below the preview rather than an overlaid pause symbol. Quick Settings retains explicit Apply, output-folder access and safe-boundary feedback; some fields require scrolling. Capture retains the selected source's native decoration where WGC supplies it. The screenshot does not replace real gameplay with the reference's Geometry Dash pixels.

## Actual screenshots and inspection

| Screenshot | Scope |
| --- | --- |
| [Shipping window](repair-v1-screenshots/shipping-window.png) | Exact final Release `PandaClip.exe`, external owned-HWND WGC capture on the current desktop. The available window name is read-only discovery; it was not started, controlled or captured. Windows transparency off; solid fallback. |
| [Real WGC recording](repair-v1-screenshots/real-wgc-frontend.png) | Same frontend source in the verification executable, actual owned moving HWND → WGC → native preview/NVENC. Passive shown window, no generated video substitution. Fixture caption is a legacy name; it is not Geometry Dash. |
| [Recording popup](repair-v1-screenshots/recording-quick.png), [Replay](repair-v1-screenshots/replay-quick.png), [Save](repair-v1-screenshots/save-quick.png) | Actual Qt widgets on an inactive owned desktop; foreground-alpha mode only, no visible DWM effect. |
| [Game Audio](repair-v1-screenshots/game-audio-quick.png), [Microphone](repair-v1-screenshots/microphone-quick.png), [Small microphone viewport](repair-v1-screenshots/microphone-quick-small.png) | Canonical settings, scrollable popup and named endpoint selection. |
| [150%](repair-v1-screenshots/scaled.png), [Settings](repair-v1-screenshots/settings.png), [Media playback](repair-v1-screenshots/playback.png) | Actual Qt presentation; preview/media content where present is explicitly generated verification input. |

Inspected sharp preview pixels, whole-capsule hit area, caption/client geometry, audio strip, clipping, popup alignment, small-window reachability, five gears, readable solid controls and state feedback. The real shipping screenshot is used separately from internal Qt images. Two final bounded physical-compositor attempts over changed owned backgrounds were skipped because other windows overlapped the region; no unrelated windows were repositioned to obtain a result.

## Desktop backdrop: verified and unverified

The implementation requests documented [DWM Desktop Acrylic](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type) (`DWMSBT_TRANSIENTWINDOW`) for the scene behind the window. Qt foregrounds supply bounded tint/rims; the native preview remains opaque. No wallpaper, cached desktop image or captured preview is used as a backdrop.

Current-desktop DWM composition reports enabled, but **Windows Transparency effects are off**. The app reports `Windows transparency off · solid appearance`, with the exact policy explanation in its tooltip/log. No Windows preference was changed. API acceptance/alpha alone cannot demonstrate desktop transmission. Active Acrylic over changing backgrounds remains a manual acceptance check. Desktop optical displacement/refraction is not implemented; View Media retains its separate local-content glass/refraction.

## Verification and isolation

Input isolation was investigated first. An inactive owned desktop provides independent Qt/native-coordinate input and UI Automation without `SwitchDesktop`. It does not establish WGC or visible DWM. Current-desktop WGC checks use only an owned moving source and a hidden/passive frontend; no global input, cursor movement, foreground activation, test beeps, personal-window commands or personal-profile writes. Only positively identified test processes can be terminated after a bounded timeout. Test outputs/logs/profiles remain under `build`.

### Passing checks

- Debug and Release shipping builds with dynamic Qt runtime deployment. Exact shipping Release SDK-free UI Automation invokes all five gears, their close buttons, missing-source Recording and safe close. Debug shipping SDK-free startup/shutdown also passes.
- Full actual-Qt checks use native client coordinates for the source capsule, all five gears, recording, replay, Save Clip and live mute. Generated video exercises ordinary/replay/shared sessions, hidden preview, Media/Settings navigation, MP4 finalization, gain/source settings, named mic selection, canonical quick/advanced agreement, custom duration/RAM/shortcut, keyboard dismissal, outside dismissal, persistence, small 860×620 window and bounded scroll viewports.
- Simulated border consent allowed/denied/error/timeout/cancellation, missing source, no-image first-frame timeout and retry, controlled output-write failure and subsequent recovery. These are simulated/control tests, not actual Windows consent decisions or disk exhaustion.
- Native caption hit testing, minimize/maximize/restore/close and Qt/native client dimensions at 100% and 150%. Targeted title dragging cancels only the owned move loop; physical drag/snap and display transitions remain manual.
- Real WASAPI check opens pinned **Microphone (Realtek(R) Audio)** at 48 kHz, delivers PCM RMS/peak to the actual Qt meter and reports Windows input volume/mute separately from PandaClip gain/mute. Nonzero samples do not establish speech. Generated PCM cases establish track packaging and do not validate audible process-loopback capture.
- Independent decoding passed **43 files**: eight fresh generated fixture originals, sixteen fixture copies, twelve newly produced full-frontend files (six per configuration), five real-WGC files, and two successful frontend write-failure retries. Each H.264/AAC stream was decoded; enabled Mix/Game/Microphone layouts, 48 kHz audio, timestamps and durations were checked. Failed exports publish no final MP4 and are not counted as saved files.
- A separate real-WGC run also checks preview Stop after encoder drain through the actual Qt Advanced Capture button. Its frontend is hidden, not the Win32 compatibility controls. Capture-source popup Stop on the input desktop remains manual: showing a Qt popup would grab input, while a hidden popup skips its normal visible-state refresh. The failed first attempt is retained under `repair-wgc-start-stop-final`; the corrected actual-button check passes under `repair-wgc-stop-checked`.
- **Real moving-window WGC:** visible native preview plus approximately 66 seconds of ordinary recording with replay attached to the same encoder; asynchronous clip save without stopping encoding; then fresh replay-only save. Newly generated video-only files independently decode: 1280×720 at 30/1 FPS, ordinary **66.333 s / 1,990 frames**, same-session replay **59.100 s / 1,773 frames**, warm-up replay **3.133 s / 94 frames**. Video timestamps start at zero, remain monotonic and meet the configured CFR. This is a timer-painted owned source, not gameplay or perceptual synchronization evidence.

### Measurements

Final Release actual-Qt run, inactive desktop, generated source, no simultaneous Debug test: 200 ms health tick average **1.958 ms**, maximum **8.584 ms**, 362 samples. Targeted 100-wheel-event synchronous handling costs below are not physical input-to-display latency.

| Condition | Wheel median / p95 (ms) |
| --- | --- |
| Idle | 0.0049 / 0.0466 |
| Recording | 0.0038 / 0.0040 |
| Replay | 0.0038 / 0.0042 |
| Recording + replay | 0.0040 / 0.0200 |
| Media playback glass / solid | 0.0036 / 0.0042; 0.0043 / 0.0101 |

The real WGC run logged 2,577 source frames, **38.84 FPS source availability**, 1,990 CFR output frames, 14 repeated frames, 599 replaced/late source frames, zero video queue drops and encoder average/max **3.05/20.89 ms**. Source max gap was 216.7 ms during passive-window presentation/startup. These are owned-window measurements at 30 FPS output, not GD cadence or replay overhead. No ten-minute gameplay/stability benchmark was performed for this repair.

### Artifacts

| Evidence | Project-local path |
| --- | --- |
| All configured Debug/Release targets | `build/repair-all-release-build.txt`, `build/repair-all-debug-build.txt` |
| Full final Release / Debug UI | `build/quiet-tests/repair-final-checked-release/checks.txt`, `build/quiet-tests/repair-final-checked-debug/checks.txt` |
| 150% native geometry and popup controls | `build/quiet-tests/repair-final-scale150/checks.txt` |
| Exact final shipping Release UIA | `build/quiet-tests/repair-last-shipping-controls/shipping-controls.txt` |
| Shipping Debug deployment | `build/quiet-tests/repair-debug-shipping-final/shipping-smoke.txt` |
| Restore preferences/layout | `build/quiet-tests/repair-final-checked-release/restore-checks.txt`, `build/quiet-tests/repair-final-checked-debug/restore-checks.txt` |
| Real WGC / long session | `build/quiet-tests/repair-wgc-visible-final/checks.txt`, its `local/PandaClip/logs/panda.log` |
| Real WGC decode | `build/repair-wgc-decoding.txt` |
| Real WGC Qt preview Stop / decode | `build/quiet-tests/repair-wgc-stop-checked/checks.txt`, `build/repair-stop-decoding.txt` |
| Fresh generated fixtures / UI MP4 decode | `build/repair-decoding-final.txt` |
| Frontend write failures, continued replay and retry | `build/quiet-tests/repair-frontend-fault-checked/checks.txt`, `build/repair-fault-decoding.txt` |
| Exact shipping presentation / executable hash | `build/quiet-tests/repair-final-executable`, `build/repair-release-sha256.txt` |
| Compressed replay, border/meter and logging component checks | `build/repair-components-final.txt` |
| Before-fix actual native presentation | `build/quiet-tests/repair-shipping-screenshot`, `build/quiet-tests/repair-compositor-check` |

Earlier failed artifacts are retained. `repair-complete-release` used an empty fixture directory and timed out waiting for Media; `repair-verified-release/debug` used obsolete flat-color fixtures that fail the current decoded-pixel/refraction assertions (and one transient appearance assertion). Fresh production-NVENC/AAC fixture generation and separate final runs replace those results. Earlier premature native-event assertions and a too-short UIA deadline were corrected in the harness; no failed overall run is represented as passing.

The first `repair-frontend-fault-final` attempt kept an unattached generated preview producer busy, so it exercised the no-frame timeout instead of the intended write hook. The corrected isolated fault run uses the existing retained owned image and verifies failure feedback, released controls, no false Saved state, two playable retries and healthy replay across a failed export. This was a harness setup correction, not evidence of a new real-WGC defect.

## Short manual acceptance check

1. Launch the exact Release below; enable Windows **Settings → Accessibility → Visual effects → Transparency effects** if you want desktop Acrylic, and leave PandaClip Reduce Transparency unchecked. Move/change background windows yourself; verify actual transmission, legible text and sharp opaque preview. Also try Reduce Transparency/Motion.
2. Select Geometry Dash through the whole top capsule, start/stop preview, accept/deny the real border permission, and check the actual visible outline. Record moving gameplay, enable replay, save, navigate Settings/Media and verify controls/timer continue. Check physical shortcut delivery/conflicts.
3. Choose/enable the intended microphone, Check Mic, speak, confirm the opened name and raw meter. Record Game-only/Mic-only/Both, toggle live mutes, stop and **listen separately to Mix/Game/Mic**. Check permission, endpoint mute/volume, physical disconnection/reconnection. No quiet test establishes audible speech, game sound or perceptual synchronization.
4. Physically drag/snap/resize the title bar, use minimize/maximize/restore and traverse 100%/150% displays. Test scrollbar dragging, wheel over editors and keyboard navigation. Close waits for backend drain; avoid launching two personal copies with competing global hotkeys.

## Launch and repeat verification

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\build\repair-v1\Release\PandaClip.exe'
```

Keep the entire deployed Release folder. VS Code **Ctrl+Shift+B** builds Debug; **F5 → PandaClip (Debug)** builds and debugs `build/repair-v1/Debug/PandaClip.exe`; **Run Task → CMake: Build Release** builds this Release. Test runners are separate, opt-in tasks.

```powershell
cmake -S . -B build\repair-v1 -G "Visual Studio 17 2022" -A x64 -DPANDACLIP_QT_FRONTEND=ON -DPANDACLIP_BUILD_REPLAY_STRESS=ON
cmake --build build\repair-v1 --config Debug --parallel
cmake --build build\repair-v1 --config Release --parallel
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\run_quiet_tests.ps1 -BuildDirectory build\repair-v1 -Configuration Release
```

For external shipping UI Automation, prepare a **fresh** project-local profile with `tests/prepare_qt_profile.py`, then invoke `PandaClipQtQuietLauncher.exe <PandaClip.exe> <profile> --shipping-ui`. `--active-smoke` captures only its owned passive shipping HWND, with bounded own-window compositor inspection; it never establishes Acrylic if Windows policy disables it. `--active-hidden-wgc-long` requires the verification executable, records its owned moving HWND, briefly displays its passive frontend for an external preview screenshot, and takes approximately 75 seconds. No runner drives Geometry Dash or plays a beep.
