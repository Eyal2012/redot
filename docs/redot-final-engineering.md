# ReDot final engineering stage — 2026-10-10

## Delivery and acceptance

Release candidate: `build/redot-final/Release/ReDot.exe`. Debug: `build/redot-final/Debug/ReDot.exe`. This stage changes resource reuse and lifecycle/accounting correctness. No features, UI redesign, recording-quality reduction, new recorder, audio filtering, or storage migration were added.

The Stage 4 fallback remains `build/redot-stage4-accessibility/Release/ReDot.exe`, SHA-256 `4a294fbf7cd9b8d438812afeba5aa782d890af01e69ced3dfda07682ae70fdbd`. Its running process was left untouched. Before editing, the original source/tools/docs were checkpointed in `build/checkpoints/redot-final-20261010/source.zip` with SHA-256 manifest. The baseline was compiled before production edits in `build/redot-final-baseline`; do not rebuild that folder to reproduce the old binary, because its CMake source points to the current workspace.

Final Release SHA-256: `61bf5a96caebb54fac02242216ba1141fd68b0aabecdb141315422230f2e8726`.

Existing Stage 3/4 physical tray, gameplay/audio and multi-monitor appearance approvals remain user-reported acceptance. This report adds engineering evidence; it does not replace those checks with synthetic performance claims. New gameplay frame-time comparisons, hardware interruption and physical listening are pending. Multi-hour end-to-end operation is not established by the shorter real-time runs or accelerated packet test.

## Architecture review and focused corrections

Reviewed WGC QPC mapping/copy leases, D3D11 scene source discovery/composition/pools, NVENC synchronous completion and resize resources, WASAPI converter/integer timelines/scene mixer, compressed packet retention and MP4 exports, thumbnail/playback workers, Qt polling/popovers, persistence, tray and shutdown. Canonical composition still feeds both native preview and one recording/replay encoder. NVENC input is not reused before completion; retained source leases and their views move together. Output CFR remains independent of preview: late composition slots are skipped, and the encoder repeats the latest owned frame when no new image is available.

### Encoder shader-view reuse

`NvencMp4Writer::drawResizedFrame` previously called `CreateShaderResourceView` on every encoded frame, although the compositor already had a bounded reusable output texture pool. `Scenes::Runtime` now creates an SRV with each output buffer and hands it to the encoder with the exact texture lease. `RecordedFrame` gains an optional view; sources without one retain the compatible creation path. Removed duplicate input-layout and blend-state setters. The integration test checks the SRV's underlying resource and every canonical encoded frame's reuse counter. The ten-minute run reused **35,998** views and created **zero** on the encoder path. This eliminates measured per-frame resource creation, without changing pixels/bitrate/preset or claiming a general gameplay FPS gain.

### Compositor deadline precision

Additional supported-rate testing exposed coarse deadline waits at 120 FPS. A 30-second 1080p120 run composed 2,250 frames (~75 FPS), missed 1,344 slots and encoded 1,460 repeated frames, despite mean GPU intervals only 0.606 ms. The CFR encoded rate alone hid this scheduling problem.

`Scenes::Runtime::Impl::run` now uses an unnamed per-worker high-resolution waitable timer, plus a cancellation event for prompt Stop. It does not call timeBeginPeriod or change a global Windows preference. Missing timer/cancellation support falls back to the existing condition-variable path, logged honestly. [Microsoft documents this timer flag](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw) for short deadlines on Windows 10 1803+. Resources are RAII handles; Stop signals the event before worker teardown. Existing scheduled monotonic timestamps and missed-slot policy remain.

One identical verification binary with the old wait forced by a **verification-only** flag produced 2,223 compositions/30 s (74.1 FPS), 1,374 late slots and 1,535 repeated encoded frames. Default precise wait produced 3,599 compositions (119.97 FPS), three late slots and three repeated frames. Encoder mean remained ~2.17 ms; whole-machine CPU increased ~0.232→0.505% because it now actually composes at the selected rate. No quality or configured frame rate was reduced. This is a measured timer-scheduling correction, not proof of 120 distinct gameplay frames: the generated source and real WGC availability are separate.

Evidence: `deadline-120-coarse-*` and `deadline-120-precise-*`. A new component regression checks at least 90% of 120 FPS **composition** over three seconds (instead of relying on encoder CFR repeats), and worker cancellation within 100 ms. Further 60 FPS comparisons/long-session evidence below use the final timer implementation.

