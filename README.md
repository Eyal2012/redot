# ReDot

## MKV recording (2026-10-11)

Ordinary recordings now default to **MKV** (crash-recoverable, incrementally written); **MP4** is selectable under Settings -> Recording -> Recording format, and instant-replay exports stay MP4. Orphaned `*.partial.mkv` files are validated and repaired into a new `*_recovered.mkv` at the next start; originals are kept. Design, guarantees, limitations and test coverage: [docs/mkv-recording.md](docs/mkv-recording.md). libwebm (BSD-3-Clause) is vendored in `third_party/libwebm`; its notices deploy to `licenses/libwebm`.

## Project rename to ReDot (2026-10-11)

The project folder, CMake targets, test executables, build options and test scripts are now named **ReDot** (workspace: `C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\ReDot`). The application and `ReDot.exe` were already named ReDot. No recording, audio, scenes, Media, UI or tray behavior changed.

Renamed: test/tool executables (`PandaClip*` -> `ReDot*`, e.g. `ReDotQuietApp`, `ReDotReplaySyncVerify`), CMake options `REDOT_BUILD_REPLAY_STRESS` / `REDOT_BUILD_GLASS_LAB`, compile definitions `REDOT_UI_VERIFICATION` / `REDOT_TEST_FIXTURE`, and the internal logging namespace. **Kept for compatibility:** `%LOCALAPPDATA%\PandaClip\` (settings, scenes, layout, logs), the settings/scenes/track-sidecar headers, the default `Videos\PandaClip` folder, `PandaClip_*` clip-name recognition, `PANDACLIP_*` environment test hooks, the `qrc:/PandaClip/` resource prefix and the MSIX identity. Historical documents under `docs/` keep their original names. See AGENTS.md ("Project identity and data compatibility") and CLAUDE.md.

After moving the folder, configure into a fresh build directory; older `build\*` CMake caches contain the previous absolute path and are intentionally left untouched.

## Scenes preview repair (2026-10-10)

Current candidate: **`build/redot-scenes-preview/Release/ReDot.exe`**. The final engineering Release at `build/redot-final/Release/ReDot.exe` remains intact as a fallback. Your running sessions and personal scenes/settings/recordings are preserved.

The Scenes editor's opaque native preview previously painted near-black outside the fitted scene. The native child is now clipped to actual scene content and editing handles, letting the surrounding editor material show through. Smaller handle-safe padding slightly enlarges the aspect-correct preview. Saved transforms, gameplay crop, composition, recording and replay output are unchanged; genuine empty scene areas remain black. See the [cause, actual before/after screenshots, verification and limitations](docs/scenes-preview-padding.md).

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\ReDot\build\redot-scenes-preview\Release\ReDot.exe'
```

Finish existing saves and close the previous app normally before using the same personal profile. The report includes the remaining user-operated Geometry Dash and physical display-scaling check; automated fixtures do not replace that check.

## Final engineering candidate (2026-10-10)

Preserved engineering fallback: **`build/redot-final/Release/ReDot.exe`**. The approved Stage 4 Release remains intact at `build/redot-stage4-accessibility/Release/ReDot.exe`; running sessions and personal preferences/recordings were preserved. The Scenes preview candidate above retains these engineering changes.

This pass reuses compositor GPU shader views, corrects replay accounting for allocated packet capacity and retained snapshots, and publishes recording completion after worker cleanup. A private high-resolution compositor timer corrects demonstrated coarse deadline misses at 120 FPS without changing global Windows timer settings. Scene AAC resources release before MF/COM teardown. The approved UI/Acrylic, recording quality, audio-continuity implementation, scenes, tray, shortcuts and VS Code configuration are unchanged. No new features or runtime dependencies.

