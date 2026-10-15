# Historical notes

Archived October 9, 2026. Earlier build paths and frontend descriptions below are superseded. Use the root README for the production build. Original evidence is retained.

﻿# PandaClip

PandaClip is a native Windows capture app. It uses Windows Graphics Capture, Direct3D 11 GPU processing, NVIDIA NVENC H.264, and Windows Media Foundation AAC/MP4. Capture, encoding, and MP4 writing are PandaClip's own pipeline; it does not use OBS/libobs or wrap another recorder.

The current frontend uses **Qt 6 Widgets for the recording workspace and Qt Quick for a separate View Media window**. The recording engine remains PandaClip's own C++ implementation. Recording and replay share direct NVENC output; Mix and separate Game/Microphone AAC tracks are preserved. Trimming, automatic highlights, cloud uploads and noise-processing effects are not implemented.

## Standalone Glass Lab prototype

`build\glass-lab-desktop\Release\PandaClipGlassLab.exe` is the separate experimental desktop-optics prototype. It adds its own WGC monitor source with owned-window exclusion, synchronized GPU texture sharing, custom blur and outer-edge refraction, alongside Native Acrylic/Solid modes and prototype-only presets. It has no connection to PandaClip recording, replay or preferences. Earlier lab deployments and the running session are preserved. See [source evidence, launch, measurements and limitations](glass-lab-desktop.md), [earlier material comparison](glass-lab-clear.md) and [rendering investigation](glass-lab.md). The optional target remains off in ordinary builds. No material or desktop-capture integration into PandaClip is approved or implemented.

## Current frontend: consistent desktop materials

**Launch `build\desktop-style\Release\PandaClip.exe`.** Keep its deployed DLLs, plugins/QML and licenses beside it. VS Code tasks/F5 now use this build folder; the existing running `repair-v1` executable is preserved. See [actual deployed Capture/Media screenshots and verification results](desktop-style.md). The earlier [repair report](repair-v1.md) remains historical.

Capture retains its dashboard arrangement, source selector, three actions/five gears, split audio strip and visible gains. Both windows now share dark-purple materials, lavender rims, rounded controls and matching captions. View Media no longer paints a thumbnail across its window. Its library background uses the same Windows compositor helper as Capture; playback controls retain bounded local decoded-video sampling.

**Windows Transparency effects is currently off on this PC.** Both tested windows show the matching purple solid fallback. Use **Open Windows transparency settings** in Capture, Media or Appearance, then enable Transparency effects yourself and leave PandaClip Reduce Transparency unchecked. PandaClip never changes your global preference. Live desktop transmission is blocked/unverified here; no desktop refraction is implemented or claimed.

### Navigation and controls

- **Capture** is the default dashboard: centered selected-window capsule, sharp native Direct3D preview, one split audio strip, floating recording/replay/save controls and compact status. The new reference guides proportions, outline icons, spacing, restrained neutral rims and selections. Captured pixels receive no decorative grading.
- Click the **selected-source bar** to select/refresh a window, start/stop preview capture, inspect borderless status or cancel a pending start. Recording and Replay start required capture automatically. Hide preview affects presentation only.
- **Start/Stop Recording**, **Enable/Stop Replay Buffer** and **Save Clip** each have their own gear. Game audio and Microphone each have a gear and mute control: **five independent gears**. Opening one does not start/stop encoding.
- **View Media**, **Settings** and **Diagnostics** retain their separate views. Closing Media does not stop recording/replay. The title-bar **…** menu retains the existing menus, layout reset/lock and optional **Advanced dock layout**. Default Capture has no Sources dock or dock headers.
- Layout remains in `%LOCALAPPDATA%\PandaClip\workspace-layout-v1.txt`, separately from recording preferences. Old dock layouts do not override the initial dashboard. Smaller windows use icon navigation and scrollable quick popovers.

### Feature-specific Quick Settings

| Gear | Existing settings/actions |
| --- | --- |
| Recording | Resolution, 30/60/120 FPS, NVENC quality, Mix + separate/Mix only/Separate only tracks, output folder, Advanced Recording |
| Replay | Custom **5–600 seconds**, RAM cap **128–4096 MiB**, available history/memory, shared quality, Advanced Replay |
| Save Clip | The **same duration** as Replay, common output folder, custom save shortcut, existing advanced save options |
| Game audio | Enable, Off/Selected app/explicit Whole system, window/system endpoint, gain/mute, refresh, Advanced Audio |
| Microphone | Enable, Windows default recording input or named device, gain/mute, Check Mic, refresh, opened-device/error status, Advanced Audio |

One popup opens at a time. Placement fits the available window/screen, flips or shortens the viewport around its opener, and supports Tab, Escape, outside-click dismissal and focus restoration. Wheel input over choices/numbers scrolls instead of changing values. Apply and Advanced use the same settings model. Quick encoder controls are locked while recording/replay is busy. Source/device/gain changes follow existing session boundaries; recording mute is live. Popovers explain pending changes and replay resets.

Meters retain coherent floating-point raw PCM RMS/peak, before recording gain/mute. Check Mic uses the pinned selected endpoint on the existing WASAPI worker, without an encoder or speaker monitoring. Opened names and Windows input volume/mute/errors remain available. Nonzero samples do not prove audible speech. Game audio never silently falls back to whole-system capture.

### Desktop backdrop

Capture, View Media, Settings, Diagnostics and the five quick popovers share the existing **DWM Desktop Acrylic** helper (`DWMSBT_TRANSIENTWINDOW`). Windows supplies the scene behind each individual window; translucent GPU tint/rim layers leave text, thumbnails, controls, meters and native video sharp. No full-window media image, wallpaper, desktop screenshot/capture texture or whole-window opacity is used. Media playback's small control strip can sample its own decoded video, independently of the outer desktop background.

Desktop Acrylic requires Windows 11 build 22621+ and effective compositor/transparency policy. Windows transparency off, high contrast, PandaClip **Reduce Transparency** or unavailable backdrop support produce a matching purple solid fallback. Appearance and Diagnostics show detailed reasons; Capture/Media expose **Open Windows transparency settings** when Windows disables transparency. Native HWND policy notifications trigger a coalesced refresh without restarting capture or encoding. Reduce Motion remains supported. Native menus and detached advanced docks stay solid.

**Desktop transmission remains unverified.** The exact deployed Release windows were inspected externally over blue and red owned backgrounds at 100% and 150%, including move/resize and opening Media. Since `EnableTransparency=0`, their fallback correctly stayed unchanged. API success, foreground alpha and internal QML images cannot establish desktop blur. Enable the OS preference yourself to perform the active-window check below; Acrylic is blur/tint, not optical desktop refraction.