### Replay accounting and allocation failure

Packet accounting formerly counted `vector.size()` only. The reproducer moves 24 packet vectors with 8 MiB capacity but 1 byte size into a 128 MiB cap: the old implementation retained **192 MiB** of payload allocation while reporting **24 bytes**. The candidate charges capacity plus conservative packet/shared/deque overhead, codec configuration, track labels and snapshot reference arrays. Shared payloads are deliberately conservatively charged again while retained by an export. The same cap now evicts to at most 15 such packets. Failed over-cap snapshots release their references. Packet insertion commits the byte count only after allocation and deque insertion succeed; an injected `bad_alloc` leaves counts/history unchanged.

This is a conservative replay allocation estimate, not a cap on the entire process, GPU textures, driver allocations or Media Foundation caches. Transient incoming packet/muxer allocations are outside the replay counters. Existing 5–600 second custom durations, presets, memory settings, two-save bound, IDR selection, shared ownership and common AAC/video offset remain intact.

### Completion ordering and shutdown

The old worker cleared busy and posted Finished before MF shutdown, optional PCM diagnostic-writer drain and COM cleanup. The UI joins that worker on Finished. A verification-only 1.5-second cleanup delay reproduced an early **0 ms** Finished, **1,250 ms** blocked next Start, and **1,531 ms** post-Finished join. The candidate's last-destroyed completion guard publishes only after worker cleanup: Finished follows **1,500 ms** cleanup, another Start rejects in **0 ms**, and post-Finished join takes **0 ms** in the initial targeted check. Repeat sessions and failed encoder startup pass. The delay seam is absent from shipping builds.

Scene AAC MFTs are also released before MF/COM teardown. Recording/replay notifications with a null HWND now release events instead of calling `PostMessage(nullptr)`, which posts to an unpumped worker thread queue. That nullable path occurs in headless component callers; shipping supplies its controller HWND. Audio continuity/resampling/mixing code is unchanged.

## Baseline methodology and results

Hardware: Intel Core Ultra 9 285K, 24 logical processors; RTX 5070 Ti, NVIDIA driver 32.0.16.1088; approximately 31.37 GiB OS-visible RAM; Windows 11 Pro 10.0.26200; Qt 6.8.3/MSVC x64. Existing personal applications remained running, so ambient desktop load was not controlled.

Primary workload: 1920×1080, 60 FPS, Balanced P4, configured 20/35 Mbps, generated moving D3D11 texture, bounded GPU pools, no raw-frame replay. The simple fixture compresses much more cheaply than detailed gameplay. Each short run lasts 30 seconds, CPU measurement excludes the first five seconds. Short replay history is 15 seconds; long tests use 60 seconds. Fixture lanes are compiled only into verification targets. No microphone, speaker output, personal WGC target, desktop input or sound is used.

`tools/measure_final_performance.ps1` launches one positively identified owned process per mode. Resource CSV records private/working-set memory, handles, CPU time, I/O transfers, frame and buffer counts; Windows GPU counters record individual owned PID engine activity and dedicated/shared memory at 1 Hz. GPU timestamp intervals include preemption/scheduling and are not equivalent to utilization. Encoder wall time includes GPU resize, NVENC completion and packet/mux work. CPU below uses all 24 logical processors (one-core percentages are available in raw metrics). Disk transfer counters include all process I/O; finalized file sizes are separately reported. No game or WGC latency is inferred from generated-source counters.

Original clean baseline: `build/final-performance/baseline-clean-20261010-161725`. Earlier `baseline-...160959` overlapped a build and is retained as exploratory/confounded evidence.

| Mode | CPU (% whole machine) | Compose CPU / GPU interval (ms) | Encoder mean (ms) | Private end (MiB) | Composed / encoded FPS | Repeated / queue drops |
|---|---:|---:|---:|---:|---:|---:|
| idle | 0.003% | 0.000 / 0.000 | 0.000 | 157.4 | 0.000 / 0.000 | 0 / 0 |
| capture | 0.070% | 0.031 / 0.012 | 0.000 | 176.2 | 60.000 / 0.000 | 0 / 0 |
| replay | 0.151% | 1.425 / 4.028 | 2.236 | 296.1 | 59.933 / 59.933 | 0 / 0 |
| record | 0.198% | 1.290 / 4.757 | 2.163 | 298.7 | 59.933 / 59.933 | 0 / 0 |
| both | 0.331% | 1.332 / 6.010 | 2.158 | 299.6 | 59.933 / 59.933 | 0 / 0 |
| layers | 0.167% | 1.676 / 5.898 | 2.193 | 304.5 | 59.933 / 59.933 | 0 / 0 |
| switch | 0.299% | 1.708 / 4.486 | 2.197 | 303.9 | 59.933 / 59.933 | 0 / 0 |
| saves | 0.313% | 1.283 / 5.501 | 2.298 | 299.1 | 59.933 / 60.000 | 2 / 0 |

