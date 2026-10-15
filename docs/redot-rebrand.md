# ReDot Stage 3: branding and compatibility

## Subsequent manual acceptance (2026-10-10)

The user reports all seven Stage 3 checks passed: application name, logo, taskbar color transitions, recording and replay together, preserved scenes/settings, Geometry Dash recording/audio, and taskbar behavior when minimized. This updates current acceptance status; the original automated measurements and original remaining-check statements below are retained as historical evidence. It does not imply physical device removal, every Windows DPI setting, extended synchronization or a long-session load benchmark was performed. Stage 4 is documented separately in [redot-stage4.md](redot-stage4.md).

## Scope

The existing scene-based application is rebranded; its layout, Acrylic policy, scene compositor, recording, replay, audio continuity and playback implementation remain unchanged. The original Stage 2 build remains at `build/scenes-stage2/Release/PandaClip.exe`. Source checkpoint: `build/checkpoints/redot-stage3-20261010-121355/source.zip`.

## Assets and identity

The maintained vector is `branding/redot.svg`: one rounded replay arrow and one independently addressable `status-dot`. Existing Qt Gui generates transparent PNGs and native multi-resolution ICO resources; no new runtime module or third-party logo dependency was added. Build/deployment assets live in `build/redot-stage3/Release/branding`: idle, replay and recording SVG/ICO variants and PNGs at 16, 20, 24, 32, 40, 44, 48, 50, 64, 96, 128, 150 and 256 pixels. Only the dot differs. Neutral lavender fits both dark and light surfaces; green and red are taskbar status assets, not alternate themes.

Changed user-facing identity: main/Settings/Diagnostics/Media captions; About; tray tooltip/Open action; initialization/permission/diagnostic messages; new Text-source default; QApplication display/version; embedded Windows product/company/description/original-filename resources; MSIX display names, description and logos; VS Code debug configuration; build target and executable; current launch/review/test scripts. New recordings use `ReDot_…`, new replay exports `ReDot_Clip_…`; the library recognizes both old and new prefixes. Existing filenames and user-authored Text sources are retained.

## Taskbar state

`Branding::engineMode` reads the existing worker's ready flag, stopping flag, completed encoded-frame count and replay-only mode via the controller snapshot. Startup, failure, stopped/draining workers are neutral. A ready replay worker with an encoded frame is green. A ready ordinary recording is red, including shared recording/replay. Returning to replay uses the existing drain/restart path; the dot is briefly neutral while that worker initializes, then green after actual encoding. There is no extra engine polling timer.

`Branding::TaskbarIcon` caches the three variants and size-specific owned HICONs, installs both native small/large icons on the actual workspace HWND using WM_SETICON while Qt keeps the neutral application identity; HWND recreation reapplies the current native status. An unchanged mode/HWND/DPI performs no native icon writes. DPI/HWND recreation and Explorer taskbar reconstruction invalidate the cache application, not the resources. Focus events do not update icons. Captions, About/Media and the existing tray retain the neutral logo; no new tray state rules were introduced.

Unpackaged ReDot uses `ReDot.Desktop` for taskbar grouping; packaged builds retain the package-provided identity. Existing pinned PandaClip shortcuts still point to the preserved old executable. Pin ReDot yourself after launching the new build; no shortcuts or Windows preferences are silently modified.

