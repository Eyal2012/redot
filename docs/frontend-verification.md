# Dock frontend verification

## Current: shared View Media glass and capture handoff

Use [the shared-glass report](shared-glass.md) for current screenshots, functional causes, measured costs and actual Debug/Release results. Launch `build\shared-glass\Release\PandaClip.exe`. Previous builds and reports remain intact.


## Archived: frontend controls, meters and custom chrome

Use [the frontend-fix report](frontend-fixes.md) for confirmed causes, final Debug/Release checks, captured WASAPI meter values, native caption geometry checks, full-window screenshots and remaining manual checks. Launch `build\frontend-fixes\Release\PandaClip.exe`. Older outputs and already running sessions remain intact.

## Archived smoked-purple refinement (October 6, 2026)

The earlier delivery was **`build\dock-frontend\Release\PandaClip.exe`**. Its [visual/functional verification report](purple-refinement.md) preserves actual before/after screenshots, decoded video plus glass, microphone/hotkey checks and measured rendering costs. The native-child screenshot limitation below applies to the archived first rebuild.

## Archived initial Qt rebuild (October 5, 2026)

The following report and its screenshots describe the first Qt rebuild. Current presentation, playback dependencies and measurements are documented above; the earlier evidence remains intact.

## Delivery

Launch **`build\dock-frontend\Release\PandaClip.exe`** from this workspace. Keep the deployed DLLs, plugins, QML and `licenses` directory alongside it. VS Code's existing Debug task and F5 configuration now target `build\dock-frontend\Debug\PandaClip.exe`.

This is a substantial visible frontend replacement: Qt Widgets owns the opaque recording workspace and real docks; Qt Quick owns the separate media window. The custom WGC/D3D11/direct NVENC/WASAPI/AAC/replay pipeline remains. A hidden native controller is retained as a lifecycle/settings adapter; its previous pages are not shown. Further separation of controller state from native controls is still possible.