Fixture `idle` is a GPU/runtime fixture, not idle shipping UI. `capture` is generated ingress/composition, not live WGC. `layers` adds alpha-image/text sources; `switch` changes between two scenes every five seconds; `saves` exports every five seconds while recording and replay continue. Baseline repeated-save run completed five clips, zero failures, maximum 287 ms; actual ordinary-plus-replay MP4 bytes 1.88 MiB, reflecting its simple silent content.

### Equivalent repeated comparisons

Two alternating baseline/final runs per mode, 30 seconds each, same settings: folders `paired-baseline-1/2-*` and `paired-final-1/2-*`. Means of separate runs:

| Mode | Whole-machine CPU baseline → candidate | Encoder mean baseline → candidate |
|---|---:|---:|
| replay | 0.225% → 0.146% | 2.128 → 2.058 ms |
| both | 0.211% → 0.206% | 2.129 → 2.097 ms |
| layers | 0.180% → 0.246% | 2.169 → 2.132 ms |

Replay encoder means improved 2.128→2.058 ms in these two runs (~3.3%); both and layers improved ~1.5% and ~1.7%. CPU varied substantially; layers CPU increased. Per-run GPU intervals also vary. These short runs are evidence of eliminated SRV creation and small encoder-time differences, **not statistically established overall CPU/GPU or gameplay gains**. Candidate replay had one repeat in one run; all pairs had zero video-queue drops. Do not pool frames across independent runs or claim zero impact.

`candidate-clean-*` is the intermediate eight-mode resource-reuse candidate before later cleanup corrections; `paired-final-*` uses the resource/accounting/cleanup changes before the later deadline correction. The deadline-controlled comparison and final delivery long run below cover that correction. Raw method.json includes each benchmark executable hash. Full results and exact per-engine counters are `build/final-performance/summary.json`. All finalized MP4 inventories are reconciled after the workers stop; an incremental decode enumeration alone is not used as proof that later files were tested.

The exact shipping baseline and candidate were each sampled for 30 seconds on an inactive owned desktop, with the same fresh no-audio/no-target profile, no SDK PATH fallback. Native WM_NULL pings all responded (500 ms limit). Mean/max ping baseline 0.238/2.443 ms, candidate 0.122/0.357 ms; end private memory 178.36/178.16 MiB. CPU 1.63/2.15% of one core (~0.068/0.090% whole machine). These runs overlapped independent decoding, so CPU differences are confounded; no idle optimization gain is claimed. CSVs: `build/final-performance/shipping-idle`. A later repeat on the final scheduler binary (same 720p30 idle profile) gave baseline/candidate mean/max pings 0.336/0.936 versus 0.218/1.210 ms, end private 176.19/176.95 MiB, CPU 4.04/2.93% of one core. It overlaps passive inspection/decoding and is also not a clean CPU comparison; all pings succeeded. Both idle profiles intentionally had no real capture source. Primary GPU/recording comparisons use 1080p60.

Functional preview-visible/hidden and tray-hidden encode checks run in the owned Qt suite. A controlled production preview-visible versus hidden **performance** comparison with real WGC remains unmeasured. No live GD frame delivery latency or overall physical desktop GPU baseline was collected.

## Replay, long-session and audio evidence

### Ten-minute pre-deadline GPU/NVENC/AAC/export run

`build/final-performance/long-final-20261010-163845/saves`: 600 seconds, 1080p60, generated desktop/microphone PCM lanes through the existing scene timeline/mixer and AAC encoders; Mix plus two named stems; visual/PCM cues every ten seconds; recording and replay active; clip request every five seconds. No sound device opened.