See the [final engineering report](docs/redot-final-engineering.md) for the original baseline, repeated comparisons, long-session resource measurements, fault matrix, exact tests, source checkpoint, screenshots and limitations. Ten minutes of quiet generated GPU/PCM recording plus replay completed 119 clips; all 120 MP4s decoded, including Mix and two stems. Replay rebasing agreed within 0.33 ms for 619 complete cues. These are generated pipeline checks, **not new Geometry Dash frame-time or physical audio listening results**. A two-logical-hour packet stress is accelerated, not a multi-hour real recording. Whole-process memory and hardware recovery require the limitations in the report; no leak-free or zero-game-impact claim is made.

Exact tested launch:

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\ReDot\build\redot-final\Release\ReDot.exe'
```

Build and quiet verification (run suites sequentially):

```powershell
cmake -S . -B build/redot-final -G 'Visual Studio 17 2022' -A x64 -DREDOT_BUILD_REPLAY_STRESS=ON
cmake --build build/redot-final --config Debug --parallel
cmake --build build/redot-final --config Release --parallel
& tests/run_quiet_tests.ps1 -BuildDirectory build/redot-final -Configuration Debug -SkipBuild
& tests/run_quiet_tests.ps1 -BuildDirectory build/redot-final -Configuration Release -SkipBuild
```

Final physical review: repeat a fixed GD section in three 60-second runs each with ReDot closed, idle, replay, recording and both. The report supplies an optional official PresentMon command and a read-only CSV summarizer for FPS, meaningful 1% lows, frame-time tails and run variation. Also compare preview visible/hidden separately. Listen to game/Mix/HyperX stems at the beginning/end and after seeks; check temporary source disconnection in a disposable session. Actual game performance, multi-hour operation and physical device interruptions remain pending.

## ReDot Stage 4 (2026-10-10)

Current candidate: `build/redot-stage4-accessibility/Release/ReDot.exe`; the earlier Stage 4 and approved Stage 3 Releases remain intact. Stage 4 adds Media Size (largest first / smallest first), independent **Settings → General → Window behavior → Minimize to tray / Close to tray** checkboxes (off by default), state-aware tray text/commands, and current application-picker wording. Both checkboxes are at the top of General and save independently without needing Apply. Existing recording/replay/audio/composition and Acrylic policies are preserved. See [Stage 4 delivery](docs/redot-stage4.md) and [deployed Settings inspection, screenshots and test matrix](docs/redot-stage4-accessibility.md).

**Stage 3 user acceptance:** all seven manual checks passed: ReDot name, logo, taskbar transitions, recording plus replay, existing scenes/settings, Geometry Dash recording/audio, and minimized taskbar behavior. This is user-reported physical acceptance, distinct from automated fixtures and from physical device-disconnection, long-session synchronization and performance checks.

Preview sources initialize when the saved scene loads, as approved for live Scenes. Launch does not automatically start recording or enable replay; no new startup automation is added. Minimize-to-tray and Close-to-tray are independent. When the notification area is unavailable, minimize remains on the taskbar and Close performs a safe exit; if it disappears while hidden, the main window resurfaces without requesting foreground activation. Tray Open restores normal/maximized state. Tray Exit drains recordings and accepted exports even with Close-to-tray enabled. Only the taskbar dot changes color; the tray logo stays neutral.

Media size sorting uses worker-cached actual file lengths with deterministic path tie-breaking, without additional UI-thread reads. Refresh discovers external file-size changes/removals; inaccessible entries are omitted or report metadata/playback errors. Date, FPS and size remain in clip tooltips.

Stage 4 Debug/Release builds and complete quiet suites passed: 33 MP4 decodes per build, existing scene/editor checks at 100/125/150% Qt scaling, focused sorting/tray lifecycle and hidden encoding/export checks. Physical tray interaction and Stage 4 gameplay/listening remain manual acceptance checks; see the delivery report. The final performance/reliability candidate and its verification are documented above; the Stage 4 evidence here is retained as history.

## ReDot Stage 3 (2026-10-10)

The approved scene-based application is now **ReDot**, with a vector replay-arrow logo and a dot that changes only in the main Windows taskbar icon: neutral while idle, green after replay has encoded frames, red during ordinary recording (including recording plus replay). Captions, Media, About and the tray retain neutral branding. No capture/audio/appearance redesign is included. See [branding, compatibility, assets and verification](docs/redot-rebrand.md).

Debug/Release and complete quiet suites passed, including native status-icon transitions, 31 MP4 decode checks per build, and scene/editor checks at Qt scales 100/125/150%. The user subsequently passed all seven Stage 3 manual checks listed above. Physical display-scale/device-loss and extended gameplay/load/synchronization checks remain separate. Preferences, scenes, layout and output folders continue using the existing PandaClip storage paths; no relocation is performed. Finish existing saves and close the old app normally before using shared personal preferences in ReDot.

## Scenes Stage 2 (2026-10-10)

All previews, recording and replay now use one selected scene: sources → D3D11 scene compositor → native previews and the existing NVENC/AAC/MP4 pipeline. Configure visual and audio sources in **Scenes**; the Capture capsule selects a **scene**. Existing scene definitions and old capture preferences migrate without replacing user-created scenes. See [architecture, evidence, performance and limitations](docs/scenes-stage2.md).

Native Windows capture, direct NVIDIA NVENC H.264, WASAPI audio, Media Foundation AAC/MP4 and compressed replay. ReDot owns its recording pipeline; no OBS/libobs or external recorder is used.

## Current production frontend

**Current candidate: `build\redot-scenes-preview\Release\ReDot.exe`; engineering fallback: `build\redot-final\Release\ReDot.exe`; approved Stage 4 fallback: `build\redot-stage4-accessibility\Release\ReDot.exe`.** Older running builds were left intact. Capture, Scenes, Media, Settings, Diagnostics and the five Quick Settings popovers use shared material/control tokens in `qml/design-system.json` and matching captions. The approved Capture arrangement, sharp native preview, audio strip, gains and independent gears are preserved.

**Desktop audio correction:** packet-local SRC phase and timestamp rounding could damage continuity. The capture converter now retains fractional position/lookahead, advances through flagged silence, and maps one QPC anchor plus integer output sample indexes. A user-operated 25.8-second Geometry Dash/HyperX review produced four finalized WAV checkpoints; the user reported "yea i think its fixed." Raw-to-processed desktop PCM and independently reconstructed final Mix both match the review's samples exactly. This is a short listening result, not long-session or OBS equivalence verification. See [desktop audio evidence, four-WAV review and limitations](docs/desktop-audio.md). Previous [playback investigation](docs/player-output.md) remains as historical evidence; its playback-only scope has been superseded by this capture correction. Original clips and running sessions remain intact.

Qt Widgets owns Capture/Settings/Diagnostics and Qt Quick owns Media. The hidden Win32 state adapter remains for backend settings/lifecycle; it is never a fallback frontend. Glass Lab and its experimental monitor renderer are excluded from ReDot. No screen capture or decoded-video sampling draws production UI backgrounds/materials.

See [focus-transition repair and verification](docs/focus-policy.md), [previous Acrylic investigation](docs/focus-appearance.md), [previous appearance changes](docs/appearance-stability.md) and [frontend inventory, checkpoint and retirements](docs/frontend-consolidation.md). Earlier [desktop-style evidence](docs/desktop-style.md), [repair evidence](docs/repair-v1.md), and the complete [historical frontend/milestone notes](docs/frontend-history.md) are retained. Their launch paths and inactive fallback descriptions are superseded.

## Dependencies and exact build steps

- Windows 11 recommended; WGC requires Windows 10 build 18362+, selected-app audio build 20348+, Desktop Acrylic Windows 11 build 22621+.
- Visual Studio 2022/Build Tools: Desktop development with C++, MSVC v143 x64, Windows SDK (10.0.26100 recommended); CMake 3.25+.
- NVIDIA H.264 NVENC GPU/driver and `nvEncodeAPI64.dll`. SDK headers/notices remain in `third_party/NVIDIA/VideoCodecSDK`.
- Windows Media Foundation AAC/MP4. Qt 6.8.3 dynamic MSVC x64 under `third_party/Qt`; Qt Multimedia deploys its FFmpeg playback runtime. No CUDA Toolkit or external recording muxer is required.
- VS Code Microsoft C/C++ extension (`ms-vscode.cpptools`). Python 3 is used by setup/verification tools, not the app.
- Playback runtime builds also require Ninja (Visual Studio's C++ CMake tools include it; alternatively `winget install Ninja-build.Ninja`). CMake rebuilds only the Qt Multimedia core with the documented WASAPI timer repair and optional playback diagnostics, caching it under `build\qt-output-runtime-ninja`. The pinned Qt source archive and automatically downloaded, hash-checked Khronos Vulkan headers are build inputs; no Vulkan runtime is required. The installed Qt SDK and its FFmpeg/QML plugins remain unmodified. Python 3.12+ is required for archive extraction. On a non-ASCII project path, Qt 6.8.3's source tools also need an existing Windows short-path alias; this workspace has one. The builder fails clearly if that alias is unavailable.

Open this PandaClip folder in VS Code. Existing dependencies are installed; for a fresh checkout:

```powershell
py -3 tools\install_qt.py
py -3 tools\qt_sources.py
cmake -S . -B build\redot-dev -G "Visual Studio 17 2022" -A x64 -DREDOT_BUILD_GLASS_LAB=OFF
cmake --build build\redot-dev --config Debug --target ReDot --parallel
cmake --build build\redot-dev --config Release --target ReDot --parallel
& '.\build\redot-dev\Release\ReDot.exe'
```

The Scenes preview delivery uses the separate `build\redot-scenes-preview` directory so existing running executables can remain untouched:

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\ReDot\build\redot-scenes-preview\Release\ReDot.exe'
```

