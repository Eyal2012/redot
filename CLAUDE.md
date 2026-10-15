# ReDot

Windows screen/game recorder with a rolling replay buffer. C++20, MSVC, CMake, Windows Graphics Capture, Direct3D 11, direct NVENC H.264, WASAPI audio, Media Foundation AAC/MP4, Qt 6.8.3 (dynamic). No OBS/libobs. Formerly named PandaClip.

**All development rules live in [AGENTS.md](AGENTS.md); read it first and do not duplicate or contradict it here.** This file is only an orientation.

## Architecture

Sources (WGC window/game, display, camera, image, text; WASAPI mic/app/desktop audio) → selected **scene** → single D3D11 compositor → native preview + NVENC + AAC → MP4 (ordinary recording) or compressed H.264/AAC packet ring (replay, exported without re-encoding). Qt Widgets owns Capture/Scenes/Settings/Diagnostics; Qt Quick owns the Media window. Hidden Win32 controls are state adapters only.

## Build and test

```powershell
cmake -S . -B build/<fresh-name> -G 'Visual Studio 17 2022' -A x64 -DREDOT_BUILD_REPLAY_STRESS=ON
cmake --build build/<fresh-name> --config Debug --parallel
cmake --build build/<fresh-name> --config Release --parallel
& tests/run_quiet_tests.ps1 -BuildDirectory build/<fresh-name> -Configuration Debug -SkipBuild
& tests/run_quiet_tests.ps1 -BuildDirectory build/<fresh-name> -Configuration Release -SkipBuild
```

Run suites sequentially. VS Code: Ctrl+Shift+B builds Debug; F5 "ReDot (Debug)" uses `build/redot-dev/Debug/ReDot.exe`. Use a new `build/` subfolder after moving the workspace (old CMake caches hold old absolute paths). Default verification is quiet (no input injection, no personal sessions, artifacts under `build`).

## Source map

- `src/main.cpp`, `src/workspace_ui.inl`, `src/frontend_adapter.inl`: app entry and hidden Win32 state adapter.
- `src/frontend_qt.cpp`, `qml/`, `src/design_system.h`, `src/window_chrome.h`, `src/desktop_backdrop.h`: Qt UI, shared design tokens (`qml/design-system.json`), Acrylic/chrome.
- `src/recording.cpp`, `src/replay.cpp`, `src/audio_capture.cpp`, `src/timeline.h`, `src/audio_timeline.h`: capture, NVENC, MP4, replay ring, audio clock/resampling.
- `src/scene_*.cpp/h`, `src/application_picker.cpp`: scenes model, editor, compositor runtime, audio routing.
- `src/settings_store.h`, `src/clip_library.h`, `src/logging.h`, `src/diagnostics.h`: persistence, media library, background logging.
- `updater/`, `docs/updater.md`, `tools/New-PortableRelease.ps1`: standalone one-click `ReDotUpdater.exe` (separate CMake project, no Qt), signing tool and signed-ZIP release script.
- `tests/` (quiet suites, `run_quiet_tests.ps1`), `tools/` (review/measurement scripts, Qt setup), `packaging/` (MSIX), `docs/` (history and evidence), `lab/` (Glass Lab, off by default).

## Approved UI behavior (do not change unprompted)

Capture capsule selects a scene; five distinct Quick Settings gears; sharp native D3D11 preview with correct aspect; Desktop Acrylic with solid/accessibility fallbacks; custom captions with native hit testing; system tray and taskbar status dot (neutral idle, green replay, red recording); Media sorting incl. size. No gallery/highlights/editing work until requested. Ordinary recordings are MKV by default (MP4 alternative); replay exports stay MP4 (`docs/mkv-recording.md`).

## Data compatibility

Settings, scenes, layout and logs stay under `%LOCALAPPDATA%\PandaClip\`, with original file headers/format tags; default output `Videos\PandaClip`; both `PandaClip_*` and `ReDot_*` clip names are read. See "Project identity and data compatibility" in AGENTS.md. Never delete or move personal data; migrations need backup and rollback.

## Preserving working functionality

Keep the approved `build\redot-scenes-preview\Release\ReDot.exe` and `build\redot-final\Release\ReDot.exe` intact. Build new work in fresh directories, make small changes, and run the quiet suites before claiming anything works. Generated-source tests do not prove real WGC capture, device audio or listening sync.

## Testing time limit (hard rule)

- **Maximum 10 minutes of total testing per task**, covering Debug, Release, integration tests and retries combined. This is not per test. Building is separate; avoid unnecessary rebuilds.
- Start with the smallest focused tests relevant to the change and reuse existing results; do not restart tests that already passed.
- Never run long benchmarks, soak/stress tests or repeated full regression suites unless the user explicitly requests them.
- When the limit is reached, stop and report what passed, failed and remains untested. Do not leave background test jobs running past the limit.
- Do not spend extra time fixing test infrastructure unless the feature directly requires it.
- Leave real gameplay and UI verification to the user's manual checks.