- 36,000 source fixture frames; 35,998 composed/encoded frames; 59.997 encoded FPS. Zero encoder repeats, video queue drops, producer/pool drops and compositor late slots. Final partial scheduling slot is not a missing encoded packet.
- Mean encoder wall time 2.106 ms, max 5.14 ms; compose CPU mean 0.716 ms, GPU interval mean 4.089 ms. Queue high water 2/8, start call 0.027 ms, first encode 84.7 ms, stop/drain/finalize 25.7 ms.
- 119 replay clips and one ordinary MP4, **all 120 independently decoded successfully**, every H.264 stream and all three AAC tracks. 48 kHz stereo AAC with monotonic contiguous decoded timestamps. Finalized files total 697,835,308 bytes (~665.5 MiB); no failed export or partial final MP4. Maximum export time 676 ms. Final available history 59.95 seconds; warm-up clips ranged from 4.85 s and later clips up to 59.867 s. Two-second GOP and AAC boundaries can shorten a requested 60 s save.
- Replay charged memory roughly 8.2–8.3 MiB after warm-up, well below the unchanged 512 MiB default. 1 Hz pending-save samples were zero because sub-second exports completed between samples; this does not mean no snapshot memory was retained. Stress tests exercise the actual pending/save limits.
- Owned GPU counters: 3D mean 1.155%; two reported video-encode engines approximately 4.786% and 4.803% individually. Do not sum them. Mean dedicated/shared process GPU allocations about 137.0/19.3 MiB. GPU counter attribution is process-level, not a measurement of the user's game's GPU impact.
- Process private memory 349.7–361.2 MiB after 60 seconds; fitted slope ~17.5 KiB/s. Threads 93–108, handles 656–949 including startup/exports; handles do not increase monotonically. **This does not prove leak-free operation.** MP4 index retention/cache warm-up can contribute, but that attribution is not proven by these samples.

### Replay-only export load and memory follow-up

`replay-stability-20261010-173136/replay-saves`: five real minutes with both generated audio lanes, no ordinary recording, 59 clips, zero export failures/encoder queue drops; all 59 decodes checked separately. This run overlapped independent decoding/build/UI verification, so it is a load/reliability check, not a clean comparative benchmark. Compositor skipped 113 late slots and the CFR encoder repeated 114 frames (0.63%) while preserving its clock. No pool/producer drops. Audio timeline missing/overwrite/clipping counts zero. Stop 22.6 ms, max export 789 ms. Replay available 59.95 s.

Private memory after one minute ranged 348.4–352.5 MiB, fitted slope ~4.1 KiB/s; threads 90–105, handles 657–900. Lower growth than recording-plus-replay suggests ordinary mux indexing/exports/caches contribute, but is insufficient to label all growth harmless. The additional replay hold test stops exports after three minutes while keeping encoding/audio alive to distinguish ongoing growth from export/cache retention:

`replay-hold-20261010-173740/replay-saves`: ten minutes with 35 exports in the first three minutes, then seven minutes of continued GPU/audio/replay processing without exports. All exports independently decoded; result evidence below. During seconds 240–599 private memory stayed within **369,008,640–369,688,576 bytes** (352 MiB, a 0.65 MiB band); handles 861–869. Whole post-minute private-memory slope ~275 bytes/s. This supports bounded cache retention in this replay-only fixture after exports stop, not a certification of all process caches. 36,000 encoded frames, 153 repeated frames under concurrent test load, 151 late composition slots, zero encoder queue/pool drops, 35 exports with zero failures, maximum 748 ms. 182 early missing/late audio frames (~3.8 ms) were already present at the first five-second diagnostic and did not increase; overwrite/clipping/queue-drop counts zero. This startup observation is preserved rather than reported as perfect zero-loss audio. Ordinary long-recording indexing remains a separate resource cost; multi-hour/real-game growth still needs verification.

### Accelerated packet stress

Two logical hours, 1,080,000 compressed-like dummy packet pushes and 720 retained snapshots; maximum charged bytes 59,315,872 under 128 MiB, history/snapshots release to zero, no outstanding save reservations. This takes less than a second and validates accounting/eviction/dependencies, **not two hours of wall-time NVENC/WGC/audio capture**. Reproducer and new allocation-failure check run in both build suites. Shared ownership and at most two exports remain enforced.

### Audio and synchronization

Long run has 60 timed cues; initial offset +10 ms, subsequent usual offsets −6.0…−5.33 ms, with one −22.33 ms observation near 110 seconds due a one-frame later visual cue. The final ten cues remain −6.0…−5.33 ms; there is no progressive trend demonstrated. Startup cue is retained/trimmed differently, and 3,696 early audio frames (~77 ms) are reported late at the initial epoch. No further late-audio growth, missing timeline samples, overwrites, queue drops or Mix clipping were reported. Do not call this startup behavior a measured wireless microphone offset.