**Ctrl+Shift+B** builds Debug. **F5 > ReDot (Debug)** builds/debugs `build\redot-dev\Debug\ReDot.exe`. **Terminal > Run Task > CMake: Build Release** builds Release above. Task labels/debugger behavior are preserved. Build/F5 does not run tests. Close only the executable being rebuilt normally first, allowing saves to finalize.

Keep the complete deployed folder: Qt DLLs, plugins/QML, notices, corresponding source archives and `licenses\Qt\changes` containing the playback patch/build recipe. Qt stays dynamically linked; no GPL-only runtime module was added. Other PCs need the [Microsoft x64 C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe), NVIDIA driver and Windows media components. Debug requires the development runtime.


## Scenes Stage 2 delivery

Every shipping preview, recording and replay consumes the selected scene's single D3D11 compositor. Capture now selects a **scene**; configure window/game/display, camera, image/text and audio sources in Scenes. The obsolete direct-window recording route was removed after generated integration checks passed. Migration version 2 preserves existing scenes and converts prior Capture source/audio preferences without deleting recordings or preferences.

Debug and Release builds and the complete quiet regression suites passed. Scene integration passed 95 assertions, including decoded recording/replay pixels, scene switching, audio routing, 17-track decoding, migration and controlled recovery. Deployed Release windows were inspected at normal/small sizes and 150% Qt scaling with no foreground activation. See [Stage 2 architecture, exact changed files, measurements and acceptance checks](docs/scenes-stage2.md). The generated 1080p60 benchmark measured 2.39 ms mean CPU composition and 0.85 ms mean GPU query time for one layer, zero queue drops, and 49 repeated composed inputs over 30 seconds. It is not GD performance evidence. Real WGC yielded no moving frames on the inactive verification desktop; actual GD, camera, audible audio and physical Windows DPI remain manual checks.

