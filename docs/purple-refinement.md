# Smoked-purple frontend refinement — October 6, 2026

## Delivery

Launch the tested **`build\dock-frontend\Release\PandaClip.exe`**. Keep the deployed DLLs, plugins, QML and `licenses` directory alongside it. VS Code build tasks and F5 still target `build\dock-frontend`. Trimming remains deferred.

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\build\dock-frontend\Release\PandaClip.exe'
```

Qt Widgets still owns the opaque recording workspace and real docks. Qt Quick still owns the separate media window. WGC, native D3D11 preview, direct NVENC, WASAPI, AAC/MP4, shared recording/replay output, bounded history/snapshots, audio stems and hotkeys are preserved. The hidden controller remains an explicit state/lifecycle adapter.

## Visual changes and actual screenshots

These are captures of the running app on an owned inactive desktop, using synthetic GPU/PCM fixtures. They are not mockups or GD footage. Whole-window and close-up images were inspected during iteration.

Workspace captures combine the Qt widget grab with one test-only readback of its actual native preview backbuffer. Media captures use the actual Qt GPU scene; crops contain unchanged pixels from those captures. Shipping preview does not use the verification readback path.

| View | Before | After |
| --- | --- | --- |
| Workspace | [Original Qt workspace](purple-screenshots/before-workspace.png) | [Violet-charcoal workspace](purple-screenshots/workspace.png), [actual mic-open check](purple-screenshots/workspace-mic-check.png), [mixer close-up](purple-screenshots/mixer-floated.png) |
| Media library | [Original material](purple-screenshots/before-media-glass.png) | [Bright/colorful and dark thumbnails](purple-screenshots/media-glass.png), [rim close-up](purple-screenshots/media-glass-detail.png) |
| Player | The previous Qt grab omitted its native child video; it was not complete playback evidence. | [Decoded video plus glass](purple-screenshots/playback.png), [toolbar close-up](purple-screenshots/playback-detail.png) |
| Contrast | — | [Bright decoded frame](purple-screenshots/playback-bright.png), [dark decoded frame](purple-screenshots/playback-dark.png) |
| Popover | — | [Actual filter menu](purple-screenshots/media-popover.png), [menu close-up](purple-screenshots/media-popover-detail.png) |
| Accessibility / empty state | — | [Reduced transparency](purple-screenshots/media-solid.png), [empty library](purple-screenshots/media-empty.png), [Settings](purple-screenshots/settings.png) |

The workspace uses #15131B background, #211D2A panels, restrained #B89AF6 accents, readable lavender-gray secondary text, softly lit dock headers and thin separators. Sources actions sit close to its compact list; long names elide with full tooltips. Mixer endpoints, dBFS meters, gains and explicit **Muted** states stay readable. Red is reserved for recording/errors. The new default bottom mixer spans the available width; custom saved dock arrangements are restored, not forcibly replaced.

View Media has floating navigation, filter menus and playback controls above sharp content. The material follows the local-content, legibility and restrained-layering ideas in [Apple's official Liquid Glass session](https://developer.apple.com/videos/play/wwdc2025/219/). It is a Windows implementation inspired by that material, not native Apple Liquid Glass. The linked [HIG materials page](https://developer.apple.com/design/human-interface-guidelines/materials) required client-side rendering here; the accessible official session transcript supplied the substantive guidance.

Each panel samples the scene at its **own current coordinates**, blurs a bounded half-resolution region, then applies a compiled shader: adaptive smoke tint, rounded mask, uneven directional rim light, faint inner reflection and 2.6-pixel edge refraction. Shadows separate it from the content. Bright surfaces receive additional dark tint for text contrast. There are no particles, rainbow gradients, idle decorative motion, desktop sampling or capture-preview blur/readback. Reduce Transparency and Reduce Motion are correctly inverted user options with persistent underlying preferences.

### Compositing problems fixed

1. Native MFPlay child playback was outside the Qt scene. Saved-file playback now uses Qt Multimedia `VideoOutput` in the same scene as glass. Actual video pixels, not a substituted poster, are present in the player screenshots. Observed first readiness samples had **23 GPU/RHI texture frames and 0 CPU-frame handles** in Release (Debug: 20/0). This establishes the observed GPU-backed presentation path, not universal hardware decoding or zero-copy behavior on every PC.
2. A hidden blur layer did not produce the expected material texture. Applying the shader as the visible blur item's layer effect fixed the black plate.
3. `mapToItem()` did not establish a dependency on later ancestor/toolbar position changes. The coalesced update now recomputes the source rectangle; verification recorded the actual toolbar region **(32, 587), 1056 by 115** after video aspect fitting.
4. Video planes can be uploaded after an offscreen pass is prepared. Frame/presentation notifications plus one bounded settling sample prevent stale glass after paused seeks. Sampling uses an 80 ms coalescing interval, stops when unchanged, and performs no idle update loop. It deliberately does not refresh tint at every video frame.

With paused content unchanged, toggling only refraction altered **1,141 of 121,440 toolbar pixels** in the final Release capture, concentrated around the rim. This verifies a rendered effect; the screenshots establish its restrained appearance. The white-frame check verifies both decoded white video and a newly brightened glass interior, rather than accepting a black stale panel.

## Functional verification

All profiles, outputs, logs and failed artifacts are beneath PandaClip's `build` directory. No mouse movement, global input, input-desktop switch, foreground activation, speaker audio or control of personal sessions was used. The launcher clears SDK/plugin search paths for both shipping smoke and actual Qt playback checks. Actual Qt objects are exercised, not the hidden Win32 frontend.

**Before styling:** the full Release baseline passed at `build/quiet-tests/run-20261006-172737-Release`. It covered docks, Settings, gallery/player controls and synthetic recording/replay workers plus the component suite. The added microphone-open, custom registration/conflict and GPU-pixel/material assertions were not present in that baseline.

**After styling:** full suites passed:

- Release: `build/quiet-tests/run-20261006-183631-Release`.
- Debug: `build/quiet-tests/run-20261006-183739-Debug`.

Additional Release UI checks passed at `build/quiet-tests/purple-final-cost` (including cross-process restore) and `build/quiet-tests/purple-scale150-final` (**150% process-local scale, effective DPI 144**). The latter includes all category scroll, dock, media/material and recording/replay/export assertions; it does not change Windows display settings or validate a physical monitor transition. Published after screenshots come from `purple-final-cost`; the baseline images are unchanged copies of the before run.

The actual scaled [workspace](purple-screenshots/workspace-150.png), [Settings](purple-screenshots/settings-150.png) and [player](purple-screenshots/playback-150.png) were also captured and inspected for clipping/readability.

Both had zero failed frontend assertions, successful cross-process restoration, SDK-free shipping workspace startup/clean shutdown and independent decoding of every fixture/worker output stream. Final builds use MSVC/C++20/CMake; their logs contain no compiler warnings/errors.

Checks performed:

- Select the owned source in the Qt dropdown; microphone Default/named list and named selection; explicit whole-system mode selection/restoration; independent gain/mute controls.
- **Real quiet Check Mic:** opened the selected **Microphone (Realtek(R) Audio)**, reported Windows input volume 72%, stopped without an encoder, retained its selection. The instantaneous raw level was zero with previous signal activity reported. Nobody was asked to speak; this is endpoint/open/status evidence, **not audible speech or a successful mic recording/listening test**.
- Production custom Ctrl+Alt+Shift+F23 registration and duplicate conflict on a positively verified inactive desktop, unregister/restore afterward. No key was injected. Physical hotkey delivery/default input-desktop conflicts remain manual.
- Real docks float/move/hide/restore/lock/reset, layout persistence in its separate file, second-process restore.
- Every Settings category at 710 by 490 and 900 by 700; wheel over shortcut/dropdown/numeric controls without value changes, scrollbar position API, Home/End, reachable ranges, no settings-file writes from scrolling; settings and reduced appearance retained across launches.
- Eight fixture clips, filters/search, new save discovery, actual filter popover, solid/motion options, decoded GPU video, pause, seek with presented-frame timestamps, volume/mute, three track labels and track change preserving position, fullscreen/Escape, diagnostics export.
- Real synthetic-source ordinary/replay/shared workers while Media/Settings are open; Qt **Stop Recording**, **Disable Replay** and **Save Clip** buttons; hidden preview leaves encoding active; export leaves replay active; closing Media leaves replay active; drain/finalization succeeds. Worker starts use the test GPU ingress because inactive-desktop WGC cannot supply the source. Normal WGC start acquisition remains manual.
- Production NVENC/AAC fixtures for video-only, Game, Mic and Both: H.264 1280 by 720 at 30 FPS, AAC 48 kHz stereo, 0/2/2/3 audio streams. Independently decoded all streams in ordinary/replay fixtures and four UI-worker MP4s. Fixtures are 6 seconds ordinary / 4 seconds replay at the available IDR. Generated cue offsets around 1.00–1.67 ms validate PCM/codec/mux/export only.
- Bounded replay/snapshot stress, simulated Allowed/Denied border policy, logging queue overflow/owned-folder write failure, Unicode/damaged settings, library service checks, AAC sync-adjustment probe, and 12 timestamp-comparator regressions.

The border test does not request real Windows consent. The generated Game/Mic combinations do not capture real game audio or speech. Actual pointer dragging and keyboard delivery are not replaced by a second physical cursor.

## Measured costs

Same machine, private inactive desktop, effective DPI 96; sequential final Release and Debug suites. No game performance claim is made.

### Before/after Settings handlers

Each mode delivers the same 100 wheel events directly to Qt. Values are median / p95 milliseconds. This measures the synchronous handler only, excluding input delivery, painting and compositor latency.

| Mode | Before | After |
| --- | ---: | ---: |
| Idle | 0.0037 / 0.0044 | 0.0040 / 0.0169 |
| Recording | 0.0042 / 0.0645 | 0.0037 / 0.0088 |
| Replay | 0.0040 / 0.0043 | 0.0041 / 0.0145 |
| Both | 0.0060 / 0.0162 | 0.0039 / 0.0287 |

These short samples do not establish a speedup or worst-case latency. Final Release cached controller/UI polling measured **1.361 ms mean / 6.872 ms maximum** over 169 ticks. Debug measured 3.831 / 26.916 ms over 174. The before suite had fewer cases; poll averages are not a paired workload comparison.

### Glass enabled/disabled

The same 1120 by 820 Media window loops the same four-second encoded clip, quietly, with a 16 ms render-request timer and a warm-up. Each mode is sampled for at least four seconds. Glass itself coalesces updates at 80 ms. An idle assertion confirms unchanged material is not repeatedly sampled.

| Release metric | Glass on | Glass off |
| --- | ---: | ---: |
| Wheel handler median / p95, ms | 0.0035 / 0.0041 | 0.0039 / 0.0313 |
| CPU record/submit median / p95, ms | 0.1204 / 0.4607 | 0.1150 / 0.4514 |
| GPU draw interval median / p95, ms | 0.1311 / 0.4963 | 0.1482 / 0.6102 |
| CPU / GPU samples | 447 / 447 | 366 / 365 |

An additional paired Release run logged sums so the extra renders are included:

| Same four-second sampling protocol | Glass on | Glass off |
| --- | ---: | ---: |
| CPU / GPU samples | 439 / 438 | 361 / 360 |
| CPU scope sum, ms | 89.1771 | 74.6179 |
| GPU draw interval sum, ms | 82.0376 | 67.5662 |
| GPU median / p95, ms | 0.1177 / 0.5204 | 0.1119 / 0.5862 |

The measured GPU sum increased by **14.4714 ms over the approximately four-second window**. This is about 3.6 ms of additional GPU draw interval per second in this fixture, not added latency to each game frame or a pinned-clock benchmark. One boundary query was still pending at reporting in each mode. The additional run's UI poll mean/max was 1.354/6.026 ms; wheel median/p95 with glass on/off was 0.0036/0.0084 versus 0.0034/0.0036 ms. Raw reports retain sample counts and maxima.

GPU timing uses eight reusable D3D11 timestamp/disjoint query slots, nonblocking reads, and Qt's supported external-command boundary to submit the deferred RHI commands before ending the query. It includes queued offscreen material draws and main-scene draws, excluding Present/vsync wait. CPU timing brackets beforeRendering through command submission; it excludes earlier scene synchronization/layer preparation, decoding and other workspace work. Neither is whole-app frame time. No queries were skipped or disjoint in this pass.

Glass schedules additional scene renders, so per-frame costs alone do not represent total work. Clock/power state was not pinned; short distributions overlap. **The lower glass-on GPU median is not evidence that glass makes rendering faster.** This is a local synthetic playback observation, not GD overhead or input-to-display latency. Early probes around unsubmitted command recording, and a probe including Present wait, were rejected as draw-cost evidence; their artifacts remain under `build/purple-refinement`.

## Dependencies and limitations

Qt Multimedia and its FFmpeg 7.1 DLLs/plugins are deployed for **saved-file playback only**. PandaClip recording still owns capture, encoder control and MP4 writing. Qt remains dynamically linked LGPLv3; observed FFmpeg configuration is LGPL2.1+ without GPL/nonfree features. Matching unmodified source archives, hashes, license texts, build configuration and replacement instructions are deployed. Qt Multimedia installation and the existing-install helper check passed; sources/checksums were reverified. A complete cold install on another PC was not repeated. The optional MSIX staging includes multimedia plugins, but no newly signed/installed package or borderless permission was verified.

Qt emits its fullscreen `requestActivate` warning on a window marked `WindowDoesNotAcceptFocus`; the owned window remains on the inactive desktop. Final runs had no QML loading/material errors. Failed intermediate reports are retained: stale fixed-delay seek/material captures were not treated as valid visual evidence, and the verifier now checks decoded timestamps and settled pixels.

### Manual checklist

1. Launch the Release command above; inspect actual hover/focus, text contrast, bright/dark playback, popovers, solid/motion modes; drag/float/redock with your pointer and move between actual monitors/DPI settings.
2. Select GD and application-loopback explicitly, start normal WGC capture/recording/replay and inspect moving preview, aspect fit, real border consent/status and outline.
3. Select the intended microphone, Check Mic, speak, verify opened name/raw meter and Windows/PandaClip mute/gain. Record and **listen independently to Mix, Game and Mic**. Physically unplug/reconnect the selected mic; it must not silently switch endpoints.
4. Press your Save Clip keybind on the input desktop; inspect real conflicts/tray behavior. Open Media while recording/replay and listen/seek/switch tracks using real clips.
5. Benchmark the same moving GD segment with capture off versus replay on, with preview shown/hidden. Measure game frame-time distributions and inspect decoded clip smoothness; no synthetic/idle measurement here proves gameplay cadence or overhead.

Build/test logs and original artifacts: `build/purple-refinement`, the baseline run and the final Release/Debug runs above. Successfully saved files and prior screenshots were preserved.

The final shipping SDK-free startup/close check passed at `build/quiet-tests/purple-final-shipping`; actual Qt media/material checks used the same deployed runtime folder. Release executable SHA-256: **`8EDC26F5A456D7136AD24A4759E051CFC4A634870238A1AB2B6D8BC54354C427`**. Neither a clean second PC nor a newly installed MSIX was tested.