Ordinary/replay common-offset comparison matched **619 complete interior cues**, maximum audio timestamp difference **0.33 ms**; truncated first/last cues are excluded from equivalence matching, not hidden. Full results: `saves/sync-summary.json`, independent decode reports and export-offset log. This verifies export rebasing preserves this session's timing. Synthetic cue scheduling and codec detection cannot establish physical WGC/WASAPI/perceptual synchronization or replace listening.

The quiet PCM continuity suite covers continuous fractional SRC, packet ordering/count/stride/channel masks, silent advancement, timestamp jitter/integer positioning, gains/clipping, four lossless checkpoints, bounded writer failure and a generated ten-minute duration. Scene audio fixtures check shared endpoint deduplication, Mix/stems, live gain/mute, scene switches, conflicting shared-source adjustments and endpoint pinning. No audio capture/resampler/timeline/scene mixer changes were made in this stage. Earlier user-approved GD/HyperX listening remains historical, not a new long-session listening test.

### Final scheduler delivery run and additional resolutions

Final delivery evidence: `build/final-performance/delivery-long-20261010-180404/saves`, 600 real seconds at 1080p60 with the final private deadline timer, generated moving GPU content and two generated PCM lanes, recording plus replay, saves every five seconds. Independent decoding was reconciled against the complete file inventory after recording stopped: **120/120 Passed** (119 replay clips and one ordinary recording). The earlier incremental watcher saw only 89 files; its first-pass summary is preserved and was followed by decoding every remaining file, rather than treating that incomplete enumeration as full coverage.

| Measurement | Final delivery run |
|---|---:|
| Source fixture / composed / encoded frames | 36,000 / 35,994 / 35,998 |
| Composed / encoded rate | 59.990 / 59.997 FPS |
| Late composition slots / repeated encoder frames | 4 / 4 |
| Video queue / pool / producer drops | 0 / 0 / 0 |
| Encoder mean / maximum wall time | 2.102 / 4.45 ms |
| Composition CPU mean / GPU interval mean | 0.124 / 0.141 ms |
| Start call / first encode / Stop-finalize | 0.063 / 73.32 / 31.01 ms |
| Completed exports / failures / maximum export | 119 / 0 / 736 ms |
| Encoder SRVs created / reused | 0 / 35,998 |
| Final charged replay bytes / available history | 8,632,634 / 59.95 s |
| Process CPU, whole machine | 0.663% |
| Mean dedicated / shared GPU allocation | 138.5 / 19.4 MiB |
| Threads / handles observed | 93–103 / 661–940 |

Owned engine counters averaged 1.108% for 3D and approximately 4.742% and 4.748% for the two reported encode engines individually; do not sum these counters or infer gameplay impact. GPU intervals include scheduling/preemption (maximum 17.77 ms). Output files total 685,496,809 bytes (~653.7 MiB). Queue high water was 2/8. Early warm-up clips contain 4.85 seconds; mature clips vary around 58.85–59.88 seconds because of IDR/AAC boundaries. Capture continued through every export.

Private memory after 60 seconds ranged 364,761,088–376,393,728 bytes (347.9–359.0 MiB), fitted slope **17.2 KiB/s**. This is continuing growth in this recording/export workload, not a demonstrated leak-free plateau. The replay-only hold test above provides a separate plateau result; ordinary recording/mux-index retention and export caches need longer profiling before multi-hour resource stability can be certified. No speculative production memory rewrite was made from this trend alone.

All files have one H.264 stream plus Mix, Fixture Desktop and Fixture Microphone AAC tracks. The ordinary video independently decoded 35,998 frames over 599.967 seconds at 1920×1080, exact 60/1 timestamps; each AAC track decoded 28,797,952 PCM frames over 599.957 seconds at 48 kHz stereo, with monotonic contiguous timestamps. No audio queue loss, timeline missing/overwrite frames or Mix clipping were reported. **3,848 late audio frames (~80.2 ms)** were present at the first five-second diagnostic and unchanged at shutdown, representing this generated startup epoch rather than measured microphone latency.