## Configure a scene

Keep the existing four-panel editor: scenes left, canvas center, sources right, audio mixer below. Add **Game Capture** or **Window Capture** using the real running-application picker; search for Geometry Dash and select its exact window. Add Display Capture using the monitor list, Video Capture Device using the camera list, alpha Image, or editable Text. Source errors are shown in the Sources panel; unavailable sources are blank while other layers continue. Game Capture uses WGC window capture, not injected game hooks.

Drag to move, handles to resize/rotate, Shift for free resizing or rotation snapping, Alt+handle for crop, Ctrl to bypass alignment snapping. Context menus provide alignment and Advanced Transform. Hidden layers disappear from actual output; locked layers cannot be edited. Editor guides are drawn after the canonical frame, only on the editor swapchain. Both Capture and Scenes show the same scene; mismatched scene/output aspect ratios use letterboxing.

Add audio in Scenes: Microphone (exact endpoint or Windows eConsole default), Desktop Audio (explicit whole-system loopback), or Application Audio (selected executable/window). Gain/mute changes and switching between existing scenes are live. Changes to audio endpoint, sync, or the session's source set apply on the next recording/replay session and display a warning. Microphone Check opens only the first microphone assigned to the selected scene, reports its actual endpoint/raw meter, and produces no speaker monitoring.