Native references: [WM_SETICON](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-seticon), [explicit AppUserModelID](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-setcurrentprocessexplicitappusermodelid), [QWindow::setIcon](https://doc.qt.io/qt-6/qwindow.html#setIcon). Native-icon inspection is not a screenshot of Windows' physical taskbar.

## Data preservation

There is deliberately **no settings-directory migration**. ReDot continues using `%LOCALAPPDATA%\PandaClip`: `settings-v1.txt`, `workspace-layout-v1.txt`, `scenes-v1.json`, logs and existing library metadata. Existing output-folder preferences and the historical default `Videos\PandaClip` remain. Scene/source IDs, relationships, migrationVersion 2 and serialization magic values remain compatible. No recordings or preferences were deleted, renamed or copied over personal data.

Internal compatibility names retained: serialized `PandaClip settings 1`, `PandaClipScenes`, `PandaClip tracks 1`; internal HWND classes, environment/test hooks, log mutex and qrc namespace; existing optional signed MSIX Identity `PandaClip.Dev`, ApplicationId `PandaClip`, certificate Publisher `CN=PandaClip Development`. MSIX's displayed product is ReDot and executable ReDot.exe. Retaining identity avoids creating a second installed product or invalidating an existing certificate solely for branding; package version advances to 0.5.2.0 (PE/application version 0.3.0). The package is optional, and installation/signing is not performed by the rebrand.

## Verification

Debug and Release built successfully with MSVC x64 / Qt 6.8.3. Complete quiet suites passed in both configurations: `build/quiet-tests/run-20261010-124326-Release` and `build/quiet-tests/run-20261010-124326-Debug`. Tests use owned inactive desktops or passive windows, isolated preferences/outputs under build, and no personal input, foreground activation, capture sessions or audible stimuli. User-reported Stage 2 GD success is prior manual evidence, not a newly performed ReDot gameplay test.

### Measured checks

| Check | Result |
|---|---|
| Scene editor/model | 104 assertions at each Qt scale 1.0/1.25/1.5, in both builds; zero failures |
| Scene integration | 94 assertions per build: GPU composition, switched scenes, persistence/migration, audio routing, recording/replay and 17 audio tracks |
| MP4 decoding | 31 successful independent decode checks per complete suite; generated/component fixtures, not real gameplay |
| Audio/playback | Continuous SRC and four lossless checkpoint comparison, meters, AAC probe, track selection, seeks/pause/resume, PCM sample-accounting/comparator regressions passed; automated output quiet |
| Replay/recovery/faults | Bounded eviction/snapshots, export, repeated sessions, three-attempt recovery and controlled write failures passed |
| Native branding | Neutral/green/red HICON pixels; both sizes; dot-only pixel differences at 16/24/32/48/256; minimize/restore, HWND recreation and simulated Explorer reconstruction passed |
| Repeated icon updates | 1,000 unchanged states: zero icon writes. After two-second Qt startup warm-up, 900 transitions: Release GDI 74→74 / USER 64→64; Debug GDI 74→74 / USER 65→65 |
| Actual encoder transitions | Both builds logged neutral→red→neutral→green→neutral→red with replay→neutral during worker restart→green→neutral; native taskbar status checked before disabling restarted replay |
| Focus policy | 12 real thread activation changes on the inactive owned desktop: native backdrop apply count remained 1; unchanged settings notification remained 1, in both builds |
| Deployed Release | Actual HWND screenshots and five gears/Settings/Media/Scenes controls at Qt scales 100/125/150%; small/maximized/restored windows; zero observed foreground activation |
| Optional MSIX | MakeAppx schema validation/package creation passed: `build/packages/package-20261010-123821-a058e789/ReDot.msix`; unsigned, never installed |

Full logs: `build/redot-stage3/quiet-release-final-pass.log` and `quiet-debug-final-pass.log`. Engine/icon traces are `ui-generated/branding-transitions.txt` in each suite root. Additional rejected-start fault checks are under `build/redot-stage3/fault-release-final` and `fault-debug-final` (explicit missing-output-folder errors, not simulated driver removal). Initial failed verification attempts and artifacts were retained; stale filename expectations were corrected. Initial GUI resource growth settled during startup; steady-state counts were then measured rather than treating lazy Qt initialization as a proven leak.

Actual deployed screenshots: `build/redot-stage3/deployed-100/capture-blue.png`, `media-blue.png`, `scenes-deployed.png`, `settings-deactivated.png`, all five `gear-*-open.png`; corresponding 125%/150% captures are in `deployed-125`/`deployed-150`. These use isolated profiles and owned coloured backgrounds. They demonstrate deployed identity/controls; they are not screenshots of your physical taskbar or proof of a new active two-monitor check. Existing Acrylic/focus implementation was not changed. No personal gameplay/audio was captured as evidence.

Release SHA-256: `f9004eda4fb427e6d7ee525a84f6c79f995df18c706c0609446ab2e1b48767d7`. Preserved Stage 2 executable SHA-256: `e9036162a9f743da0a591655ed1de8b7d91a48ffb8e9d9621a32a2e6226a7f46`; the pre-existing process was left running.

### Remaining limits

Native WM_GETICON pixels/state transitions are verified, but Explorer's physical taskbar rendering/cache, physical multi-monitor moves, snap gestures and Windows display-scale changes remain manual. Qt scale overrides do not prove physical DPI changes. Actual moving GD, camera hardware, loopback audibility and listening to Desktop/Mix/Microphone remain your acceptance checks; prior Stage 2 manual success is user-reported evidence. Startup rejection tests cover invalid output configuration, not removal of the installed NVIDIA driver. Archived recordings may still contain PandaClip in their filenames or user-authored scene text, and internal compatibility names intentionally remain. The preserved old build and old pinned shortcuts are not renamed.

## Manual acceptance

When your existing recording/export is finished, close the old app normally before using the shared personal preferences in ReDot. Launch `build\redot-stage3\Release\ReDot.exe`, keeping its deployed Qt DLLs/plugins alongside it. With your existing selected scene, verify idle neutral → replay green → ordinary recording red → both red → stop recording → replay green after restart → disable replay neutral. Confirm the dot during minimize/restore, another-monitor focus changes and physical display scale changes; check any pinned shortcut launches ReDot. Listen to a short new recording and replay, including Desktop and Mix, and confirm scene switching still behaves as in Stage 2. These physical taskbar/gameplay/listening checks remain manual unless explicitly reported otherwise.

## Files changed

Exact checkpoint comparison (no unrelated files deleted):

- `src/application_picker.cpp`
- `src/audio_capture.cpp`
- `src/clip_library.h`
- `src/diagnostics.h`
- `src/frontend_adapter.inl`
- `src/frontend_bridge.h`
- `src/frontend_qt.cpp`
- `src/main.cpp`
- `src/recording.cpp`
- `src/replay.cpp`
- `src/scene_editor.cpp`
- `src/settings_store.h`
- `src/workspace_ui.inl`
- `qml/Media.qml`
- `tests/prepare_qt_profile.py`
- `tests/prepare_scene_profile.py`
- `tests/qt_fault_checks.inl`
- `tests/qt_frontend_checks.inl`
- `tests/qt_quiet_launcher.cpp`
- `tests/qt_wgc_checks.inl`
- `tests/run_audio_border_checks.ps1`
- `tests/run_m6_checks.ps1`
- `tests/run_m6_daily_checks.ps1`
- `tests/run_m6_gallery_checks.ps1`
- `tests/run_m6_settings_edges.ps1`
- `tests/run_m6_smoke.ps1`
- `tests/run_quiet_tests.ps1`
- `tests/run_reliability_test.ps1`
- `tests/run_replay_sync_test.ps1`
- `tests/run_settings_ui_checks.ps1`
- `tools/review_appearance.ps1`
- `tools/review_capture_audio.ps1`
- `tools/review_playback.ps1`
- `tools/review_scenes.ps1`
- `packaging/AppxManifest.xml`
- `packaging/New-MSIX.ps1`
- `.vscode/launch.json`
- `.vscode/tasks.json`
- `CMakeLists.txt`
- `README.md`
- `branding/redot.svg`
- `src/brand_state.h`
- `src/brand_icon.h`
- `src/redot.rc.in`
- `tools/brand_assets.cpp`
- `tests/branding_test.cpp`
- `docs/redot-rebrand.md`
- `docs/QT-NOTICE.txt`