The final session contains 60 synthetic flash/PCM cues: initial offset +10 ms, later observations between −24.0 and −6.67 ms, final offset −6.67 ms. Intermediate visual detections occupy either of two adjacent video slots while audio cue timing remains near its ten-second schedule; endpoint subtraction alone is insufficient to diagnose accumulating drift. No blind offset correction was applied. Across the 119 exports, **619 complete interior cues** match the ordinary recording after the logged common offset, with maximum audio timestamp difference **0.33 ms**, zero comparator failures. This establishes this fixture's export timing consistency, not clean audible hardware capture or perceptual game synchronization. Raw evidence: `decode/summary.txt`, `sync-summary.json`, `metrics.txt`, `resources.csv`, `gpu-counters.csv` and local logs.

Auxiliary 30-second output checks before the deadline correction: 720p30 produced 900 encoded frames (one repeat, encoder mean 1.340 ms); 1440p60 produced 1,798 frames (50 repeats, 10 late composition slots, encoder mean 3.416 ms). 1080p120 exposed the scheduling defect described above; passing decode alone was insufficient. All three ordinary MP4s independently decode with exact CFR timestamps/resolution. No audio hardware or gameplay was involved. Final primary/deadline performance remains subject to source availability and ambient workloads.

## Verification and fault matrix

Final production Debug/Release builds succeeded with C++20/MSVC/CMake and deployed dynamic Qt runtime. Final-timer suites passed sequentially. Each full suite independently decodes 37 MP4s, checks the 99-assertion GPU/scene integration (including deadline precision and cancellation), scene/editor tests at 100/125/150% Qt scale, exact shipping controls/Settings at those scales, lifecycle/buffer/audio/logging/service tests, playback/sync comparator negative cases and three frame-time arithmetic tests. Prior full Debug/Release runs also passed; failed intermediate attempts remain described below.

- Debug: **Passed**, `build/quiet-tests/run-20261010-180520-Debug`; 37 independent MP4 decode passes; 120 FPS deadline check 361 compositions in three seconds, zero late slots; timer Stop under 100 ms.
- Release: **Passed**, `build/quiet-tests/run-20261010-180911-Release`; 37 independent MP4 decode passes; deadline check 362 compositions in three seconds, zero late slots; timer Stop under 100 ms.
- Exact final Release passive controls/appearance: **Passed**, `build/final-deployed-delivery`, no foreground violation. Before/after synthetic deactivation Capture PNGs are byte-identical. This is not a new physical two-monitor click test.
- Controlled packet/encoder/write/recovery/exit cases below: **Passed** in both full suites. Physical hardware/listening scenarios remain **Not Verified** in this stage.

Tests are quiet, under fresh build profiles and owned files. Qt checks run on inactive owned desktops without switching the user's desktop. Physical-desktop screenshots use nonactivating/input-transparent owned windows; no recording or personal capture is started there. Run UI suites sequentially. A concurrent attempt failed eight initial UIA Invoke lookups, and a later Debug attempt failed the Settings lookup before Qt provider readiness. The native root can appear before its named controls are exposed. The harness now reacquires/waits for the actual named Invoke control for at most five seconds, invokes once, and never forces activation. Failed logs remain preserved. The final sequential runs are the reported acceptance runs.

The initial Release suite failed one fixed-650-ms material assertion under concurrent GPU decoder load. An isolated recheck passed unchanged frontend. The test now waits for the naturally updated observed Media material/motion state up to three seconds, still failing if it does not arrive; the passing Release observed 1,014 ms. Production appearance logic was not changed. Another run checked hidden-frame advancement immediately after a very fast export completed, sometimes before one 30 FPS frame period. The test now observes the hidden window for at least 250 ms before checking actual encoded-frame advancement; no worker or tray behavior changed. The failed artifacts are retained.