Scene configuration lives in `%LOCALAPPDATA%\PandaClip\scenes-v1.json`; recording preferences and Qt geometry remain separate. Migration version 2 preserves existing scenes/selection; previous capture settings become Gameplay or a separate Migrated Capture scene. An unresolved target stays unavailable and is never replaced by another application.

For a separate review profile, with no automatic recording, audio or shortcuts:

```powershell
& .\tools\review_scenes.ps1 -Demo
```

The demo contains owned image/text content and an explicitly unavailable game target. Select your real game and add audio yourself before recording. It does not prove game capture or microphone speech.

Build and run the explicit quiet suites (F5 does not run tests):

```powershell
cmake -S . -B build\scenes-stage2 -G "Visual Studio 17 2022" -A x64 -DREDOT_BUILD_REPLAY_STRESS=ON -DREDOT_BUILD_GLASS_LAB=OFF
cmake --build build\scenes-stage2 --config Debug --parallel 4
cmake --build build\scenes-stage2 --config Release --parallel 4
& .\tests\run_quiet_tests.ps1 -BuildDirectory build\scenes-stage2 -Configuration Release -SkipBuild
```

Stage 1 [editor usability](docs/scenes-editing.md) and [original evidence](docs/scenes-stage1.md) are historical; their editor-only recording boundary is superseded.

## Desktop audio repair and lossless listening checkpoints