Qt 6.8.3 MSVC 2022 x64 is dynamically linked. `tools/install_qt.py`, `tools/qt_sources.py`, CMake deployment, LGPL notices and corresponding unmodified source archives are included. See [setup and licensing](../README.md#dependencies-setup-and-deployment) and [Qt notice](QT-NOTICE.txt).

## Screenshots and visual inspection

These are actual running-app captures on an inactive, owned desktop, using a generated GPU source and independently encoded clips. They are not design mockups or Geometry Dash footage.

| View | Screenshot | Inspection |
| --- | --- | --- |
| Recording workspace | [Workspace](frontend-screenshots/workspace.png) | Large aspect-preserving canvas; compact Sources left, Mixer below, Controls bottom right; opaque charcoal; one font family; thin separators. |
| Floating layout | [Workspace after floating](frontend-screenshots/workspace-floated.png), [floating mixer](frontend-screenshots/mixer-floated.png) | Actual QDockWidget floating state, not a decorative panel. Separate widget captures omit OS window frames. |
| Settings | [Settings](frontend-screenshots/settings.png) | Separate seven-category dialog, moving content viewport, bounded compact form and persistent footer. |
| View Media | [Glass](frontend-screenshots/media-glass.png), [solid option](frontend-screenshots/media-solid.png) | Own-content blur and tint are visibly different from the solid option; bright edges, rounded floating controls and sharp thumbnail cards. |
| Playback controls | [Playback](frontend-screenshots/playback.png) | Glass controls, seek, audio choice and external-player action. **The native MFPlay child video surface is omitted by the Qt GPU grab. The black rectangle does not establish whether playback pixels were visible.** |

Workspace captures combine an actual Qt widget grab with a single readback of the actual preview swap-chain backbuffer. Readback is compiled only into the verification target; shipping presentation does not copy preview frames to CPU images. Media captures use `QQuickView::grabWindow`, including the actual GPU-rendered glass, without replacing missing video pixels with a poster.

The attachment contained request text but no OBS/PandaClip reference images. One request for the missing references received no response during implementation. The workspace was evaluated against the written structure and proportions; **direct comparison with the user's OBS image remains unavailable**.

The media treatment adapts [Apple's material guidance](https://developer.apple.com/design/human-interface-guidelines/materials) and [Liquid Glass overview](https://developer.apple.com/documentation/technologyoverviews/liquid-glass) to Windows. It does not claim native Apple material. [Qt MultiEffect](https://doc.qt.io/qt-6/qml-qtquick-effects-multieffect.html) blurs bounded, half-resolution regions of the media scene, with tint, masking and edge highlights. Scene updates are coalesced. Playback controls sample the selected clip's poster/background rather than copying the live video into a blur texture.

## Verification scope

All tests used owned processes and fresh profiles/output/logs beneath PandaClip's `build` directory. No input-desktop switch, global keyboard or mouse injection, physical pointer movement, foreground activation or audible cue was performed. Existing personal PandaClip instances were neither closed nor controlled. Silent MFPlay is muted before opening media; generated PCM cues are encoded without playing them through an output device.

### Builds and runtime deployment

- Debug and Release were built with MSVC, C++20 and CMake in `build/dock-frontend`; final build logs contained no compiler warnings/errors.
- The actual shipping executable, not only the verification variant, was opened and closed on an inactive desktop with an SDK-free Windows-only `PATH` and cleared Qt/QML plugin-path environment variables. The deployed workspace appeared and exited successfully.
- Local runtime deployment includes Qt DLLs/platform/QML plugins, shader dependencies and licensing material. This is not a test on a clean second PC; Microsoft VC runtime and the NVIDIA driver remain prerequisites.
- Optional MSIX staging was run: `build/packages/package-20261005-232728-e1c4ff51/PandaClip.msix` passed MakeAppx schema validation and packaging. It includes Qt runtime, notices and corresponding sources. Signing, installation and actual borderless consent were not performed.

### UI and worker checks

The automated Qt checks exercise actual visible frontend objects rather than the hidden native adapter controls:

- Dock visibility, floating, moving, hiding/restoring, layout reset/lock, separate on-disk persistence and restoration in a second process.
- Large central canvas and resizing; preview presentation can be disabled while the real encoder remains active.
- Every Settings category at smaller dimensions; positive scroll range/page size, wheel routing over shortcut/dropdown/numeric editors without value changes, scrollbar slider-position API, Home/End and value retention. Scrolling does not write the settings file.
- Media scene loads, eight clips appear, replay/audio filters and search produce expected counts, actual glass/solid screenshots, three audio-track labels, silent playback readiness/duration, paused seek, track change preserving position, fullscreen/Escape and diagnostic report export.
- Real ordinary, replay-only and shared recording/replay workers while Settings/View Media are open; export without stopping replay; closing media leaves replay active; successful drain/finalization and independent decoding of generated files.

The underlying component suite also exercises bounded replay retention/snapshots, simulated border-access policy, asynchronous logging overflow/write failure, atomic Unicode settings and damaged-file fallback, worker metadata/thumbnails, missing media/folder errors, PCM sync adjustment, AAC production encoding and 12 timestamp-comparator regressions. Simulated access policy does not exercise Windows consent or prove the visible outline is absent.

Direct NVENC/AAC fixtures cover video-only, Game-only, Mic-only and Both configurations: 1280×720 H.264 at 30 FPS; 48 kHz stereo AAC; 0/2/2/3 audio streams (Mix plus enabled stems). Ordinary fixtures run for six seconds; the requested five-second replay yields four seconds at the available IDR. Every stream is independently decoded, with monotonic video and contiguous AAC timestamps. Generated cue offsets are approximately 1.00–1.67 ms. These are PCM/codec/mux/export measurements, **not acquisition, speech, speaker synchronization or gameplay results**.

### Timing methodology

The same owned desktop and synthetic-worker conditions were used for the frontend checks. Each mode sends 100 wheel events directly through Qt's event handler and records handler time; a separate counter times cached controller/UI polling. These timings exclude physical input delivery, painting/compositor latency, end-to-end preview, capture scheduling and gameplay. They must not be compared numerically with the old Win32 `SendMessage` measurements, which include different work.

Final full suites passed with zero failed frontend assertions:

- Release: `build/quiet-tests/run-20261005-232657-Release`.
- Debug: `build/quiet-tests/run-20261005-232707-Debug`.
- Additional process-local `QT_SCALE_FACTOR=1.5` Release run: `build/quiet-tests/qt-scale150-20261005-232747`, effective DPI 144. All scrolling/layout/media/worker assertions passed; the scale setting was restored afterward. [Scaled Settings](frontend-screenshots/settings-150.png) and [scaled workspace](frontend-screenshots/workspace-150.png) were inspected.

Release direct Qt wheel-handler samples at effective DPI 96:

| Mode | Median ms | p95 ms | Maximum ms |
| --- | ---: | ---: | ---: |
| Idle | 0.0035 | 0.0041 | 0.0513 |
| Recording | 0.0255 | 0.1228 | 0.2291 |
| Replay | 0.0035 | 0.0038 | 0.0818 |
| Recording + replay | 0.0035 | 0.0044 | 0.0816 |

Cached controller/UI poll time was 1.666 ms mean / 5.356 ms maximum over 82 ticks (Debug: 3.328 / 21.542 ms over 84). The full Debug and Release suites overlapped, and personal app instances remained running, so these are local handler observations, not an isolated performance benchmark or a claim about user input latency. The separate 150% run measured wheel p95 0.0105/0.0044/0.0060/0.0042 ms for the four modes and poll mean/max 1.530/5.940 ms. No old/new framework speedup is inferred.

Failed intermediate artifacts are retained, including `qt-scale150-20261005-232428`, whose fixed 900 ms track-selection wait failed while layout/scrolling passed. The test now waits for the expected position within an explicit three-second budget and logs completion; final runs observed 2.010/2.000/2.000 seconds after the track change. It does not change playback timestamps or conceal a timeout. Qt emits a quiet-desktop fullscreen warning about `requestActivate` on a window marked `WindowDoesNotAcceptFocus`; tests remain on the inactive desktop. No QML loading/customization errors occurred in the final runs.

Final build/transcript logs: `build/frontend-rebuild/build-release.txt`, `build-debug.txt`, `quiet-release.txt`, `quiet-debug.txt`, `quiet-scale150-final.txt` and `msix-staging.txt`. Each run retains its `checks.txt`, cross-process restore report, shipping smoke report, generated MP4s and screenshots. The dependency install's initial mirror/TLS and ANSI-path updater failures were corrected without disabling TLS; the new install helper's existing-install validation passed. A full cold install of that helper was not independently repeated.

Delivery rebuilds are in `build-release-delivery.txt` and `build-debug-delivery.txt`; the final SDK-free shipping smoke passed in `shipping-delivery-smoke.txt`. The Release executable SHA-256 is `A56627EF1B4DEFDFC4EBAABFF98139618162869132E4B9C65ED896FDEFFCE758`.

## Manual checks still needed

1. Open the new Release executable on the normal desktop. Drag/float/redock panels with the physical pointer, lock/reset layout, Tab through controls, and move windows between your actual display scales/monitors. Process-local Qt scaling is not a physical multi-monitor transition test.
2. Select Geometry Dash in Sources properties. Start capture and inspect actual moving WGC preview/aspect ratio, source resize, border-access status and visible outline. Existing capture/NVENC settings and recovery are preserved but acquisition was not exercised by generated-source tests.
3. In Audio properties choose application loopback explicitly, choose the wanted microphone endpoint, enable its source and use Check mic. Speak and inspect the opened endpoint/raw meter, Windows input mute/volume and PandaClip gain/mute. Listen separately to Mix, Game and Microphone after recording. Physical disconnection/reconnection remains unverified here.
4. Set output folder, start recording or enable replay. Open and close View Media, seek/fullscreen/switch tracks, and check that actual video pixels and audible playback are correct. Try the glass/solid and motion options over bright and dark real clips.
5. Inspect the default save hotkey (`Ctrl+Shift+F9`, or your retained binding), tray behavior and lock/wake/source reopening on the input desktop. Quiet test processes deliberately disable global hotkey registration and tray notifications.
6. For game performance, compare the same continuously moving GD segment with capture off, replay on with preview shown, and replay on with preview hidden. Save repeated clips, decode/listen independently and compare game frame-time distributions and clip smoothness. No synthetic or idle-window measurement here establishes GD overhead.

No trimming, automatic highlights or other recorder dependency was added.