| Scenario | Result / evidence type |
|---|---|
| Scene editor transformations, layer order, picker/restore, migration/persistence | Quiet Qt/model checks at 100/125/150% Qt scale |
| Exact composition, resize/aspect/stretch, scene switches in recording/replay | D3D11/decoded-frame integration; generated sources |
| Game-only, mic-only, both, video-only and up to 16 scene stems | Generated NVENC/AAC MP4 layouts and PCM tests |
| Start/Stop, cleanup, repeat sessions, unavailable encoder | Targeted real NVENC + controlled invalid-adapter test |
| Replay presets/custom history, RAM pressure, snapshot retention/save bound | Packet stress + decoded asynchronous clips |
| Recording packet-write failure and retry | Controlled worker injection; visible error, no false Saved, prior files preserved |
| Replay packet-write failure and retry | Controlled worker injection; healthy replay continues, finalized retry only |
| Missing output folder / rejected initialization | Owned profile rejection; neutral taskbar state |
| Recovery exhaustion and explicit restart | Controller injection; exactly three attempts, no endless retry; preserved scene records again |
| App exit while accepted export/recording active | Qt/tray Exit action triggers drain; expected final files, no partial MP4 |
| Minimize/close preferences, normal/maximized restore, hidden recording/replay/Save | Real Qt controls and owned HWND state/encoding; no physical Explorer tray clicking |
| Hotkey registration/conflict while hidden | Owned registration probe, not a physical global key press |
| Taskbar dot neutral/green/red priority, neutral tray object | Native icon pixels and actual engine transitions |
| Media size/date filtering/search, missing file, playback/seek/tracks | Real Qt/QML plus independent generated-media checks; silent playback |
| Acrylic/popup/navigation/caption/size/scaling | Deployed passive screenshots and isolated Qt/native checks; no new physical two-monitor click/drag test |
| Logging saturation/unwritable folder | Four producers, bounded queue, dropped-count reporting, worker failure |
| Real game close/reopen, monitor/DPI change, actual mic/webcam unplug | Not performed in final stage; fixtures do not establish hardware recovery |
| Real GPU removal, sleep/wake or session lock | Not performed: would disrupt the active personal desktop/driver |
| Full physical disk / power loss during MP4 finalization | Not performed; controlled writes are not power-loss or disk-full certification |
| Multi-hour shipping/gameplay operation / physical listening | Pending |

Physical Stage 4 tray/gameplay and focus appearance were previously approved by the user. No new physical validation is invented. The finite NVENC/API waits inside a driver or filesystem can still stall; controlled seams do not certify arbitrary driver/kernel failures. Abrupt process/power termination cannot promise a finalized MP4.

### Actual deployed appearance evidence

Capture and View Media were inspected as compositor screenshots over owned backgrounds, before/after synthetic focus notifications, five gears, Scenes and small/maximized windows. Sharp controls/video surfaces and the same production materials remain; `src/frontend_qt.cpp`, backdrop/chrome/QML, branding and .vscode hashes are unchanged. Accepted DWM state alone is not a physical active-focus claim. Some combined Settings screenshot attempts were refused because the foreground-owned-window coverage safety check failed; those are recorded as zero/skipped, not successful images.

![Capture — passive owned backdrop](../build/final-deployed-delivery/capture-blue.png)
![View Media — passive owned backdrop and generated clips](../build/final-deployed-delivery/media-blue.png)

## Reproduce builds/tests and launch

Installed dependencies are unchanged; no additional runtime setup. All paths below are within the workspace. Debug/Release CMake/MSVC builds passed. Quiet suites need the verification option:

```powershell
Set-Location 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip'
cmake -S . -B build/redot-final -G 'Visual Studio 17 2022' -A x64 -DPANDACLIP_BUILD_REPLAY_STRESS=ON
cmake --build build/redot-final --config Debug --parallel
cmake --build build/redot-final --config Release --parallel
& tests/run_quiet_tests.ps1 -BuildDirectory build/redot-final -Configuration Release -SkipBuild
& tests/run_quiet_tests.ps1 -BuildDirectory build/redot-final -Configuration Debug -SkipBuild
& tools/measure_final_performance.ps1 -BuildDirectory build/redot-final -Label review -Seconds 30
py -3 tools/summarize_final_performance.py
```

Run suites sequentially; no unrelated loads for comparative benchmarks. The benchmark does not run or capture GD, but does exercise GPU/NVENC and writes owned files, so schedule it outside personal gameplay. Long-run options include `-AudioFixture -HistorySeconds 60 -Seconds 600 -Modes saves` and `-Modes replay-saves -ExportUntilSeconds 180`. Watch disk space for long ordinary/repeated exports; do not fill the disk as a test. Raw data and failed artifacts are retained.