The Scenes delivery includes the previous audio repair and keeps Acrylic, recording controls, replay encoders and VS Code tasks. For normal use:

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\ReDot\build\redot-stage4-accessibility\Release\ReDot.exe'
```

The previous four-WAV capture tool/evidence remain available in [desktop-audio.md](docs/desktop-audio.md). That tool launches the retained pre-scenes audio-repair build and validates its original two-source capture path. It is a historical diagnostic, not a Stage 2 multi-source listening test. The scene pipeline reuses the repaired converter and integer PCM timeline; test actual new scene tracks using the checklist below.

## Appearance and windows

Supported **DWM Desktop Acrylic** supplies blur behind each supported window. Thin lavender edges, neutral dark tint, restrained hover states, Segoe UI and custom captions use one design system. Text, thumbnails, video and controls stay sharp. This is compositor blur/tint, not desktop refraction.

Windows Transparency effects, high contrast, compositor support and ReDot **Reduce Transparency** determine availability. **Losing input focus no longer selects a solid background**, including clicks on another monitor. Root alpha, panels and controls are independent of foreground-window identity. Focus notifications now update diagnostics only; they never reapply the native backdrop or margins. Unchanged effective-policy notifications are cached. Custom chrome maintains the same nonclient visual policy via WM_NCACTIVATE while explicitly permitting real deactivation; WM_ACTIVATE/WM_ACTIVATEAPP continue normally and no focus/activation API is called. The user confirmed the new Release stays consistent during their real PandaClip-to-YouTube two-monitor focus transition. Native policy probes measured zero backdrop reapplications across 12 focus changes, compared with 12 extra applications in the pre-edit helper. Owned dropdowns/dialogs retain parent ownership; Quick Settings are child overlays. Appearance/Diagnostics still report accessibility/support failures; API acceptance alone is not visual evidence. **Open Windows transparency settings** appears when Windows disables the effect. ReDot never changes global preferences. Reduce Motion honors Windows' client animation preference. Old theme/preset/material keys cannot restore another style. Windows may still impose power, remote-session or compositor restrictions; no screen-capture substitution is used.

Qt owns the full custom client. DWM owns the single rounded outer contour and subtle border; duplicate Qt outer/caption rims and redundant control material borders were removed. Native resize/system styles, hit testing and maximize work-area bounds are preserved. Advanced detached docks/native menus use the same solid palette.

**Physical two-monitor check:** `tools\review_appearance.ps1` launches this Release with isolated preferences and disabled initial audio/replay. Put it on monitor 1; click ReDot, then YouTube on monitor 2 without minimizing ReDot. Repeat with Recording gear/source choices and Settings. Glass, tint and controls should remain unchanged; moving background content may naturally change the live blur. The user confirmed this physical click sequence works with the new candidate. Automated runs preserve personal focus; they test thread activation/policy and deployed controls separately. Physical Windows scaling checks at all 100%, 125%, 150% remain manual; Qt scale overrides were tested at each value.

## Record and save Geometry Dash clips

1. Launch this Release. Open **Scenes**, select/create **Gameplay**, Add Source → **Game Capture**, search/select **Geometry Dash**, then Add Source. Keep GD visible/running. Check the source status and live preview; if unavailable, resolve the exact selected window rather than recording another one.
2. Add **Application Audio** for GD and **Microphone** for the exact HyperX input. Desktop Audio captures the whole selected output endpoint only when explicitly added. Confirm mute/gains in the scene mixer; check raw mic activity using the microphone gear's Check Mic before recording.
3. Open **Capture** and choose Gameplay in the scene capsule. Recording gear: begin with **1920×1080, 60 FPS, Balanced P4**, Mix + separate tracks and the desired output folder. Encoder changes are locked during sessions.
4. **Start Recording**, then Stop. NVENC/AAC drain and MP4 finalization precede Saved. Hide preview does not stop encoding. Borderless consent is asynchronous; denied access permits outlined capture. Windows can still require an outline because of another active capture.
5. **Enable Replay Buffer**, let history warm, then **Save Clip** or the configured shortcut (default Ctrl+Shift+F9). Replay stores compressed packets, exports without re-encoding and shares encoder output with ordinary recording. Session/configuration changes reset history explicitly.
6. Switch scenes or change layer position/visibility while recording/replay runs: subsequent composed frames change, with fixed encoder dimensions/FPS and continuous timestamps. Missing sources become blank; other sources continue.

Default audio order is **Mix**, then one stem per enabled referenced scene source, in persisted source-definition order. All sources used by any scene are planned at session start (up to 16 stems); unassigned/muted sources are silent in the current scene. One physical endpoint/application capture is shared between aliases and mixed once. Track flags filter game/desktop/application stems or microphone stems; Mix-only/video-only are supported. AAC is 48 kHz stereo. Sidecar `.tracks` labels preserve scene source names; keep them with the MP4. Listen to the exact named track externally because players may choose different defaults.

Replay starts at a retained H.264 IDR, preserves dependencies/complete AAC packets, and uses one common timestamp offset. Keyframes, packet boundaries, warm-up and memory pressure affect actual duration. Sync defaults to zero: positive milliseconds delay source PCM, negative advance it; apply between sessions. Shared aliases must use the same sync. Synthetic flash/beep offsets are never blindly subtracted.

Five separate gears cover Recording, Replay, Save Clip, Game audio and Microphone. One bounded anchored nonnative child overlay opens at a time, with Escape/outside dismissal, bounded keyboard navigation and focus restoration. Owned native choice lists retain parent ownership. The native preview is clipped only beneath an open overlay; encoding continues. Wheel scrolling over editable controls does not change their values. Settings retains one viewport per category, cached device enumeration, explicit refresh and debounced persistence. Dashboard/advanced layout remains independent of recording preferences.

## Verification and remaining manual checks

### Earlier playback-stop repair (crackling persists)

The user reports the same MP4 sounds clean outside PandaClip. Investigation therefore targeted playback, preserving capture, mixing, AAC encoding and replay. Qt 6.8.3's Windows output timer could stop WASAPI with frames still queued: the HyperX **output** endpoint test measured 32 premature stops in 40 trials, up to 321 frames (6.69 ms) retained. The repaired DLL rounds waits up and rechecks actual padding before declaring an underrun: 0/40 in both Release and Debug. Track changes also avoid a redundant seek/second renderer flush. No noise suppression, artificial smoothing or additional buffering was added.

See [historical timer repair and verification](docs/playback-audio.md). The user confirmed this repair **did not eliminate audible distortion**. The [current investigation](docs/player-output.md) adds decoded, converted and actual device-submitted checkpoints, independent Windows decoding and sample-accounting regressions. The audio-only WinMM trial also failed the user's listening check and was removed from production. The user then confirmed the **independently decoded Mix WAV also distorts externally**, establishing distortion before PandaClip's output; this historical playback-only investigation was superseded by the documented continuous-SRC desktop capture correction and user listening result. No suppression was applied.

For a quiet-profile **manual listening review**, run the command below, click Media and explicitly select Microphone then Mix. It plays nothing automatically, copies representative existing clips and leaves personal recording/replay sessions intact. Close only the review window normally to finalize its diagnostic trace. Windows' native reader and Qt can default to different audio stems; compare the same named track externally.

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\ReDot\tools\review_playback.ps1' -Output wasapi
```