Implementation follows [Microsoft DWM backdrop types](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type), [Windows settings URIs](https://learn.microsoft.com/en-us/windows/apps/develop/launch/launch-settings), [frame integration](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmextendframeintoclientarea) and [Qt Quick Widget transparency](https://doc.qt.io/qt-6.8/qquickwidget.html).

### Build and VS Code

Existing dependencies remain: MSVC 2022 Desktop C++, Windows SDK, CMake 3.25+, NVIDIA driver/Video Codec SDK and dynamic Qt 6.8.3 MSVC 2022 x64. Qt notices, LGPL texts and matching unmodified sources are retained; no GPL-only runtime module was added. Qt Multimedia/FFmpeg supplies playback; capture/encoding remain PandaClip's WGC/D3D11/direct NVENC/WASAPI/Media Foundation implementation.

```powershell
py -3 tools\install_qt.py
py -3 tools\qt_sources.py
cmake -S . -B build\desktop-style -G "Visual Studio 17 2022" -A x64 -DPANDACLIP_QT_FRONTEND=ON -DPANDACLIP_BUILD_REPLAY_STRESS=ON
cmake --build build\desktop-style --config Debug --parallel
cmake --build build\desktop-style --config Release --parallel
```

**Ctrl+Shift+B** builds Debug. **F5 > PandaClip (Debug)** builds/debugs the dashboard. **Terminal > Run Task > CMake: Build Release** builds the Release executable above. Build/F5 does not run tests. CMake deploys the runtime alongside the executable; keep that whole folder. Other PCs require the [Microsoft x64 C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe), NVIDIA driver and Windows media components. Debug requires the development runtime.

Quiet verification uses owned inactive desktops and project-local profiles/output, without physical input, foreground activation, audible stimuli or control of personal sessions:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\run_quiet_tests.ps1 -Configuration Release
```

Focused checks passed in Debug, Release and Release at 150%: **377 frontend assertions per full run**, recording/replay/shared exports, live mute atomics, raw meter delivery, five gears, scroll/reachability/preferences, media playback/tracks and native caption/client geometry. Controlled write-failure retries passed 17 assertions. **47 final-run MP4 files decoded successfully** (23 newly produced files plus 24 copied fixtures). An owned moving-window WGC run produced 66.500 s recording, 59.100 s same-session replay and 3.133 s warm-up replay at 1280x720/30 FPS. These are not gameplay performance or listening results. [The report](desktop-style.md) separates exact shipping screenshots, verification-target checks and failed harness runs.

Manual acceptance:

- Open Windows transparency settings yourself, enable its effect, keep Reduce Transparency off, then move Capture and Media over differently colored browser/editor windows. Confirm their own live scene changes through blur, while text/preview remain sharp. Also check solid/reduced-motion/high-contrast modes. Windows can still choose solid Acrylic while a window is inactive or under other compositor policy.
- Physically drag, resize, snap, minimize/maximize and use sliders/five gears at your normal scale and 150%. Quiet checks use targeted events on an inactive desktop; they do not establish physical input behavior.
- Select the intended microphone, speak during Check Mic, record Game-only/Mic-only/Both and listen separately to Mix/Game/Mic. Test physical input disconnection and real game sound. Synthetic PCM proves conversion/encoding/control routing, not audible speech or application-loopback sound.
- Record continuously moving Geometry Dash; compare capture-off versus replay-on game frame times and inspect exported smoothness. Owned moving-window cadence is not a GD benchmark. Real borderless consent, perceptual synchronization and physical hotkey delivery remain manual checks.

### Record or save a Geometry Dash clip

1. Launch the new Release build. Click the source bar, refresh and select Geometry Dash.
2. Open the Recording gear: 1920×1080, 60 FPS, Balanced P4, desired track layout/output folder; Apply.
3. Enable **Selected app** through Game audio. If wanted, choose/enable the exact mic, Check Mic, confirm the opened name and inspect Windows volume/mute and raw activity. Check PandaClip gain/mute separately.
4. Click Start Recording or Enable Replay Buffer. Borderless consent remains asynchronous; denial/failure allows outlined capture. **Capture source > Cancel Start** cancels a pending request. Allowed alone does not prove the outline is absent.
5. Set duration/RAM through the Replay or Save Clip gear. Wait for ready history, then Save Clip or the configured hotkey (default **Ctrl+Shift+F9**). Resolve reported conflicts. Export uses available keyframe/AAC-aligned history without restarting encoding.
6. Wait for successful finalization/Saved. Open View Media and listen to Mix/Game/Mic. Stop Recording/Replay before incompatible encoder/source changes.

**Roadmap only:** Full and Lite will share one recording core; Lite focuses on clipping with minimal interface/resource use. Later scenes/sources and audio routing are planned. Streaming, direct multistreaming and Linux support are planned for v2. This pass adds no Lite packaging, scenes, streaming, Linux backend, logo redesign or trimming.

## Archived shared-glass dock frontend (superseded)

The following launch paths and UI measurements describe the previous build. Use the dashboard instructions above. Historical evidence is retained in [shared-glass.md](shared-glass.md).

**Launch `build\shared-glass\Release\PandaClip.exe`. Keep its DLLs, QML, platform plugins and license folders alongside it.** This is the current Release output. Older builds remain intact; they do not acquire the new interface while running. Personal capture sessions were not closed or controlled during this work.

### Recording workspace

The main window has File/View/Docks/Tools/Help menus, a native Direct3D 11 preview, Sources on the left, Audio Mixer across the bottom, Controls at the bottom right, and a compact status bar. The restrained violet-charcoal palette uses lavender selections, soft dock-header lighting and quiet separators. Sources uses a compact list, truncated names with full tooltips and nearby actions. Mixer gain/mute controls are aligned, with explicit **Muted** states. Red remains reserved for recording/errors. Main caption/buttons, menus/status bar, dock panels/headers/controls, Settings and Diagnostics now share View Media's GlassPanel, GPU shader and palette tokens. Transparent widget foregrounds sit above bounded local samples of a restrained application backdrop. View Media samples its own thumbnails and decoded video. Preview and recorded pixels are not graded or distorted.

- Drag dock title bars to rearrange or float them; drag separators to resize. Use **Docks** to restore hidden panels, **Reset Layout** for the starting arrangement, or **Lock Layout** to prevent moving/floating/separator dragging. Main-window resizing remains available when locked.
- Layout and floating-window geometry persist separately in `%LOCALAPPDATA%\PandaClip\workspace-layout-v1.txt`. Recording/audio preferences remain in `settings-v1.txt`; layout saves cannot overwrite them.
- Sources lists the selected window, game audio and microphone. Use **Source Properties** or double-click a row. Full titles/statuses are available in tooltips. Source configuration retains the existing session boundaries and endpoint pinning.
- Game/Microphone strips have floating-point raw RMS bars and peak markers from -90 to 0 dBFS, gain, live recording mute, and Properties. **Check mic** opens the selected endpoint on its existing WASAPI worker without creating a recording; the opened endpoint and detailed input errors are shown in properties/tooltips. Nonzero samples do not prove audible speech.
- Recording/replay buttons change with their states. Save Clip explains unavailable history or pending exports in its tooltip; elapsed time, available history, memory in the history tooltip, and save progress remain visible. Finalizing disables another stop action. Detailed counters and report export are in **Tools > Diagnostics**.
- **View > Enable preview** controls presentation independently of recording. Preview uses the existing GPU renderer and aspect-preserving fit; no preview frame is copied through Qt images in the shipping app.

### Separate View Media

**View Media** opens its own resizable window. It has large thumbnail cards, search, Newest/Oldest/Name/Duration sorting, All/Replay/Recording/Has audio/Unreadable filters, metadata, playback, seek, volume/mute, fullscreen, audio-track selection, Open Folder and External player. Enter opens the focused grid item, Space toggles playback, and Escape leaves fullscreen or returns to the library. Closing this window stops only playback; it leaves recording/replay running.

The material is a **Windows adaptation inspired by Liquid Glass**, following [Apple's official session](https://developer.apple.com/videos/play/wwdc2025/219/) and [materials guidance](https://developer.apple.com/design/human-interface-guidelines/materials), not native Apple material. Each floating toolbar/menu samples the content at its own current position through a bounded half-resolution `ShaderEffectSource`. Qt Quick blur and a compiled GPU shader add adaptive smoked-violet tint, rounded masking, uneven curved rim reflections, restrained 2.6-pixel edge refraction and elevation shadows. Playback controls sample the **actual decoded video underneath**, while video and thumbnails stay sharp. Position mapping is recomputed after coalesced layout/frame changes; sampling stops when content is unchanged or glass is disabled. There is no desktop capture or preview readback for glass. Empty states have quiet broad violet tonal variation.

**Settings > Appearance > Reduce transparency** selects readable solid surfaces in both windows. **Reduce motion** disables short hover/selection/menu transitions; there is no constant decorative animation. Both preferences persist independently of the Windows desktop setting.

Metadata/thumbnail decoding stays on the existing library worker. The catalog is capped at 2,000 entries, thumbnail retention at 256 images (320 by 180), and the image-provider cache at 64 images. Grid delegates are reused with a bounded viewport cache. Playback now uses Qt Multimedia's FFmpeg backend and `VideoOutput` in the **same GPU scene** as the controls, replacing the native child playback surface. Quiet captures verified RHI texture frames with zero CPU-frame handles in the observed sample, actual video pixels and locally sampled glass together. Shipping playback performs no custom frame mapping/readback or full-window CPU blur. Backend/driver behavior on other PCs remains a separate check. FFmpeg is used only to play saved files; recording/replay encoding and muxing remain PandaClip's own direct NVENC and Media Foundation pipeline.

### Settings and engine integration

Settings is a separate categorized window: General, Capture, Recording, Replay, Audio, Keybinds and Appearance. Each category has one moving `QScrollArea` content widget. Wheel input over closed dropdowns, numeric edits, gain sliders and shortcut editors goes to the viewport; it does not change values. Qt provides the scrollbars, keyboard navigation and resize layout. Device lists come from the existing background catalog, with explicit refresh/device-change updates. Polling copies cached state, and the existing 750 ms background settings writer debounces edits. Apply retains next-session rules for expensive changes.

All visible presentation was migrated. A hidden Win32 controller still owns the existing lifecycle, validated settings, registered hotkeys, recovery notifications and worker events; its old controls are internal compatibility state, not a visible workspace. The frontend adapter keeps this migration separate from the recording engine. Qt drives its health polling explicitly; WGC frame notifications target the controller, not the Qt preview's root window. Closing waits for drain/finalization, then accepts the final Qt close. This fixed a shutdown loop found in the first isolated run.

### Controls, meters and custom window chrome

- Start Recording / Enable Replay validate the selected window before consent. Pending starts disable duplicate buttons, show their state, and can be cancelled. Recording/replay auto-start the selected capture; preview visibility remains independent.
- Meters read captured stereo PCM statistics directly, bypassing hidden progress controls. Floating-point RMS retains quiet input below the old 1/1000 threshold. A real-sample peak and 300 ms release envelope retain short bursts between UI polls. Gain/mute do not animate or suppress the raw-input meter; they affect recording. Check Mic works even when mic recording is disabled. Main labels distinguish Disabled, Capture inactive, Device unavailable/opening, Open · silent and Signal detected. The full opened endpoint and Windows mute/volume remain in the tooltip/Audio properties. Nonzero input is not proof of speech.
- Preview alone does not open game audio. Start recording or replay to use the selected process-loopback/explicit whole-system mode and see game levels. Neither mode nor microphone is silently changed.
- Main, Settings, Diagnostics and View Media have custom icon/title and accessible minimize/maximize/restore/close controls. Native Windows styles, physical edge/corner/caption hit tests, work-area maximization and system commands are retained. The maximize target advertises `HTMAXBUTTON` for Windows snap layouts. Physical snap flyouts, dragging, monitor transitions and Alt+Space still need active-desktop checks.
- The main material is one stable Qt Quick GPU layer per workspace/dialog using the **same GlassPanel and compiled shader as View Media**, including half-resolution backdrop sampling, blur, 2.6 px edge refraction, adaptive smoked tint, rim reflections and shadows. It covers dock panels, headers, controls, menus/status bar and dialogs, with real widgets retaining input and docking. Main surfaces sample the application backdrop; Media samples actual thumbnails/video. There is no preview/desktop capture or CPU video blur. Detached docks and native popup menus use the consistent solid fallback: Qt 6.8 crashed when a Quick-backed floating dock returned to the hierarchy. Reduced transparency disables sampling; reduced motion remains preserved.

### Dependencies, setup and deployment

Keep the existing MSVC 2022 Desktop C++ workload, Windows SDK, CMake 3.25+, NVIDIA driver and vendored Video Codec SDK requirements below. Add **dynamic Qt 6.8.3 MSVC 2022 x64**, Widgets/QML/Quick/Quick Widgets/Quick Controls/Effects, Multimedia and shader tools. The Multimedia package supplies dynamically linked FFmpeg 7.1 playback libraries. Python 3 is used by project-local setup/verification scripts; no global Qt installation or PATH change is required.

From a PowerShell terminal in PandaClip:

```powershell
py -3 tools\install_qt.py
py -3 tools\qt_sources.py
cmake -S . -B build\shared-glass -G "Visual Studio 17 2022" -A x64 -DPANDACLIP_QT_FRONTEND=ON -DPANDACLIP_BUILD_REPLAY_STRESS=ON
cmake --build build\shared-glass --config Debug --parallel
cmake --build build\shared-glass --config Release --parallel
& .\build\shared-glass\Release\PandaClip.exe
```

The SDK is installed only under `third_party\Qt\6.8.3\msvc2022_64`. The installer uses aqt 3.3.0 with HTTPS/checksum validation and a project-local fix for ANSI qmake output in Hebrew paths; a relative `qt.conf` handles relocation. The installed SDK/tool version was verified; the new installer script's already-installed validation was run. Its complete cold-install path was not independently repeated.

CMake's default `PANDACLIP_QT_FRONTEND=ON` adds the Qt frontend, AUTOMOC and embedded QML resources. Its post-build step uses [Qt's `windeployqt` workflow](https://doc.qt.io/qt-6/windows-deployment.html), copies required dynamic libraries/plugins/QML modules, and includes notices and matching source archives. Keep the entire Release folder when moving the executable. On another PC install Microsoft's official [x64 Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe), the NVIDIA driver and any required Windows Media Feature Pack; individual compiler runtime DLLs are not harvested from system folders. Debug requires its MSVC debug runtime/development environment.

Qt is dynamically linked under **LGPLv3**; no GPL-only Qt runtime module is used. Playback FFmpeg 7.1 is dynamically linked under **LGPL2.1 or later**, built without GPL/nonfree features. [Qt licensing](https://doc.qt.io/qt-6/licensing.html) and [LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations) informed this choice. Help > About, `QT-NOTICE.txt`, license texts, observed FFmpeg build configuration and SHA-256-verified matching unmodified Qt/FFmpeg source archives are included under `licenses\Qt`. Compatible DLL/plugin replacement is permitted. Development shader tools are not recording dependencies. MSIX staging carries the multimedia plugins and notices; the prior package passed validation before this playback change. A new package has not been signed/installed or tested for borderless consent.

### VS Code

The task labels and C++ debugger configuration are retained. **Ctrl+Shift+B** builds Debug; **F5 > PandaClip (Debug)** builds and starts the new frontend. Both now use the dedicated `build\shared-glass` directory so they do not overwrite the older running Release executable. Task names, build shortcuts and debugger behavior are retained. **Terminal > Run Task > CMake: Build Release** rebuilds the launch path at the top. Build/F5 do not run tests or test stimuli.

### Verification and screenshots

Run **Verify: Quiet (Release)** or:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\run_quiet_tests.ps1 -BuildDirectory build\shared-glass -Configuration Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\run_quiet_tests.ps1 -BuildDirectory build\shared-glass -Configuration Debug
```

Qt verification targets actual widgets on an owned inactive desktop. It uses no input injection, desktop switching, foreground activation or audible playback. Generated GPU/PCM sources are compiled only into `PandaClipQuietApp`; shipping `PandaClip.exe` has no fixture source. Check Mic quietly opens the explicitly selected endpoint using the real WASAPI monitor, without encoding or speaker playback. A production hotkey registration/conflict probe runs only on a proven inactive owned desktop and sends no keys. A separate shipping smoke test uses the actual executable with SDK/Qt search paths removed and confirms a visible Qt workspace and clean exit.

The detailed current checks, measured timings and actual screenshots are in [frontend verification](frontend-verification.md). The source engine/component tests remain separate from UI checks. Do not run archived Win32 UI automation against the new Qt frontend's hidden controller.

**October 6 refinement:** Debug and Release full quiet suites passed, followed by Release material/render-cost and 150% process-local scaling checks. Actual before/after captures include decoded video and its glass toolbar. A paired four-second fixture run measured GPU draw sums of 82.04 ms with glass / 67.57 ms solid; these are local playback measurements, not game-frame latency or GD overhead. See [the full scope, timings, screenshots and manual checklist](purple-refinement.md).

**Current shared-glass rebuild:** See [the shared-material report](shared-glass.md) for functional fixes, Debug/Release/frontend/decode results, visual comparisons, GPU costs and remaining manual checks. Both final full quiet suites passed with zero frontend failures; a separate Release 150% scaling run also passed. A retained WGC image now seeds encoder startup if the compositor has stopped sending changes; in-flight publication rechecks the attached recorder. First-video waits are bounded to 10 seconds after encoder/audio initialization. Startup snapshots are identified separately from fresh WGC callbacks. Client title-bar input now explicitly invokes Qt system movement and double-click maximize/restore; supported frameless flags also correct the previous Qt/native caption-margin mismatch. The inactive-desktop moving-WGC attempt was blocked by "no capturable client area" and is not counted as a passing capture check. Older results remain in [frontend fixes](frontend-fixes.md).

**Still manual:** speak into the selected mic and listen to Mix/Game/Mic; microphone unplug/reconnect; real application-loopback/WGC/Windows border consent and visible outline; active moving Geometry Dash capture/frame times; physical hotkey delivery/conflicts on the input desktop; dock/scrollbar dragging and physical multi-monitor/DPI transitions. Inspect hover/focus/material appearance on your active display. Current playback screenshots include both decoded video and glass; main-preview screenshot readback remains verification-only. Quiet fixture timings are not gameplay performance or listening evidence. See the [final screenshots, measured costs and exact verification scope](shared-glass.md).

### Record or save your first Geometry Dash clip

1. Launch the Release executable at the top. In **Sources > Source Properties > Capture**, refresh and select the Geometry Dash window. Stop Capture first if switching an active source.
2. In **Settings > General**, select the output folder. In Recording, start with 1920×1080, 60 FPS and Balanced P4. Apply settings.
3. In **Audio**, choose selected-application game audio explicitly; whole-system audio is optional. Choose Default device or a named microphone and enable it only if wanted. Use **Check mic**, confirm **Open:** names/statuses and raw meter activity, then check Windows input mute/volume and PandaClip gain/mute.
4. Click **Start Recording**; it starts capture automatically, even with preview disabled. Windows borderless consent is asynchronous. Denial/failure keeps the outline and allows capture. A pending request offers **Sources > Cancel Start** and times out after 30 seconds; Capture properties allow another request. No selected window opens Capture properties with an actionable message. Permission success does not prove the outline is absent.
5. For replay, click **Enable Replay** and wait for history. Click **Save Clip**, or set/apply the Save Clip hotkey in Keybinds (default Ctrl+Shift+F9). Resolve any displayed conflict with an older app instance. Save uses the available keyframe-aligned history.
6. Wait for saved/finalized status. Open **View Media**, refresh if needed, open the clip, and listen to each audio track. Stop Recording/Disable Replay before changing encoder/source configuration for the next session.

## Archived Win32 interface refresh and quiet verification (superseded)

The following dated UI results describe the previous frontend, not the current Qt presentation. Engine behavior and historical measurements are retained for reference. Use the current launch/setup/verification instructions above.

## Interface refresh and quiet verification (October 5, 2026)

The native C++/Win32 presentation remains; there is no new runtime framework or recording dependency. Capture has a larger central preview, a compact two-source audio mixer (enable/device/mode, mute, gain and raw meters), a persistent recording/replay control dock, clip history/save status and elapsed time. **Audio mixer**, **Source details**, and **Hide preview** reduce secondary information; Source details shows actual opened devices and borderless status. In compact windows expanded source details replace the preview area; this never stops capture/encoding. Gain/source changes retain the existing next-session rules; mutes are live. Controls have matching cyan accents, rounded borders and tooltips.

**Clips** uses thumbnail cards with filename/type/date/duration/format/size, search, sorting, Replay/Recording/Has audio/Unreadable filters, and a large player with seek, mute/volume, fullscreen and audio-track choice. The background library and bounded thumbnail cache are preserved. **Settings** provides General, Capture, Recording, Replay, Audio, Keybinds, Appearance and All settings categories. Apply and validation/pending-session status stay outside the scroll viewport. The existing one-window scrolling implementation remains; wheel input over sticky category controls also scrolls rather than changing values. Defaults remain 1920x1080/60 FPS, Balanced P4, VBR 20/35 Mbps, 60 s/512 MiB replay, 100% gain, zero sync adjustment, Game selected-application enabled, mic disabled, Mix plus enabled stems, preview 30 FPS. Use Restore Defaults to restore configuration without deleting clips.

**Appearance** persists separate transparency and animation switches (both on by default). Windows 11 22621+ is asked for its supported Mica Alt backdrop in the header; unsupported Windows, disabled composition/transparency, accessibility/power policy, or the switch off use solid surfaces. Readable panels stay opaque. There is no continuously rendered blur over video. Hover changes animate only the affected button for approximately 80 ms, then stop; disabled animation changes state immediately. Requesting the native effect does not establish that Windows visibly used it. Inactive-desktop PrintWindow screenshots can omit native backdrop/GPU playback and are not proof of those visuals on the input desktop. See [Microsoft system backdrops](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type).

### Quiet mode (default)

Normal configure/build/F5 tasks do not run a stimulus or any tests. In VS Code use **Terminal > Run Task > Verify: Quiet (Release)**, or from this workspace:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\run_quiet_tests.ps1
# Debug uses the same isolated verification directory and source:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\run_quiet_tests.ps1 -Configuration Debug
```

This builds optional tools into `build\interface-refresh`, then saves transcripts, reports, screenshots and media under a new `build\quiet-tests\run-*` directory. It never moves the pointer, sends global keys, activates windows, switches the input desktop, plays a beep, opens Explorer/log viewers, controls GD, or modifies a personal PandaClip profile/session. Device discovery is read-only. Hotkey registration and tray icons are disabled only in the isolated test process. MFPlay is muted **before** creating/starting media, so checking its track controls does not play fixture audio. UI API calls have a three-second timeout and owned shutdown has a fifteen-second budget; a failed owned worker can leave an honestly incomplete `.partial.mp4`, never a claimed Saved file. The runner never kills a personal process.

`PandaClipQuietMediaFixture` sends known video colors/white flashes and generated stereo PCM cues through the actual direct NVENC writer, AAC encoders and compressed replay exporter. It covers video-only, Game, Mic and Both track combinations, then independently decodes every stream. These are **component fixtures**, not game/WASAPI/microphone acquisition or perceived speaker sync. `PandaClipAacDelayProbe` separately checks PCM sync offsets and production AAC encoding. Replay byte/duration/snapshot stress, simulated border policy, logging overflow/write failure, and timestamp-comparison regressions stay separate.

`PandaClipQuietUiTest` starts only its owned app/source on a private **inactive desktop**, without SwitchDesktop or physical input. It targets controls through window APIs and PrintWindow, tests navigation/categories/wheel routing/thumb-position/keyboard scroll/resize/value retention/audio mirrors/gallery filters/silent playback, and exports diagnostics. Actual pointer dragging and user keyboard input are not simulated by a second cursor. If desktop isolation is unavailable, it reports a skip/error; it does not quietly fall back to foreground automation. WGC capture of that desktop returned no capturable client area during investigation. Default verification now skips real WGC and borderless consent entirely, preventing a Windows permission dialog; simulated border-policy tests remain separate. The optional `--hidden` runner argument uses an explicitly hidden owned window and targeted APIs; it does not show or activate that app.

`PandaClipQuietApp` is a **test-only target**, with the same UI/recording worker plus an owned, bounded generated GPU source. It exercises real recording, replay, shared encoder output and finalization during navigation when WGC cannot capture the inactive desktop. Its headless D3D context enables multithread protection before use on producer/NVENC threads. The generated-source controls are not compiled into `PandaClip.exe`. Reports from that path are generated-source evidence, not WGC callback cadence or GD smoothness. A successful component or generated-source test does not validate real WASAPI acquisition or end-to-end synchronization.

### Interactive mode (explicit opt-in)

Older integration scripts and `replay_sync_stimulus.ps1` now skip unless called with **-Interactive** (opted-in wrappers pass the switch to their child runners). They may show/activate **owned test windows**, play scheduled beeps and, for the Settings pointer runner, move the physical pointer/use desktop input. They are not ordinary builds or quiet verification. Run only when you are ready to stop using the desktop and hear test audio:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\run_replay_sync_test.ps1 -Interactive
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\run_settings_ui_checks.ps1 -Interactive -Executable 'build\interface-refresh\Release\PandaClip.exe'
```

Legacy runners retain their original build-directory arguments/assumptions; use their `Executable`/`BuildDirectory` options where available. The reliability runner now locates windows by **owned PID**, including reopened sources, so an interactive run cannot accidentally choose a personal PandaClip by class/title. Preserve wanted clips and let your personal app finalize normally before updating its executable. A muted source is not a real application-loopback audio test. Real WGC, real WASAPI application loopback, audible mic input, microphone unplugging/reconnection, listening to Mix/Game/Mic, speaker flash/beep sync, live glass appearance/hover, physical thumb dragging, other DPI/multi-monitor transitions and active GD require separate checks. Trimming remains deferred.

### Verified results for this refresh

**Built:** standard `build/Debug/PandaClip.exe`, dedicated Debug, and `build/interface-refresh/Release/PandaClip.exe`. Final MSVC build logs had no warnings/errors. The standard Release was locked by your running copy; neither personal Release session was closed or controlled. The updated executable is the alternate Release path above. F5 and existing build tasks remain intact.

**Quiet automated checks passed in Debug and Release:** navigation and all category ranges; wheel over edit/dropdown/sticky category without changing dropdown values; thumb-position API; Home/End/PageUp/PageDown; 875x775 and 1450x1000 resizing at actual 125% scaling; numeric retention; no settings-file writes from scrolling; mixer gain/mute mirrors; preview/section toggles; Appearance solid/off-animation options; gallery discovery/search/sorting/audio filter; silent playback with three track choices; diagnostic file export; and owned shutdown with zero failed assertions. Search/sorting checks cover matching counts, not an exhaustive metadata-order audit.

The generated-source UI runs separately passed real worker recording, replay export, and a shared ordinary/replay encoder while navigating pages. All four newly finalized worker files appeared in the gallery. Every generated ordinary/replay MP4 decoded independently. The direct GPU/PCM fixture covered video-only, Game-only, Mic-only and Both: 1280x720 H.264 at exact 30 FPS, 48 kHz stereo AAC, and 0/2/2/3 AAC streams (Mix plus enabled stems). Ordinary fixtures were 6.0 seconds; requested 5-second replay snapshots yielded 4.0 seconds at the available IDR. AAC packets were monotonic/contiguous. Component cue offsets were approximately 1.00-1.67 ms; this measures generated PCM/AAC/mux/export, **not real WASAPI/WGC or speaker synchronization**. No beep was sent to an output device.

Additional component checks passed atomic Unicode settings save/reload and damaged-file fallback; asynchronous metadata/thumbnails; incremental save notification and explicit refresh; paused seek and all three audio-track switches preserving position; missing playback/folder errors; replay byte/duration/retained-snapshot caps; simulated Allowed/Denied border policy across four recreated sessions; bounded logging overflow and owned-folder write failure; AAC delay/sync-adjustment probes; and all 12 timestamp-comparator regressions. Permission policy simulation does not verify actual Windows consent or the visible outline.

**UI timing:** the same PC, 1200x960 physical window, actual DPI 120 (125%), private inactive desktop, 80 alternating thumb-position/wheel pairs. Numbers measure synchronous wheel-message handling, not pointer latency, rendered frame times, native backdrop cost, or gameplay. Paired idle Release runs used the previous working Settings-fix executable and the updated executable:

| Pair | Previous median / p95 (ms) | Updated median / p95 (ms) |
| --- | --- | --- |
| 1 | 3.53 / 7.12 | 3.59 / 5.59 |
| 2 | 3.14 / 5.44 | 3.21 / 4.98 |
| 3 | 3.87 / 7.67 | 3.67 / 7.14 |

The paired results do not show a large Settings slowdown; these short samples do not establish an improvement or a worst-case guarantee. The final full Release suite measured idle 4.42 ms median / 9.40 ms p95. Generated-worker results from that pass:

| Mode | Median (ms) | p95 (ms) |
| --- | --- | --- |
| Recording only | 3.44 | 8.81 |
| Replay only | 2.45 | 5.54 |
| Recording + replay | 2.35 | 4.87 |

An earlier refresh pass measured 5.62 ms idle median and 15.90 ms p95 with both generated workers enabled. Run-to-run variation is retained here rather than presenting only the quickest result. Source code uses native backdrop requests and short affected-control hover timers; it adds no continuous blur pass. Inactive-desktop measurements do not verify visible glass/hover effects or GD overhead.

**Screenshots inspected:** [Capture](build/quiet-tests/run-20261005-215115-Release/ui/screenshots/capture.png), [Clips](build/quiet-tests/run-20261005-215115-Release/ui/screenshots/clips.png), [Settings](build/quiet-tests/run-20261005-215115-Release/ui/screenshots/settings.png), [Diagnostics](build/quiet-tests/run-20261005-215115-Release/ui/screenshots/diagnostics.png), [compact Capture](build/quiet-tests/run-20261005-215115-Release/ui/screenshots/capture-small.png), [compact source details](build/quiet-tests/run-20261005-215115-Release/ui/screenshots/source-details-small.png), [compact Clips](build/quiet-tests/run-20261005-215115-Release/ui/screenshots/clips-small.png), and [compact Settings](build/quiet-tests/run-20261005-215115-Release/ui/screenshots/settings-small.png). Corrected clipped playback/source labels, card metadata, footer placement, and compact gallery status. The dark fixture thumbnails reflect their real first frame; they are not gameplay screenshots. PrintWindow may omit GPU video/native backdrop on the inactive desktop.

**Evidence:** Release `build/quiet-tests/run-20261005-215115-Release`, Debug `build/quiet-tests/run-20261005-215228-Debug`; final transcripts/build logs in `build/interface-refresh/quiet-release-final.txt`, `quiet-debug-final.txt`, `release-delivery-build.txt`, `debug-delivery-build.txt`, `standard-debug-delivery-build.txt`; paired measurements `build/interface-refresh/paired-idle/comparison.csv`. PowerShell test scripts parsed successfully, and calling the legacy Settings runner without `-Interactive` printed SKIPPED before running automation. No interactive runner was executed for this refresh. Early test-only GPU fixture failures were retained honestly in their owned build folders; enabling D3D multithread protection fixed that fixture's threaded stall, and the final suites passed. This was not a demonstrated shipping capture-engine fault.

**Checks still requiring you:** normal input-desktop WGC preview/record/replay and border consent/visible outline; actual application-loopback audio; audible microphone speech and physical reconnect; listening to Mix/Game/Microphone; speaker-to-flash synchronization; native glass/hover on/off; physical scrollbar dragging and Tab navigation; other DPI/multi-monitor transitions; large real libraries; tray/global shortcut operation in the refreshed app; and active moving GD frame-time/smoothness comparisons. Quiet tests intentionally suppress global hotkeys/tray in their own processes and cannot validate those integrations. Earlier milestones' interactive/performance/synchronization results below remain historical. No trimming or recorder dependency was added.

## Requirements

- Windows 11 recommended. Windows Graphics Capture needs Windows 10 build 18362 or later. Selected-application WASAPI loopback needs Windows 10 build 20348 or later.
- Visual Studio 2022 or Build Tools with **Desktop development with C++**, MSVC v143 x64 tools, and Windows 10/11 SDK (10.0.26100 recommended).
- CMake 3.25+ and VS Code. Install Microsoft's **C/C++** VS Code extension for F5 debugging: `code --install-extension ms-vscode.cpptools`.
- NVIDIA GPU and current driver with H.264 NVENC support and `nvEncodeAPI64.dll`.
- Windows Media Foundation AAC encoder and MP4 Sink Writer (included with Windows). Select an active microphone only if mic input is wanted.
- Python 3 is optional for the timestamp comparison and manual PresentMon CSV summary scripts (`py -3`). It is not used by the app or its C++ build.

The NVIDIA Video Codec SDK 13.1 NVENC header is included under `third_party`; attribution and license terms are in `third_party/NVIDIA/VideoCodecSDK/NOTICE.md`. No CUDA Toolkit, FFmpeg, OBS, or external muxer is used.

## Archived Win32 build and launch instructions

1. Open this `PandaClip` folder in VS Code (**File > Open Folder...**).
2. Install the Microsoft C/C++ extension if it is missing.
3. Press **Ctrl+Shift+B** and choose **CMake: Build Debug**. This configures/builds the Debug target in `build`.
4. Press **F5** and choose **PandaClip (Debug)**. The launch configuration runs the build task first.
5. To use Release, run **Terminal > Run Task > CMake: Build Release**, then start `build\Release\PandaClip.exe`.

Close any running copy of the executable being rebuilt first, and let it finalize accepted saves. MSVC reports LNK1168 if that EXE is still in use. The existing VS Code tasks and F5 configuration are preserved. The interface refresh updates the standard Debug target. Both of your older Release copies were running and were preserved. The updated Release output is `build\interface-refresh\Release\PandaClip.exe`; close your standard copy normally before rebuilding `build\Release\PandaClip.exe`.

To launch the updated Release build directly from VS Code's PowerShell terminal:

```powershell
.\build\interface-refresh\Release\PandaClip.exe
```

The equivalent commands in a VS 2022 Developer PowerShell are:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
cmake --build build --config Release --parallel
```

## Archived Win32 Settings scrolling fix (October 5, 2026)

The old Settings wheel handler invoked `shellLayout()` for every scroll: it moved/repainted controls across all four pages, showed/hid every settings row, resized each combo twice, and visited the capture preview resize path. This was the confirmed expensive path. Settings also used a static-text window as a native-scrollbar host; real thumb dragging did not move its contents in the baseline. Dropdowns consumed wheel input and changed values, while PageDown and top/bottom commands were not implemented. These failures were reproduced with capture off, recording active, and replay active.

Settings now uses a regular viewport window with one clipped content child. Scrolling moves that child only; it does not recompute row geometry, enumerate devices, collect/save settings, resize the preview, or lay out other pages. Resize/rate-mode/DPI layout recomputes the content height and page size, clamps the offset, and batches row geometry. Thumb tracking reads the full position through `GetScrollInfo`. Wheel routing covers the message loop, root window, viewport, and child controls; fractional wheel deltas accumulate and Windows' wheel-line preference is respected. Wheel input scrolls instead of changing dropdown/slider values. PageUp/PageDown work within Settings; Home/End/arrows scroll from buttons/background while edits retain their text-navigation keys. Tab focus reveals the focused setting. Static labels no longer create extra tab stops.

Device enumeration is on one background COM worker with coalesced requests and bounded results. Lists load at startup and refresh explicitly or on device changes; opening a mic dropdown does not enumerate again. Completed updates preserve IDs, defer during recording/open dropdowns, and keep disconnected endpoints visible. Optional logs report discovery duration. UI status text, level meters, and tray notifications update only when their values change. Settings edits/window changes are debounced for 750 ms; explicit Apply and shutdown still save, and encoder changes retain safe session-boundary behavior. Scrolling itself produced no settings-file rewrite in the final checks. The old store already performed disk writes on a worker and skipped identical values; disk writes and repeated enumeration on every scroll were **not** demonstrated as the main cause.

### Measured UI response

Release comparison used the same PC, 125% Windows scaling, a 1000x850 physical window, 80 alternating position/wheel pairs, and the same moving synthetic source. Active sessions used 1280x720/30 video-only. Each wheel duration measures synchronous message handling; UI CPU is the UI thread's CPU time over the complete 80-pair loop, including position changes. These are UI measurements, not Geometry Dash frame-time or recording-performance results.

| State | Before median / p95 wheel (ms) | After median / p95 wheel (ms) | Before / after UI CPU (ms per loop) |
| --- | ---: | ---: | ---: |
| Capture off | 89.83 / 113.35 | 2.44 / 6.63 | 6484 / 234 |
| Recording | 91.24 / 115.24 | 2.98 / 8.25 | 6328 / 328 |
| Replay | 91.53 / 104.57 | 2.31 / 4.17 | 6844 / 188 |

Quiet `WM_NULL` probes were already fast before the fix; the confirmed slowdown was scroll/layout work, not a continuously blocked message loop. Earlier reproduction runs measured medians of 110-126 ms, showing normal run-to-run variation. Do not interpret these short results as a worst-case latency guarantee.

Debug and Release builds passed. Both configurations passed viewport/child wheel handling, real injected wheel input over labels/edits/dropdowns, native scrollbar dragging, PageDown, bottom reachability, Tab access to first/last settings, value retention, and no scrolling-triggered settings rewrite. Physical window sizes 875x775 and 1450x1000 exercised narrow/wide layouts at 120 DPI (125%). Ordinary and replay test files independently decoded. The missing-device/Refresh/defaults/damaged-settings regression passed after correcting a missing initial Default-mic row in the new async loader. The recording/replay regression also checks Mix/Game/Microphone playback, safe pending format changes, tray saving, and accepted-export shutdown.

Other display scales and movement between monitors remain manual checks; no system-wide display setting was changed. Audible speech, physical mic disconnection, and active Geometry Dash performance remain unverified. Focused automation initially used the wrong thumb-command constant and lacked activation before native mouse input; corrected runners were repeated and their partial failures are not counted as a final pass.

Final Release UI artifacts are in `build\settings-ui\check-20261005-194822`; Debug UI artifacts are in `check-20261005-194212`. Evidence is under `build\settings-ui`: `baseline-final.txt`, `release-final.txt`, `debug-final.txt`, `settings-edges-final.txt`, `daily-regression.txt`, build logs, and each `check-*\measurements.json`. Tests use isolated profiles/media/logs and control only their own app/source processes. The older desktop-interacting runner is now opt-in. Prefer the quiet workflow below; historical pointer-based results above are not new verification of this redesign. To deliberately repeat that older check:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_settings_ui_checks.ps1 -Executable 'build\interface-refresh\Release\PandaClip.exe' -Interactive
```

Close the old copy normally, then launch the updated Release path above. To rebuild the current alternate output: `cmake --build build\interface-refresh --config Release --parallel`. Standard VS Code tasks/F5 still build the source in `build`; after closing its running EXE, the standard Release task will also produce the fix there.

The follow-up verification found the original `build\Release\PandaClip.exe` still running with replay enabled. Its on-disk build was from 18:22; the fixed Release was built at 19:48. Running applications do not receive rebuilt source changes. Save any wanted replay clip and exit the old copy normally before switching builds. The old session was preserved during testing. Fresh checks are in `recheck-final.txt` and `extended-keys-final.txt`: PageUp/Home/End, fractional wheel accumulation, and wheel input over an on-screen gain slider were also checked, with the slider value retained. Other display scales remain manual checks.

## Archived Win32 Geometry Dash instructions

1. Start Geometry Dash. Launch the Release executable above and open **Settings**.
2. Choose the output folder (default `%USERPROFILE%\Videos\PandaClip`). Keep 1920x1080, 60 FPS, Balanced P4 and VBR initially. GPU scaling fits the picture with letterboxing; it preserves aspect ratio through source resizing.
3. Choose **Selected app** for game audio. **Whole system (explicit)** captures only the explicitly selected output endpoint; **Off** disables this source. There is no automatic fallback.
4. For voice, enable **Microphone** and select its named device, such as **Microphone (HyperX Cloud III Wireless)**. **Default device** means Windows' default recording input (eConsole), not its communications input. Click **Check mic**, speak, and check the actual opened name, raw level, Windows mute/volume, and sample status. Start at 100% PandaClip gain with mic mute unchecked. Nonzero samples do not prove audible speech. Starting recording/replay stops the check asynchronously before opening recording audio.
5. Leave **Mix**, **Game**, and **Microphone** tracks selected for mixed playback and separate editing stems. Click **Apply**. With one source enabled, only that source's requested stem and Mix are written. Uncheck every track, or disable both sources, for video-only output. AAC is stereo 48 kHz.
6. Open **Capture**, select Geometry Dash, and click **Start Preview**. Respond to Windows borderless consent if prompted and read the outline status. Preview can be hidden without stopping recording or replay.
7. For a recent clip, keep **60 seconds** and **512 MiB**, click **Enable Replay Buffer**, wait for history to warm, then click **Save Clip** or press **Ctrl+Shift+F9**. If the shortcut reports a conflict, edit it in Settings. Wait for **Saved clip** and its actual duration. Shorter available history is valid during warm-up or memory pressure.
8. For a continuous file, click **Start Recording**, then **Stop Recording**, and wait for **Saved MP4**. Open **Clips**, double-click the finalized file, and select Mix/Game/Microphone for listening. Clip names use `PandaClip_Clip_YYYYMMDD_HHMMSS_mmm_sequence.mp4`.

Replay is also usable while continuous recording is active. In that case it shares the same H.264 NVENC output and AAC packets; saving a clip does not restart capture/encoding or pause the rolling buffer. Enabling replay by itself starts capture/encoding even if preview is hidden. Stopping a continuous recording while replay remains enabled restarts the replay-only encoder and explicitly resets the buffer, so wait for its history to warm again. Changing encoder/audio settings also resets history to prevent incompatible packets from mixing. Closing the selected window drains the current file and suspends replay. PandaClip waits up to 60 seconds for an unambiguous window with the same executable path and title, then starts a new file/session and warms replay again. Click **Stop Preview** to cancel recovery. If it cannot identify the reopened window, select it manually.

No audio device is substituted without an explicit selection. A disconnected mic is reported, contributes silence, and retries the same endpoint at most three times (2/5/10 seconds). After exhausted retries, reconnect it and start a new session. PandaClip does not send microphone audio to the speakers for live monitoring.

## Settings, defaults, and supported limits

| Setting | Default | Supported values |
| --- | --- | --- |
| Video output | 1920x1080 SDR H.264 | 720p, 1080p, 1440p; custom even width 320-3840 and height 180-2160 |
| Output cadence | 60 FPS | 30, 60, 120; subject to NVENC dimension/throughput capabilities |
| Quality preset | Balanced P4 | P4 or High Quality P6 |
| Rate control | VBR, 20 Mbps target / 35 Mbps maximum | VBR or CBR, 1-200 Mbps; CQP with QP 0-51, starting at 23 |
| Replay history | 60 seconds | Presets 15/30/60/120/300; custom 5-600 seconds |
| Compressed packet budget | 512 MiB | 128-4096 MiB; includes retained export snapshots |
| Pending clip exports | At most 2 | Fixed bound; further saves report busy or memory pressure |
| Audio tracks | Mix and enabled-source stems | Independent Mix/Game/Microphone choices |
| Audio sync | Game 0 ms, mic 0 ms | -500 to +500 ms per source; positive delays, negative advances |
| Preview | Visible, 30 FPS refresh limit | Hide without stopping capture; refresh limit 1-60 FPS |
| Audio gain / mute | 100%, unmuted | Independent game/mic gain 0-200%; live mute |
| Capture cursor | Off | Applies on the next capture session |
| Close behavior | Exit | Optional close/minimize to tray |

The encoder checks actual H.264, ARGB input, dimensions, rate control, and throughput support. Unsupported configurations report an error. On this RTX 5070 Ti, the queried throughput permits 720p120 and 1080p120, but conservatively rejects 1440p120. Output FPS is a constant-frame-rate schedule; it does not establish the number of distinct WGC frames. Diagnostics reports source cadence and repeated/late/dropped frames separately. Only relevant bitrate or QP fields appear for the selected rate mode.

Encoder and audio changes apply at the next safe session boundary, with a visible pending message while recording/replay is active. Replay history resets when a new encoder/audio session starts. Preview preferences and replay duration can change live; a memory-limit change resets history and is rejected while snapshots are retained. Capture-cursor changes take effect on the next capture session. Gain/device/source controls are locked during an active session; mute controls remain live.

Settings are stored as bounded, versioned UTF-8 in `%LOCALAPPDATA%\PandaClip\settings-v1.txt`. A coalescing background writer flushes a temporary file and replaces it atomically. Source executable/title, endpoint IDs, volume/mute/sync, video settings, replay settings, folder, shortcuts, preview, tray preference, and window bounds persist. Missing sources/devices remain explicitly unavailable. Damaged/outdated settings load defaults with a warning and are preserved until an explicit Apply or confirmed Restore Defaults. Restore Defaults does not delete clips. Restoring settings never automatically starts capture. Launch-at-sign-in and automatic replay start are not implemented.

### Global shortcuts

Settings contains press-shortcut controls for Save Clip, Toggle Replay, Start/Stop Recording, Mic Mute, Game Mute, and Show/Hide PandaClip. Save defaults to **Ctrl+Shift+F9**; the other five actions start unbound. Press the combination in its field, then **Apply shortcuts**. Backspace/Delete clears a binding. Function keys and common modified keys are supported; unmodified letter keys, Windows-key bindings, and reserved F12 are rejected with an explanation.

Each action shows registration status. Internal duplicates and shortcuts held by another app are reported beside the affected bindings. Old registrations are released before applying replacements. `MOD_NOREPEAT` prevents held keys from repeatedly toggling actions; clip exports remain bounded independently of key input.

## Archived Win32 Clips, playback, and tray

**Clips** shows finalized MP4s from its selected folder, with thumbnail, name/type, date, duration, resolution/FPS, and size. Search filters names; sorting supports date/name/duration/size. **Refresh** discovers external changes; **Open Folder** opens Explorer. Corrupt files remain visible with a useful metadata/playback error. Temporary `.partial.mp4` files are excluded. External removals are detected on Refresh or when opening the missing file.

Metadata and thumbnails run on one cancellable background worker. Scans are capped at 2,000 files and thumbnails at 256 entries, 160x90 each (about 14 MiB per collection). The refreshed UI uses a separate 200x200 DIP card image slot for each of those bounded thumbnails (plus one shared placeholder). At 125% scaling that image list is about 61 MiB; at 200% it is about 157 MiB. Backend result/cache collections add their original pixel storage; decoder and application memory are additional. There is no thumbnail disk cache. Successful saves update only the new file after initial discovery; they do not rescan the directory on every save. A folder change cancels outdated work between files; an individual Windows metadata call must return before cancellation completes.

Double-click a clip, select it and press Enter, or select it and press Play / Pause. Playback provides play/pause, seek/time, volume/mute, fullscreen (Esc to leave), audio-track selection, and the system default player. Track changes retain position and synchronization; Mix is selected by default where present. Rendering fits the clip's display aspect ratio. Video-only files are supported. Playback uses Media Foundation's player and does not create another NVENC session or stop replay. Changing clips or leaving Clips releases playback resources.

New files have a small optional `.mp4.tracks` sidecar identifying Mix/Game/Microphone; keep it beside its MP4 for those labels. The MP4 itself is independently playable. Missing/stale sidecars and older/external files use generic audio labels. Sidecar-write failure does not invalidate an already finalized MP4. Other players may expose generic AAC names or play only the first track.

With **Close/minimize to tray** enabled, capture stays active while hidden. The tray tooltip/menu reports state; icons are gray idle, red recording, green replay, and amber recovery/finalization. The menu offers Open, Save Clip, Start/Stop Recording, Enable/Disable Replay, and Exit. Actual Exit drains active recordings and accepted exports, stops workers, and flushes settings. If shutdown remains stalled for 30 seconds, a dialog offers continued waiting or explicit force-exit; forced termination can leave the current file incomplete and never reports it Saved. The stalled-worker choice itself has not been fault-tested.

## Milestone 6 verification (October 4-5, 2026)

Both standard MSVC x64 Debug and Release builds passed, along with the optional verification tools. Final build logs are `build\milestone6\build-debug-final.txt`, `build-release-final.txt`, `tools-debug-final.txt`, and `tools-release-final.txt`. The actual standard Release executable was launched with an isolated test profile, navigated, and closed successfully (`smoke-standard-release-final.txt`). Tests controlled only their own PandaClip/synthetic-source processes and kept generated profiles, logs, screenshots, and media under this project's build folder.

The October 5 format matrix independently decoded every video and AAC stream:

| Actual video format / mode | Decoded frames | Video duration | AAC layout |
| --- | ---: | ---: | --- |
| 1280x720, 30 FPS VBR | 166 | 5.53333 s | Mix, Game, Microphone |
| 1920x1080, 60 FPS CBR | 333 | 5.55000 s | Mix only |
| 2560x1440, 60 FPS CQP | 331 | 5.51667 s | Game, Microphone |
| 1280x720, 120 FPS VBR | 661 | 5.50833 s | Video only |
| 1600x900, 30 FPS VBR | 166 | 5.53333 s | Mix, Game, Microphone |

All video streams began at timestamp zero and had strictly monotonic, exact configured-CFR timestamps. Media Foundation reported the 120 FPS metadata ratio as `10000000/83333` (approximately 120.00048); the verifier checked every decoded timestamp against the intended 120 FPS schedule rather than treating that rounded ratio as drift. Every enabled AAC stream decoded as stereo 48 kHz with monotonic, contiguous timestamps. This verifies structure/timing, not perceptual synchronization or audible microphone speech. The 4.9- and 10.0-second replay files also independently decoded all three AAC tracks and video.

Focused automated checks passed:

- Custom 37-second history/128 MiB settings and persistence; invalid 4/601/non-numeric duration, 127/4097 MiB, and odd dimensions were rejected. Debug and Release packet-ring stress covered byte pressure, eviction, shared snapshots, pending-save bounds, and 5/15/30/37/60/120/300/600-second selections. These were packet tests, not timed ten-minute recordings.
- Shortcut capture, internal conflicts, clearing, rebinding, and actual registered Ctrl+Alt+F10 input. Repeated key-down without release toggled mic mute once. A real default-shortcut conflict with another PandaClip process was reported in the October 4 run; the October 5 isolated run registered it successfully.
- Recording and replay shared the old 720p30 configuration while a 1600x900/60 change was pending; stopping recording restarted replay with the new format and a history reset. Warm-up and repeated exports finalized and decoded.
- Gallery updates after recording/replay, search/sort paths, bounded thumbnail generation, damaged entries, partial-file exclusion, missing playback files, and inaccessible folders. A service test proved that a finalized-save update adds only the named clip; manual Refresh discovers unrelated external additions.
- MFPlay opening/advancing, paused seek to two seconds, all three audio selections preserving paused position within 0.1 seconds, resume, and fullscreen aspect fit. Replay saved successfully while gallery playback was active. The final fullscreen screenshot shows the synthetic moving circle retaining its shape; this is not gameplay evidence.
- Close-to-tray retained replay; tray Save/Open commands worked. Exit with replay active and an accepted export completed within the test's 15-second deadline, and every finalized file decoded.
- Missing source/mic/output identities remained selected through Refresh with no fallback. A missing mic gave an endpoint error. Restore Defaults Cancel/Confirm and unknown settings-version recovery worked; explicit Apply replaced the damaged configuration. Atomic UTF-8 roundtrip passed.
- Scrolling and small-window layout at the current 125% Windows scaling (120 DPI), with an actual 875x775 physical window, were checked. Multiple-monitor DPI transitions are still manual.

Evidence: `build\milestone6\checks-final.txt`, `daily-final.txt`, `settings-edges-final.txt`, `services-final.txt`, `services-debug-final.txt`, `replay-stress-final.txt`, `replay-stress-debug-final.txt`, and `gallery-final.txt`. Per-file decoder reports are beside media in `checks-20261005-173111\profile\Videos\PandaClip`; daily-operation media is in `daily-20261005-173743`. Gallery screenshots are in the timestamped `gallery-*` directories.

To repeat the focused checks from this folder:

```powershell
cmake -S . -B build\milestone6 -G "Visual Studio 17 2022" -A x64 -DPANDACLIP_BUILD_REPLAY_STRESS=ON
cmake --build build\milestone6 --config Release --parallel
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_m6_checks.ps1 -Interactive
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_m6_daily_checks.ps1 -Interactive
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_m6_settings_edges.ps1 -Interactive
.\build\milestone6\Release\PandaClipReplayBufferStress.exe
.\build\milestone6\Release\PandaClipM6ServicesTest.exe "build\milestone6\checks-20261005-173111\profile\Videos\PandaClip" "build\milestone6\services-new"
```

Use your latest format-test media directory for the service test's first argument and a **new** directory inside build for its second. These short tests use synthetic motion and generated game audio. Completed milestone 5 long synchronization/performance tests below were not repeated; there is no new active-GD performance or listening result.

**Still requiring you:** speak audibly into the intended mic and listen to Mix/Game/Microphone individually; physically disconnect/reconnect it; play moving GD and run the frame-time benchmark below; check border visibility/real permission denial on your Windows policy; exercise multi-monitor DPI changes, Explorer/system-player opening, and the tray menu by mouse. Actual hung-GPU forced shutdown is unverified. Confirm real-game synchronization near the start/end of a longer session before applying any nonzero sync adjustment.

## Capture outline and Windows consent

PandaClip calls `GraphicsCaptureAccess.RequestAccessAsync(Borderless)` on its UI apartment without blocking the UI. Capture waits for that result. Only **Allowed** permits `GraphicsCaptureSession.IsBorderRequired(false)`; denied, unsupported, and failed requests leave the Windows default border setting alone and report that the outline remains. **Request borderless** retries the supported consent flow. The allowed policy is reapplied to every new WGC session, including preview, direct recording, replay, and recovery. A border-setting failure is reported separately without discarding otherwise usable capture.

Windows can keep an outline if another application captures the same target and requires a border. The status deliberately says **permission granted; requesting no outline**, rather than claiming the visible border was removed. Property success/readback cannot prove that. These rules follow [Microsoft's IsBorderRequired documentation](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.graphicscapturesession.isborderrequired).

On this PC the unpackaged EXE returned **Allowed** during both Debug and Release testing. That does not establish availability on every Windows version/policy. If the status says package capability missing, denied, or unpackaged consent failed, use the optional full-trust MSIX package: its manifest declares `uap11:graphicsCaptureWithoutBorder`, `runFullTrust`, and microphone access. Merely placing AppxManifest.xml beside the F5 EXE does not give it package identity. Signing/installing and launching the packaged application is the required packaging change when identity/capability is needed. Windows build 20348+ is required for the borderless APIs; policy/user denial must still be respected.

### Optional MSIX build and installation

No package/certificate has been installed or Windows permission changed by this work. From the project root:

```powershell
cmake --build build --config Release --parallel
powershell.exe -NoProfile -ExecutionPolicy Bypass -File packaging\New-MSIX.ps1
```

The script uses the installed Windows SDK MakeAppx, stages the EXE/manifest/generated initials icons under a unique `build\packages\package-*` directory, and emits an **unsigned** `PandaClip.msix`. SDK schema validation passed. It copies the Release EXE; install Microsoft's x64 Visual C++ v143 Redistributable on any machine without that runtime. No store submission or installer dependency download occurs.

Use a code-signing certificate whose subject matches the manifest publisher, by default **CN=PandaClip Development**. Alternatively pass `-Publisher 'CN=Your Certificate Subject'` when packing. For a local development certificate, export its PFX and public CER into `build\signing`; trust its public certificate in **Local Machine > Trusted People** using Windows' certificate import UI before installing. Trust only your own development certificate. Signing/trusting requires your own deliberate Windows setup; it was not performed during verification. With an unencrypted development PFX (or add SignTool's `/p` password option for your protected PFX):

```powershell
$sdk = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Directory |
  Where-Object Name -match '^10\.0\.' | Sort-Object Name -Descending | Select-Object -First 1
$signTool = Join-Path $sdk.FullName 'x64\signtool.exe'
$package = Get-ChildItem build\packages -Recurse -Filter PandaClip.msix |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1
& $signTool sign /fd SHA256 /f .\build\signing\PandaClip.pfx $package.FullName
if ($LASTEXITCODE -ne 0) { throw 'Package signing failed; do not install the unsigned package.' }
Add-AppxPackage -Path $package.FullName
```

Launch **PandaClip** from Start after installation, then request borderless access. This is a separate launch from the unpackaged VS Code F5 configuration. Missing MakeAppx/SignTool means the Windows SDK tools need installing through Visual Studio Installer. A signed package installation/runtime grant or denial has not been tested. See [Microsoft capability declarations](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/app-capability-declarations) and [desktop MSIX packaging](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-manual-conversion).

## Microphone diagnosis and pre-recording check

**Default device** now means the Windows default **recording input** (`eConsole`). The previous implementation used the default **communications** input (`eCommunications`), which can be another device. Resolve Default once and keep that endpoint pinned until you explicitly reselect a microphone; named choices open only their exact endpoint ID. Opening errors include HRESULT and permission/connection guidance. An invalid endpoint does not fall back to Default or another input. Game audio similarly never switches to whole-system capture without your explicit selection.

The main window separates game and microphone controls so both dropdowns, enable checkboxes, volumes, mutes, meters, and source statuses fit at the default size. **Check mic / Stop mic check** opens only the selected mic with the production WASAPI/resampling path on a background worker. It has no PCM queue, encoder, disk recording, or speaker monitoring. Its meter/sample indicators are **before PandaClip gain/mute**; muted/zero-gain recordings can therefore show input activity, with the recording mute/gain warning alongside it. Samples arriving without a signal are reported explicitly. Endpoint volume/mute is read, not changed. During recording/replay the actual microphone already open in that pipeline supplies the meter/status; a second mic check cannot start.

For access failures, open **Settings > Privacy & security > Microphone**, check **Microphone access** and **Let desktop apps access your microphone**, and PandaClip's app permission when packaged. Also inspect **Settings > System > Sound > Input**, the headset's physical mute, and other applications holding exclusive input access. Supported integer PCM and float formats are converted to 48 kHz; unsupported formats now produce an error rather than silently converting to zero. Logs/reports include actual endpoint IDs/names, native format, incoming packets/frames, WASAPI silence flags, nonzero resampled frames, Windows input volume/mute, and recording gain/mute.

### October 4 verification and silent-track investigation

- **Builds:** current code built Debug and Release in `build\audio-border`; standard Release also built. Standard Debug linking encountered LNK1168 because the user's existing Debug process was still running; that process was preserved. Close it and rebuild before F5. VS Code configuration is unchanged.
- **Real Windows permission:** the unpackaged app received **Allowed**, and logs show `IsBorderRequired(false)` policy applied to the original and recreated WGC sessions. Visible Geometry Dash outline removal and a real Windows denied decision were not observed. The separate policy test passed Debug/Release with simulated Allowed/Denied/Pending/Unsupported/Failed decisions and four recreated sessions; it proves setter gating, not OS consent or visual border removal. Optional MSIX packing/schema validation passed; signing/installing/packaged runtime were not tested.
- **Enumeration/privacy:** five named active inputs were enumerated. Windows desktop mic privacy entries read Allow, and actual WASAPI capture succeeded. No privacy/default-device settings were changed. Default recording is **HyperX Cloud III Wireless**; default communications is **e2eSoft iVCam**. HyperX opened at **32 kHz mono 32-bit float**, unmuted, Windows input volume **70.6%**. A ten-second check received **322,880 native frames**, converted nonzero input, and retained **zero queued PCM packets**. Raw signal was mostly 1-2/1000; this proves activity, not audible speech.
- **Reproduced silent input:** explicitly opening iVCam received **444,087 native frames** in ten seconds and **zero nonzero resampled frames**, despite no WASAPI SILENT flag, endpoint unmuted and volume 100%. The main-window check says samples are arriving but no signal was detected. An earlier saved three-audio-track file (`build\milestone5\run-20261003-232854-Both\profile\Videos\PandaClip\PandaClip_20261003_232900.mp4`) independently decodes with a completely zero microphone stem and nonzero Game/Mix. Its earlier mic native format was 44.1 kHz stereo, matching the current virtual input rather than HyperX. The previous default role is a demonstrated selection bug and a likely explanation for that silent stem; the old log did not record the actual endpoint identity, so the historic choice cannot be conclusively established.
- **Release UI/MP4 checks:** default and explicit HyperX pre-recording checks, asynchronous check-to-recording handoff, five successive source combinations (game-only, mic-only, both, mic muted, video-only), two capture restarts, and a both-source replay export passed. Each ordinary file was about 13 seconds. All streams independently decoded: H.264 1920x1080 at exact 60 Hz timestamps; enabled audio was 48 kHz stereo AAC with monotonic, contiguous timestamps. Track counts were 2/2/3/2/0 AAC respectively. Mic-only decoded peak **405/32768**, RMS **0.001814**; muted mic tracks were exactly zero. The 4.9-second replay decoded all three audio tracks and first video timestamp zero. These are synthetic-window/input-activity checks, not audible speech or gameplay performance.
- **Failure and buffer checks:** an explicitly nonexistent mic endpoint failed with **0x80070490**, zero packets, and no substituted device. Debug/Release replay byte/duration/snapshot stress tests passed. MP4 verification now reports per-track peak/RMS/nonzero samples/digital silence in addition to decoding/timestamps; it does not equate those counters with speech or listening quality.
- **Debug UI/MP4 checks:** repeated the full pass, additionally switching explicitly to the silent iVCam input during the check and recording with mic gain zero. Six successive combinations (game-only, mic-only, both, muted mic, zero-gain mic, video-only) finalized and independently decoded with AAC counts 2/2/3/2/2/0. Muted and zero-gain Mix/Microphone tracks were exactly zero while raw input still arrived. Mic-only decoded peak **331/32768**, RMS **0.001683**. Both-source replay, capture restarts, and Allowed consent passed again. Evidence: `build\audio-border\ui-checks-debug.txt` and `ui-20261004-184534`. The live meter is RMS-scaled; the initial screenshot/transcripts mislabeled that raw scalar as peak, corrected in the final build.

Evidence: `build\audio-border\ui-checks-pass.txt`, `ui-20261004-184236` (MP4s, per-file verifier reports, UI screenshot, logs), `mic-probe.txt`, `communications-mic-probe.txt`, `invalid-mic.txt`, and `prior-mic-track-verify.txt`. Test runners isolate profile/output/log paths under this project's build folder and only control their own source/app processes. An initial helper failure selected the wrong PowerShell command alias; a later runner mistook a hotkey conflict status for a failed export, although the export had finalized. Those partial runs are not the completed pass above.

To repeat the check with connected/allowed input devices, configure `build\audio-border` with `-DPANDACLIP_BUILD_REPLAY_STRESS=ON`, build all targets for the desired configuration, then run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\run_audio_border_checks.ps1 -Interactive -Configuration Release` (or Debug). The script selects its own synthetic source, checks Default/named input, records source combinations, and decodes each file. A real consent prompt requires your response. `PandaClipAudioInputProbe.exe build\audio-border\unused-probe-directory [endpoint-id]` checks an exact/default input for ten seconds without recording; the directory must be new and inside this project. The executable logs locally there.

**Checks still requiring you:** close the old PandaClip copy, start the new one, select moving GD, grant borderless consent and visually inspect the game outline during preview/recording/replay/restart. Exercise an actual denied permission decision without expecting a borderless claim. Select **HyperX Cloud III Wireless**, enable Microphone, Check mic and speak audibly; record with 100% gain/unmuted, wait for Saved, then listen to Mix/Game/Microphone individually using a player/editor that can select all tracks. Other players may play only the default Mix. Verify physical microphone mute/disconnection/reconnection and actual gameplay/game-audio behavior. No human listening, physical mic speech/disconnection, or new active Geometry Dash gameplay pass was performed.

## Replay behavior and limits

- Replay stores compressed H.264 and AAC packets in RAM. It does not retain raw images or start a second encoder when settings match. The default cap is 512 MiB, editable from 128 to 4096 MiB; history is configurable from 5 to 600 seconds. Capture shows requested/available duration, rolling packet bytes, retained export bytes, the cap, and save state. Memory pressure visibly warns that the available history is shorter. This is a compressed-packet budget, not a limit on total process RAM.
- The rolling ring is byte bounded and duration bounded. Eviction aligns history to an H.264 IDR. NVENC requests an IDR every two output seconds. Export begins at a usable IDR, so alignment can make a requested duration shorter by up to roughly one keyframe interval; warm-up or memory pressure can produce a shorter clip. Actual duration is shown in status and logs.
- Exports take a stable shared-packet snapshot and run asynchronously. At most two clip exports can be pending. The MP4 muxes the saved compressed packets without re-encoding, carries H.264 parameter sets and AAC configuration, and uses one common timestamp offset based on the starting video IDR. AAC access units stay whole; audio begins at the first complete packet at/after that offset, which can trim less than one 48 kHz AAC frame (about 21.3 ms) from the head. The export is reported **Saved** only after Sink Writer finalization succeeds and the temporary file is renamed.
- A pending export retains its shared packets against the same memory cap. If snapshot retention would exceed the cap, the save fails clearly rather than growing memory without bound. A failed save does not stop the healthy rolling buffer. Check the app status/log for folder, muxer, disk, hotkey, or encoder errors.
- The pipeline uses a monotonic clock. H.264 has no B-frames in this configuration, so packet PTS/DTS ordering is retained. For 60 FPS output, a missing WGC source frame repeats the latest image; late frames may replace stale queued frames. Preview redraw speed does not schedule recording.

## Reliability and synchronization controls

Open **Diagnostics** to inspect capture cadence, source timestamp gaps and callback delivery delay, CFR repeats/replacements/drops, video/audio queue high-water marks, encoder/GPU/mux call time, audio rate correction and timestamp fallbacks, replay RAM, export timings, process memory, and recent failures. These counters are separate from the normal recording controls. **Export report** writes a UTF-8 text report into the selected output folder on a background thread. Report-write failures appear in that view, the failure history, and the diagnostic log. It retains the most recent completed session's counters. Settings' preview visibility hides the preview without stopping capture, recording, or replay; minimizing also skips preview rendering.

Optional **Game sync (ms)** and **Mic sync (ms)** are under Settings. Both default to **0** and now persist across launches. Positive values **delay that source's audio**; negative values **advance it** relative to video. For example, `-100` moves a consistently late source 100 ms earlier. The range is -500 to +500 ms. Apply the settings; the next session uses them and old replay history is cleared. Apply an adjustment only after repeatable listening/visual calibration of the intended source, not from the synthetic test alone.

The adjustment moves PCM on the shared 48 kHz timeline before stems are encoded and before mixing, so normal recordings and replay exports share the adjusted compressed packets. PCM advanced before the recording origin is trimmed; delayed source tails beyond the final video interval are trimmed. Negative adjustment increases bounded audio lookahead, so a live replay snapshot can end slightly farther behind the newest video. AAC packets remain complete and replay still uses one common offset for every track.

Capture uses WGC's compositor QPC timestamp, not callback arrival time. WASAPI timestamps use that same calibrated QPC mapping. Source-frame delivery delay is measured separately. Diagnostics distinguish frames available before GPU copy, accepted frames, GPU buffer-pool drops, queue loss, and CFR repeats. Captured surfaces are copied on the GPU into a pool of at most 12 owned textures; leases prevent a buffer being reused while queued, displayed, or used by the encoder. The 60 FPS output remains scheduled independently of preview, repeating the latest image when the source supplies fewer frames. The encoder time counter includes GPU resizing, the synchronous NVENC call, and video mux submission; it is not a GPU-only timing measurement.

### Recovery policy

- **Sleep / session lock:** stop taking new frames, stop audio, drain the current encoders, finalize the current recording, suspend capture, and clear replay history. Wake/unlock resumes only the selected source in a new session; wake recreates the graphics device. The interrupted recording is never appended to. Unlock/wake recovery waits until both conditions permit capture.
- **Capture / GPU errors:** stop the old worker before rebuilding graphics and opening a new encoder. Capture recovery permits three attempts with bounded delays (initial 2 seconds, then 5/10 seconds); a healthy 30-second interval restores the retry budget. Exhaustion is reported and requires a manual start. A callback failure is surfaced even when preview is hidden.
- **Game closure:** retain the executable path, title, and audio choices. Wait up to 60 seconds for exactly one matching window. Ambiguous or missing matches stop automatic recovery. Selected-process loopback is reopened for that window's process; there is no whole-system fallback.
- **Audio endpoint failure:** retain the selected endpoint ID, including the default microphone/render endpoint resolved at first use. Continue video and other audio with silence for the missing source; retry that same endpoint three times. Source-specific warnings remain visible together. Changing the Windows default does not substitute a device. To deliberately choose a newly changed default, select that default option again before a new session.
- **Unavailable output folder / write failure:** exports report failure independently of healthy replay. A missing folder is recreated at the selected path if permitted. Normal recording write failure closes/discards its own incomplete temporary file; if replay was enabled, it starts a fresh replay-only session and reports warm-up. Saved files are preserved. Pending snapshots retain their original codec configuration across resets.
- **Shutdown:** the window stays responsive while recording, clip exports, and a pending report finish. It does not report Saved for an unfinalized file. A hung driver or indefinitely blocked OS filesystem call cannot be safely force-terminated in-process; that exceptional case still needs OS intervention and an unfinished `.partial.mp4` is not a saved clip.

### Repeatable milestone 5 checks

Configure the verification targets as above, then build:

```powershell
cmake --build build\replay-stress --config Release --target PandaClipAacDelayProbe PandaClipReplayBufferStress PandaClipReplaySyncVerify --parallel
.\build\replay-stress\Release\PandaClipAacDelayProbe.exe build\milestone5\aac-probe.mp4
.\build\replay-stress\Release\PandaClipReplaySyncVerify.exe build\milestone5\aac-probe.mp4
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\run_reliability_test.ps1 -Interactive -Seconds 620 -Mode Both -HidePreview
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\run_release_matrix.ps1 -Interactive -Seconds 35
```

`run_reliability_test.ps1` assumes the Release app/tools are already built. It preloads a synthetic WAV, logs actual flash paint times, starts recording before enabling replay to share one encoder, saves near the beginning/end, exports diagnostics every 30 seconds, and independently decodes the resulting files. It checks that the requested preview/sync controls really applied. Artifacts stay in a unique `build\milestone5\run-*` directory. `-Mode Replay` and `-Mode Recording` select the other pipeline modes; `-GameSync -100` or `-GameSync 100` exercises the offset; `-EnableMic` includes the selected default microphone (audibility is a separate manual check). The matrix runs the six mode/preview combinations sequentially with zero adjustments.

With Python 3 installed, compare the ordinary file and same-session replay cues:

```powershell
py -3 tests\compare_sync_run.py build\milestone5\run-YYYYMMDD-HHmmss-Both
py -3 tests\sync_compare_regression.py
py -3 tests\summarize_release_matrix.py build\milestone5\followup-release-matrix.txt
```

The comparison requires independently passed decode reports, existing MP4 files, and at least two complete cue pairs in the ordinary file and every saved clip. It matches both video and audio timestamps against the saved clip's common offset; incomplete evidence fails. Current logs identify each saved path's offset, including concurrently completed exports. Legacy logs are accepted only when snapshot pairing is unambiguous. The regression script uses simulated reports and placeholder files to check rejection behavior; it does not decode media. To regenerate a matrix transcript, redirect `run_release_matrix.ps1` output to a new file under `build\milestone5`, then pass that file to the summary script.

The AAC probe checks game and mic PCM shifts at -500/-100/0/+100/+500 ms and encodes known cues directly through the production AAC MFT/muxer, removing WASAPI/WGC/playback scheduling from that measurement. The MP4 verifier decodes every stream independently and detects audio in 1 ms RMS windows. Media Foundation reader indexes can enumerate tracks differently from MP4 track IDs; it chooses the audio track with the most test cues for its AV event summary. MP4 track creation remains video, Mix, Game, Microphone (enabled stems only); the first audio track is Mix. Listening to individual stems still requires a player/editor with audio-track selection.

Controlled faults are opt-in test hooks, normally unset: `PANDACLIP_TEST_RECORD_FAIL_FRAME`, `PANDACLIP_TEST_EXPORT_FAIL_PACKET`, `PANDACLIP_TEST_EXPORT_FAIL_FINALIZE`, `PANDACLIP_TEST_AUDIO_FAIL_SECONDS`, `PANDACLIP_TEST_EXPORT_DELAY_MS`, and `PANDACLIP_TEST_HOOKS`. The runner exposes `-RecordFaultFrame 120`, `-ExportFaultPacket 10`, `-ExportFinalizeFault`, `-AudioFaultSeconds 8`, `-ExportDelayMilliseconds 3000`, `-ExerciseInterruptions`, `-ExerciseReopen`, `-ExerciseGpu`, `-ExhaustRecovery`, and `-FolderFault` (Replay mode only). They simulate app notifications or worker failures without actually sleeping/locking Windows, removing a GPU, unplugging a device, or filling the disk. Folder faults use only a test-owned destination under the unique run directory. `PANDACLIP_SYNC_TRACE=1` logs mapped source and pre-AAC cue timestamps for diagnosis; ordinary use leaves this off.

After building the Release app and tools, run all nine controlled cases:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\run_fault_checks.ps1 -Interactive
# To repeat only the tail after diagnosing a runner failure, add -FromCase 5.
```

### Measured milestone 5 results

These are automated desktop tests on this RTX 5070 Ti machine, **not active Geometry Dash gameplay or listening results**. Artifacts are under `build\milestone5`. The six Release matrix cells used the same 1920x1080/60 Balanced preset and selected-app audio, zero sync adjustment, about 35 seconds of stimulus plus startup/finalization. The table uses the first live diagnostic sample (~33-36 output seconds), before clip exports:

| Mode | Preview | Source FPS | CFR frames / repeats / late | GPU + encoder + mux avg/max (ms) | Private RAM (MiB) |
| --- | --- | ---: | ---: | ---: | ---: |
| Replay alone | visible | 43.130 | 1990 / 560 / 0 | 3.449 / 11.748 | 205.805 |
| Replay alone | hidden | 43.586 | 1992 / 545 / 0 | 2.705 / 21.358 | 204.508 |
| Recording alone | visible | 42.846 | 1991 / 570 / 0 | 2.999 / 19.788 | 201.738 |
| Recording alone | hidden | 42.446 | 1994 / 584 / 0 | 3.244 / 10.847 | 205.047 |
| Recording + replay | visible | 42.642 | 2176 / 630 / 0 | 4.092 / 19.947 | 211.301 |
| Recording + replay | hidden | 42.484 | 2173 / 636 / 1 | 5.278 / 20.822 | 210.805 |

Every cell decoded independently with 1920x1080 H.264 at exact 60 Hz timestamps and both 48 kHz stereo AAC tracks, with monotonic/contiguous audio. Source and output rates are separate: the synthetic WinForms stimulus delivered fewer images than the 60 CFR slots. All cells had zero video/audio queue losses. Calls over 16.67 ms, in table order, were **0, 10, 1, 0, 1, and 385**; these are combined GPU/NVENC/video-mux wall times, not GPU-only measurements. Video queue high-water was 2-3 of 8 and audio 1-3 of 96. Visible/hidden differences include the synthetic source's scheduling and desktop composition; this is one run per cell, not a statistically established game overhead estimate. Six mode/preview cells passed with repeated clip exports in each replay-enabled cell.

This table is from the Release binary with bounded asynchronous logging, runs `172207`, `172313`, `172418`, `172514`, `172609`, and `172722` on **2026-10-04**. All **16 resulting MP4s** decoded independently. Available-before-copy and accepted-frame counts matched, with zero GPU-pool drops, log drops, or log-write errors in the sampled reports. Twelve successful exports took **290-411 ms**. Raw output and reports are in `build\milestone5\followup-release-matrix.txt`, with the summarized table/JSON beside it. The prior 2026-10-03 matrix remains under `final-release-matrix.*`; its shorter 2.06-2.10 ms averages and lack of sampled slow calls were not reproduced in every follow-up cell. Neither set establishes active-game overhead or continuous-motion smoothness.

The longer run was **631.283 seconds** (37,877 H.264 frames), with Mix and Game AAC decoded for **631.275 seconds** (30,301,184 PCM frames each). It used **visible preview**: an initial automation window lookup did not apply the requested hidden setting; the corrected six-cell matrix above verifies the checkbox and exported report state. The long run's 78 beep/flash pairs spanned **616.05 seconds**. Audio-minus-video varied **51.0-101.0 ms**, median **84.33 ms**; first/last were **101.0/51.0 ms**. Intermediate offsets moved back and forth in frame-sized steps. A least-squares fit changed only **-2.67 ms** over that span; the -50 ms endpoint difference does not demonstrate a steadily accumulating drift.

The logged pre-AAC cues remained exactly **8,000 ms** apart to printed precision; decoded cues differed from those pre-AAC timestamps by at most **0.167 ms** in that run. Source flash paint lateness was **0.076/16.167/33.928 ms** min/median/max. An isolated production AAC/mux probe with known PCM cues at 1000/3000/5000 ms decoded cues at **1000.33/3000.67/5001.00 ms**. This excludes a 57-74 ms AAC/mux delay on the tested Windows path. SoundPlayer speaker presentation and loopback latency remain unmeasured; the remaining constant source/capture-path offset is not independently calibrated. No synthetic-offset subtraction was applied.

Four exports from the long recording decoded independently at **59.117, 58.100, 59.117, and 58.150 seconds**. Matching events agreed with the ordinary file after the common offsets (22, 568, 568, and 570 seconds) to within **0.0001 ms of printed comparison precision**; the audio detector itself has ~1 ms windows and video ~16.7 ms granularity. That agreement verifies same-session rebasing, not perceptual alignment. Export finalization/rename times were **504-538 ms**, with capture continuing. Long-run WGC cadence was **41.22 FPS**, with **12,095 repeats**, **239 late/replaced frames**, zero queue drops, and **2.09/4.45 ms** average/max GPU+encoder+mux call time. This is synthetic source availability, not evidence of GD smoothness.

Replay history stayed around **118-120 seconds** and compressed packet memory around **12.8-13.3 MiB** after warm-up. Process private memory rose from **226.7 MiB at two minutes** to **231.9 MiB at ten minutes**; working set was **138.4 to 144.0 MiB**. This is modest continued growth, not a claim of flat or indefinitely stable total memory. The byte/duration/snapshot reservation stress tests passed in Debug and Release; retained snapshots remain accounted and a cap change is rejected while exports retain reservations.

#### Fresh ten-minute follow-up, preview hidden

The **2026-10-04** run `run-20261004-172835-Both` verified hidden preview and zero adjustments, then independently decoded the ordinary file and four replay exports. Video contained **37,874 frames / 631.233 seconds** at 1920x1080/60; Mix and Game each contained **30,299,136 PCM frames / 631.232 seconds** at 48 kHz stereo, with monotonic/contiguous timestamps. All five files passed decoding, including video starting at timestamp zero. Clips lasted **59.017, 58.100, 59.083, and 58.100 seconds**. Complete AAC packet alignment at the clip head and the final video-frame boundary can make decoded audio sample-count duration differ from video by a few tens of milliseconds; timestamped cue matching checks synchronization across those boundaries.

The ordinary file had **78 pairs across 615.983 seconds**, with audio-minus-video **50.667/67.333/84.000 ms** min/median/max. First/last were **67.333/84.000 ms**, but the fitted offset change was only **+0.550 ms** across the span. Paint lateness was **0.109/7.537/22.483 ms** min/median/max; decoded versus pre-AAC cue onset differed by at most **0.667 ms**. The remaining offset is not a demonstrated accumulating drift, nor a calibrated measurement of speaker presentation. No automatic correction was applied. All **30 matched replay events** agreed with ordinary timestamps after common offsets of **22/568/568/570 seconds**, with **0.0000 ms lag difference at printed precision**; detector/video granularity still limits interpretation. Raw comparison: `build\milestone5\followup-sync-comparison.txt`.

Final capture cadence was **42.94 source FPS**, with **10,768 CFR repeats**, **zero late/replaced frames**, and **zero video/audio queue losses or timestamp fallbacks**. Available-before-copy and accepted counts matched in the reports, without GPU-pool drops. The repeated frames track insufficient synthetic source availability, rather than measured queue loss; this remains unrelated to active GD validation. Average/max GPU+NVENC+video-mux call time was **3.03/31.05 ms**, with **two calls over 16.67 ms** across the ordinary session. Four exports completed in **428-535 ms** while capture continued.

After warm-up, sampled replay history stayed around **118-120 seconds**, using about **12 MiB** of compressed packet RAM under the 512 MiB cap. Private memory increased from approximately **226.0 MiB at two minutes to 233.2 MiB at ten minutes**; working set increased from **138.4 to 146.7 MiB**. Total memory did not remain flat; its growth was not isolated to a particular component. Sampled diagnostics showed no dropped log messages or log-write errors. Full transcript: `build\milestone5\followup-long-run.txt`; reports and media are inside that run directory. The preceding visible-preview run remains separate evidence above.

### Reliability checks actually performed

Debug and Release app builds and verification-tool builds completed successfully. The PCM offset checks placed both game and microphone impulses exactly at -500/-100/0/+100/+500 ms adjustments; the AAC probe decoded in both configurations. Live selected-app audio at **+100 and -100 ms** retained the same adjusted cue offsets in ordinary files and replay exports. The CSV benchmark summary was checked with a numerical fixture (100 intervals, known median/p95/p99), not game data.

The 2026-10-04 follow-up rebuilt Debug and Release and reran both configurations' replay memory/snapshot stress and AAC probes. Four concurrent log producers submitted 1,024 entries while the writer was delayed three seconds: Debug/Release submissions took **3/1 ms**, with exactly **256 pending and 768 dropped**. Normal exit drained the retained messages and overflow notice. Test-owned blocked log folders incremented write-error counters in both builds. These fixture timings measure enqueue behavior, not recording performance. The comparison script passed 12 simulated-report regressions, including rejection of absent media/cues/reports, failed decoding, independently reset audio timestamps, and ambiguous legacy snapshot offsets; it accepted explicit concurrent completions in reversed order. Existing ten-minute and +/-100 ms ordinary/replay artifacts also passed the stricter comparator.

The fresh unavailable-folder check (`run-20261004-172117-Replay`) also obstructed a diagnostics report write. The failure was surfaced and logged, then the test removed only its own obstacle. Three clips saved and independently decoded while CFR counts advanced **906 to 1206**. No disk-filling or unrelated-application fault was used.

Controlled checks used only PandaClip notifications, its test workers, and its own `build\milestone5\run-*` output. Results:

- **Lock/unlock and sleep/wake notifications, followed by real test-window closure/reopening:** three new sessions resumed the same executable/title and process loopback. Four ordinary MP4s and four clips decoded with Mix/Game/Microphone AAC. Injected microphone/game-audio failures retried the same sources; warnings and silence were reported. The microphone stream was decodable but audible input was not established. Artifacts: `run-20261003-232854-Both`.
- **Capture/GPU interruption hook:** drained the first session, recreated graphics, and saved new-session recording/clips. All five MP4s decoded. This exercised app recovery; it did not remove/reset the physical GPU. Artifacts: `run-20261003-233051-Both`.
- **Forced recovery failures:** exactly three attempts, then an exhausted state with replay disabled and no endless automatic restart. Verified again on the final binary in `run-20261003-233638-Replay`.
- **Ordinary recording write failure:** reported Failed and removed its incomplete file; replay restarted with a cleared history and saved three independently decodable clips. Final-binary artifacts: `run-20261003-233742-Both`.
- **Replay packet-write and finalization failures:** each of three attempts reported failure, left no finalized or partial MP4, and kept capture running. CFR counts advanced **904 to 1205** and **903 to 1204** across the respective export exercises. Artifacts: `run-20261003-233831-Replay` and `run-20261003-233907-Replay`.
- **Unavailable output folder:** an owned file obstructed only the test destination. Save reported filesystem error 183; after removing that obstacle, three clips saved/decoded without restarting capture. CFR count advanced **905 to 1206**. Artifacts: `run-20261003-233943-Replay`.
- **Rapid saves:** six presses with an artificial three-second export delay admitted two exports and rejected four; both clips decoded. Snapshot reservation/eviction tests separately verified the byte cap and release of retained memory. Artifacts: `run-20261003-232754-Both`; the stress tool passed in Debug and Release.

An earlier reopening pass exposed loss of the selected title after the HWND disappeared; remembering the identity before closure and handling WGC's Closed event fixed it. Early automation also failed to find the hidden diagnostics window, and a later test assertion mistook the temporarily sticky Saved status for inactive replay. Those runner defects were corrected and the affected checks repeated; earlier partial passes are not used as proof of recovery. The combined fault suite was verified in two runs (`verified-fault-suite.txt` cases 1-6 and `final-fault-tail.txt` cases 5-9), after fixing a PowerShell negative-index issue in its progress assertion.

Physical microphone unplugging/reconnection, actual OS sleep/lock, physical GPU/device reset, listening to each stem, and active Geometry Dash gameplay remain manual checks. Both ten-minute timing runs were synthetic, one with visible preview and the fresh follow-up with verified hidden preview; they do not establish perceptual synchronization or GD frame-time overhead.

### Active Geometry Dash benchmark (manual)

Actual continuously moving gameplay has not been validated. Use the same level segment, window mode, refresh rate, FPS cap, graphics settings, audio configuration, and Release preset for each run. Warm the game first. Run **off / replay on / off / replay on**, at least 60 seconds each, with three repetitions; keep preview hidden for the main comparison, then repeat with preview visible. Actively play or use the game's own practice/playback capability; a menu or paused window does not measure gameplay cadence. Include a hotkey save halfway through each replay run.

For game presentation frame times, use the official [PresentMon console application](https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md). Download its console executable from the official releases into `tools\PresentMon` inside this project if needed. Do not use another recorder. From this project, substitute its actual executable filename:

```powershell
New-Item -ItemType Directory -Force build\gd-benchmark
.\tools\PresentMon\PresentMon.exe --process_name GeometryDash.exe --session_name PandaClipGDBenchmark --v1_metrics --timed 60 --terminate_after_timed --output_file build\gd-benchmark\off-01.csv
# Repeat with replay enabled and unique CSV names, e.g. replay-01.csv.
py -3 tests\summarize_game_frame_times.py "build\gd-benchmark\*.csv"
```

Compare median, p95, and p99 game presentation frame times using the same CSV timing column (MsBetweenPresents for present-to-present intervals; keep the same metrics version and timing column), not average FPS alone. Keep game frame times separate from PandaClip WGC delivery cadence and output repeats. Export PandaClip reports, independently decode saved clips, and inspect continuous motion at normal speed/frame stepping for judder and repeated images. PresentMon results and smoothness/listening judgments have not been measured by the synthetic automated checks.

### Manual listening and hardware checklist

- Speak audibly into the specifically selected microphone. Confirm its meter reacts, then listen to the saved Microphone stem.
- Listen to Mix, Game, and Microphone individually; verify gains/mutes, clipping, missing sounds, and the default Mix track.
- Physically unplug/reconnect that microphone; confirm warning/silence and recovery of the same device, with no fallback. Reconnect after the retry budget to check the manual-new-session path.
- Perform a real lock/unlock and sleep/wake with PandaClip; check finalized pre-interruption files and fresh replay warm-up. Real hardware GPU removal/reset is not simulated test coverage.
- Play moving Geometry Dash for at least ten minutes, save near the beginning/end, listen/inspect synchronization, and complete the off/on frame-time benchmark above.

## Diagnostics and verification

Logs are written to `%LOCALAPPDATA%\PandaClip\logs\panda.log`. They include WGC source-frame availability and largest callback gap, output CFR slots, NVENC timing/slow calls, late/repeated/dropped frames, audio queue pressure and timestamps, and save/encoder/device errors. Producers format/enqueue messages; one background worker creates folders, acquires the cross-process log lock, and writes to disk. Its queue holds at most 256 messages plus one in-flight write. Overflow drops the oldest queued messages and reports the count. Diagnostics reports include pending/dropped messages and log-write errors, so missing log evidence is visible. The process drains retained messages on normal exit; a crash or failed destination can lose messages.

The optional `PandaClipLoggingStress` target checks this queue with a bounded, test-only writer startup delay and a test-owned file obstructing the log directory. Run from the project root using unused paths:

```powershell
cmake --build build\replay-stress --config Release --target PandaClipLoggingStress
.\build\replay-stress\Release\PandaClipLoggingStress.exe build\milestone5\logging-new
.\build\replay-stress\Release\PandaClipLoggingStress.exe build\milestone5\logging-blocked-new --blocked-folder
```

`PANDACLIP_TEST_LOG_START_DELAY_MS` is an optional test hook, clamped to three seconds and normally unset. It stalls only PandaClip's log worker. Logging stress redirects its own `LOCALAPPDATA` inside the requested project directory; it does not change the normal app log destination.

Build and run the replay ring stress check from a VS 2022 Developer PowerShell:

```powershell
cmake -S . -B build\replay-stress -G "Visual Studio 17 2022" -A x64 -DPANDACLIP_BUILD_REPLAY_STRESS=ON
cmake --build build\replay-stress --config Release --target PandaClipReplayBufferStress PandaClipReplaySyncVerify --parallel
.\build\replay-stress\Release\PandaClipReplayBufferStress.exe
```

`PandaClipReplayBufferStress` checks byte/duration eviction, keyframe-aligned snapshots, and clip duration. `PandaClipReplaySyncVerify` independently decodes H.264 and every AAC track, checks timestamps, and reports timing differences between a white flash and a 1 kHz beep embedded in the test clip. Run the repeatable visual/audio sync test from the project root in PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\run_replay_sync_test.ps1 -Interactive
```

The script builds Release targets, opens a synthetic moving window and PandaClip, warms the 60-second replay ring, plays scheduled beep/flash events through selected-app loopback, saves a replay MP4, and verifies it. Test outputs are kept under `build\replay-sync-test`; it does not alter the normal output folder. For a manual replay pass, also make warm-up saves, test all duration presets plus a custom duration, repeat saves, change source size, minimize PandaClip, save during continuous recording, stop recording, close the source, and try a folder path that cannot be created. Listen to the Mix and each stem. A ten-minute real-gameplay sync and quality pass remains useful after this automated short timed-event check.

### Checks performed for milestone 4

- Built PandaClip Debug and Release with the VS 2022 x64 CMake generator. Built and ran `PandaClipReplayBufferStress`; it passed byte and duration bounds, eviction, IDR-leading snapshot, and keyframe-adjusted duration checks.
- Automated a 132-second run with a synthetic animated window and continuous 440 Hz tone. Replay history grew while the PandaClip window was minimized; resizing the source did not stop replay. It exported 15/30/60/120-second selections as 14.8/29.8/58.8/119.8-second clips, then exported repeated clips and a clip while continuous recording was active. Capture continued through export, recording stopped cleanly, replay restarted with a visible warm-up reset, a memory-cap change reset history, and target closure disabled replay.
- Automated a warm-up save after about 4.6 seconds of history while 15 seconds was selected. It saved a 4.8-second available-history clip, and independent video decoding confirmed first timestamp 0 and exact 60 Hz timestamps.
- Independently decoded the four duration clips' video with the in-workspace Media Foundation verifier. Each reported H.264 1920x1080 at 60/1, first timestamp 0, strictly monotonic exact 60 Hz timestamps, and changing frame content; verified durations were 14.8, 29.8167, 58.8167, and 119.817 seconds. The decoder's NV12 buffer is padded to 1088 rows internally; MP4 stream metadata is 1920x1080.
- The four duration clips, warm-up clip, and save-during-recording clip were independently decoded with the Media Foundation stream-by-stream verifier. The selected-app files had one H.264 video track and two AAC stereo 48 kHz tracks (Mix and Game). Audio timestamps were monotonic and contiguous; audio durations were within one AAC packet of video duration. Video began at zero, had exact 60 Hz timestamps, and decoded successfully.
- Ran the automated flash/beep test for a 60-second replay clip with seven detected events. All video and both AAC tracks decoded; timestamps were monotonic/contiguous. The exported beeps were 57.3 ms after their flashes for the first three events and 74.0 ms after for the last four. The measured offset changed by 16.7 ms over the approximately 48-second span between first and last events. This is a synthetic test on this Windows audio/render path, not a Geometry Dash gameplay result.
- Captured the open Geometry Dash window directly, without sending input to the game. During replay-only capture WGC delivered 59.88 frames/s at 2559x1440, with 60 CFR output slots/s, zero queue drops, two late/replaced frames, five repeats, a 40.6 ms largest WGC gap, and a 12.15 ms maximum NVENC call in the observed interval. A 14.05-second saved clip decoded to 843 frames at 1920x1080/60 with monotonic timestamps. The selected frame fingerprints were mostly unchanged, so this appears to have been a menu or paused view rather than continuous active gameplay.
- For a short comparison on the same open game window, continuous recording logged 59.98 WGC frames/s and 60 CFR output, zero queue drops, seven late/replaced frames, eight repeats, a 26.2 ms largest WGC gap, and 3.30/15.66 ms average/maximum encode time over 25.1 seconds. Both runs held the requested output cadence. The runs were not controlled active gameplay sessions, and the different intervals make this a diagnostic comparison rather than a benchmark.
- A deliberately invalid output folder produced a clear create-folder error and replay capture continued. Registering the default global hotkey while another process held it reported a conflict; selecting an alternative registered successfully. Synthetic moving-source diagnostics during the 132-second run averaged about 46.4 WGC callbacks/second while producing 60 CFR slots/second, repeated the latest image for missing slots, recorded no capture queue drops, and showed a maximum NVENC call near 24.5 ms. These are synthetic-window observations, not Geometry Dash performance results.

### Checks still needed on your machine

Geometry Dash's window was captured, but I did not play or control a level; the captured frames were mostly unchanged. Check actual GD cadence during continuous motion and compare replay enabled/disabled while actively playing. Verify that game audio sounds right. Microphone input was not audible in the automated run; listening to the Mix/Game/Microphone tracks, testing a real mic disconnect, and repeating real gameplay synchronization checks near the beginning/end of a ten-minute recording remain manual checks. Milestone 5 has separately completed a ten-minute synthetic run described above. The app supports the relevant source and lifecycle paths, but those active-gameplay, listening, and longer-run synchronization results are not claimed here.

## Current limitations

- SDR H.264 with configurable even dimensions and 30/60/120 CFR, subject to NVENC capabilities; no HDR, VFR, or multiple simultaneous targets.
- AAC is stereo; multichannel inputs are downmixed. Whole-system mode and microphone selection pin endpoint IDs. Deliberately select a different endpoint between sessions if needed. PandaClip does not live-monitor audio or automatically switch disconnected devices.
- Listening quality and mic audibility are not validated by sample decoding; check the saved tracks on the intended headphones/speakers.
- Gallery/playback are implemented with the bounds and sidecar-label limitations above. Trimming, editor, automatic highlights, cloud uploads, noise processing, sign-in launch, and automatic replay start are not implemented. There is no software H.264 fallback; an NVIDIA H.264 NVENC-capable GPU and driver are required.

## References

- [Microsoft wheel-message routing](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mousewheel), [32-bit scrollbar tracking](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getscrollinfo), and [batched control positioning](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-deferwindowpos)

- [NVIDIA Video Codec SDK 13.1 NVENC Programming Guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html)
- [NVIDIA Video Codec SDK 13.1 Read Me](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/read-me/index.html)
- [Microsoft AAC encoder](https://learn.microsoft.com/en-us/windows/win32/medfound/aac-encoder) and [Sink Writer](https://learn.microsoft.com/en-us/windows/win32/medfound/using-the-sink-writer)
- [Microsoft application loopback activation](https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-activateaudiointerfaceasync) and [official sample](https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/ApplicationLoopback/cpp/LoopbackCapture.cpp)
- [Microsoft WGC compositor/QPC timestamp](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframe.systemrelativetime) and [WASAPI packet/device QPC timestamps](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer)