Finish current saves and close the old app normally yourself before opening the candidate with the same personal preferences. Exact tested Release launch:

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\build\redot-final\Release\ReDot.exe'
```

Rollback: launch the preserved Stage 4 executable at `build/redot-stage4-accessibility/Release/ReDot.exe` after closing the candidate normally. No profile migration is needed.

## Remaining physical Geometry Dash verification

1. Use the same level/section, output 1080p60 Balanced, scene, audio endpoints/gains, in-game FPS cap, VSync, resolution, monitor refresh rate and background-app workload. Warm up for 30 seconds. Use three separate 60-second runs per condition: ReDot closed, ReDot idle with the same selected scene, replay, recording, both. “Idle” can still have a live scene preview; keep that consistent. Repeat preview visible/hidden as a separate comparison.
2. Collect game presentation intervals with the official [PresentMon console](https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md), whose release binary is an optional measurement tool, not a recording dependency. Place it under `build/gd-benchmark` as `PresentMon.exe`. Use a unique ETW session; do not terminate another existing session. Example, manually select the condition and play the same section:

```powershell
& '.\build\gd-benchmark\PresentMon.exe' --process_name GeometryDash.exe --v1_metrics --output_file '.\build\gd-benchmark\replay-run1.csv' --session_name ReDotReviewReplay1 --delay 10 --timed 60 --terminate_after_timed --no_track_input
py -3 tests/summarize_game_frame_times.py 'build/gd-benchmark/*-run*.csv'
```

3. Summarizer reports each process/swapchain separately: average FPS, reciprocal of mean slowest 1% intervals (withheld below 1,000 samples), median/p95/p99 and per-condition repeated-run range/CV. Record tool version/settings. Presentation intervals are not WGC/encoded cadence. Compare frame-time tails and run-to-run variation, not only mean FPS. Also export ReDot Diagnostics for source cadence/gaps, repeats, queue pressure and encoder timing; inspect moving footage for duplicate/late frames. No new “minimal gameplay impact” number is claimed until this is done.
4. Record/replay game music/effects and HyperX speech; listen to Mix and each stem externally and in Media at beginning/end, including seek/pause. Compare a same-device reference recording if required. Check silence/mute/gains intentionally; nonzero PCM is not proof of clean speech. Check at least ten minutes near start/end for perceptual alignment.
5. In a disposable session after saving, manually check selected microphone unplug/reconnect, game close/reopen, webcam if used, sleep/wake and tray/hotkey controls. Sources must stay pinned; failures must not switch microphone or application-loopback mode. Do not deliberately remove the GPU or fill a disk.
6. Multi-hour soak remains pending. Start an isolated profile/output folder using the existing review runner, monitor Diagnostics/process/GPU memory and file sizes, save periodically, then stop normally and decode/listen to intermediate clips as well as the final recording. Run within available disk/RAM, with user supervision. Continuing private-memory growth requires further profiling; current data does not justify a leak-free claim.

## Files changed

Production: `src/recording.h`, `src/recording.cpp`, `src/scene_runtime.cpp`, `src/replay.h`, `src/replay.cpp`, `src/diagnostics.h`.

| File / function | Change |
|---|---|
| recording.h / RecordedFrame, RecordingIngress | Optional leased SRV and reuse counters |
| scene_runtime.cpp / Buffer, Runtime::Impl::run, Runtime::requestStop | Create/reuse buffer SRV; private deadline timer and cancellable worker wait |
| recording.cpp / drawResizedFrame, encode, RecordingPipeline::run | Reuse matching view/lease; remove redundant state; defer Finished until cleanup |
| recording.cpp / closeEncoder, notification post helper | Release scene AAC MFTs before COM/MF teardown; discard null-HWND events |
| replay.h / packetMemory; replay.cpp / reset, push, evictLocked, snapshot | Capacity/metadata/snapshot accounting; strong insertion accounting; release rejected snapshots |
| replay.cpp / saveAsync notification paths | Release null-HWND events |
| diagnostics.h / recording diagnostics formatter | Report SRV creation/reuse only in Diagnostics |


Verification/build: `CMakeLists.txt`, `tests/scene_performance.cpp`, `tests/scene_integration.cpp`, `tests/replay_buffer_stress.cpp`, `tests/final_lifecycle.cpp`, `tests/qt_quiet_launcher.cpp`, `tests/qt_frontend_checks.inl`, `tests/stage4_settings_uia.h`, `tests/run_quiet_tests.ps1`, `tests/summarize_game_frame_times.py`, `tests/frame_time_summary_test.py`, `tests/summarize_long_sync.py`, `tools/measure_final_performance.ps1`, `tools/summarize_final_performance.py`.

Documentation: `README.md`, this report. No changes to main/controller recovery, audio capture/resampler/timeline/mixer, scene model/editor/persistence, Media playback, user recordings, branding, Acrylic/QML/control layout or VS Code configuration. The stage stops for final manual review; no further feature work is started.