[The playback report](docs/playback-audio.md) lists this repair's builds and checks; [the focus repair report](docs/focus-policy.md) retains the prior appearance evidence. Quiet checks use owned inactive desktops/processes and project-local profiles/output, without personal input/focus, global shortcuts or sounds. Generated GPU/PCM proves control/encoding behavior, not actual game cadence or audible capture. Previous appearance/scaling results and the prior harness guard incident remain documented in [the historical report](docs/appearance-stability.md).

Existing full quiet suite (explicit; not run by F5):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\run_quiet_tests.ps1 -BuildDirectory build\redot-dev -Configuration Release
```

- Click the app to inspect active Acrylic; test physical drag/snap/resize, captions/gears/sliders, current scale/150% and reduced transparency/motion.
- Speak during Check Mic, record game-only/mic-only/both, listen to Mix/Game/Mic separately, and physically remove/reconnect the selected mic. Nonzero samples/decoding do not establish audible speech or application loopback.
- Record moving GD gameplay and compare capture-off/replay-on frame times/export smoothness. Synthetic/idle sources are not gameplay performance evidence. Physical hotkeys, visible border removal and perceptual synchronization remain manual.

The existing bounded interruption recovery, endpoint pinning, error reporting and diagnostics remain. Failed/incomplete outputs are not reported Saved. HDR/software H.264 fallback, trimming/editor, automatic highlights, cloud upload and noise processing are not implemented. NVIDIA NVENC is required.

Logs: `%LOCALAPPDATA%\PandaClip\logs`. Preferences: `settings-v1.txt`; separate layout: `workspace-layout-v1.txt`. Preferences, recordings and historical evidence were preserved. [Historical notes](docs/frontend-history.md) retain detailed recovery/sync measurements, manual GD benchmark and optional MSIX borderless instructions.
