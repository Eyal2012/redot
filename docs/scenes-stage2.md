# Scenes Stage 2 — implementation and verification

## Delivery and architecture

Candidate: `build/scenes-stage2/Release/PandaClip.exe`. The build folder contains the deployed Qt runtime/QML/licenses; keep it together. VS Code tasks and F5 continue targeting `build/production` from the same source. Older running builds and all recordings/preferences/evidence remain intact. Recoverable pre-change source checkpoint: `build/checkpoints/scenes-stage2-20261010-014515/source.zip`.

```
Scene definitions + selected scene (immutable snapshot)
  WGC Game / Window / Monitor       MF webcam       Image / Text
                   ↓ owned GPU-copy leases ↓
                  one D3D11 scene compositor
                    ↙                 ↘
     Capture native preview       direct NVENC H.264
     Scenes editor swapchain      ordinary MP4 + compressed replay
     (guides added here only)           ↑
     WASAPI per physical source → continuous SRC → integer 48 kHz timeline
                                  scene gains/mutes → Mix + named AAC stems
```

There is no shipping direct-window-to-encoder route. `App::startSceneSession` is the sole recording/replay entry; it attaches the compositor ingress. Capture's capsule chooses the same selected scene as Scenes. The old direct WGC session, frame pool, frame callbacks, retained-frame routing, direct audio options and direct recording/replay starts were removed from `main.cpp`. Shared D3D11/NVENC/WASAPI/clock/mux/replay components remain. Hidden legacy controls remain state adapters for existing recording preferences/migration; they are not another frontend or source workflow. Test-only generated input enters the scene compositor. A direct-ingress baseline exists only in the standalone verification benchmark.

## Video sources and lifetime

Game and Window use WGC `CreateForWindow`; Display uses `CreateForMonitor`. These are documented [Windows capture interop](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow) APIs with [free-threaded frame pools](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture). Game Capture does not inject a game hook. Exclusive/protected/minimized content remains subject to Windows capture restrictions.

Exact executable, title and optional window class identify a reconnect candidate. Ambiguity or closure leaves that layer blank; another application is never selected automatically. Missing targets are polled every two seconds; capture errors receive at most three opening/recovery attempts. Stop/Start preview explicitly releases sources and resets the retry budget. Successful previously used sessions are retained across switches, limited to eight live WGC/camera sources, and unused ones retire after 30 seconds.

WGC frames are copied to owned reusable GPU textures before pool-frame release. Four source buffers and twelve compositor buffers retain shared leases until all consumers finish. Source/output/asset textures have a combined 1 GiB budget; bounded exhaustion reports errors/drops. Text/image rasterization happens on the worker only when definition properties change. Alpha images use premultiplied blending. Static assets are limited to 4096 pixels on each axis. GPU composition applies crop, dimensions/scale, position, rotation, visibility and persisted layer order. Different output/scene aspect ratios are centered with black letterboxing. Live window/display/camera textures also retain their current aspect inside the saved source box when aspect lock is enabled, including after source resizing. Explicit Stretch disables that fit; images/text retain their edited transform.

Webcams use asynchronous Media Foundation source readers pinned to symbolic links. RGB32 conversion observes actual signed [IMF2DBuffer pitch](https://learn.microsoft.com/en-us/windows/win32/api/mfobjects/nn-mfobjects-imf2dbuffer), treats BGRX as opaque, and uploads the current frame. This initial camera path uses CPU conversion/upload, unlike WGC GPU copies; no camera audio is implied. Camera format changes and unusual drivers need physical checks.

Borderless access remains asynchronous, only requested after a valid WGC source opens. Allowed applies to every session; denied/failed access keeps outlined capture usable. API/property success does not prove Windows' visible border absent. Preview, recording and replay never sample desktop content for UI decoration.

One fixed-rate compositor worker supplies canonical frames. Recording timing remains monotonic and independent of preview. With no new source frame it composes the last owned source image; missing/closed sources are blank. Encoded repeated-composition counters describe unavailable compositor output, not every repeated WGC image. Source availability/drop counters are reported separately; a 60 FPS composed stream is not proof of 60 FPS gameplay capture. Canonical frames carry their scheduled composition-slot timestamp, rather than scheduler wake time. Missed slots are skipped and counted, never burst. Composed ingress receives one bounded frame period for GPU submission to arrive before the encoder selects that slot. PTS eligibility and the shared audio/video origin remain unchanged; this adds delivery latency, not an audio-sync correction. The direct baseline has no such delivery wait.

## Scene audio

The session plans up to 16 audio definitions referenced by any scene, pinning default endpoints at startup. One capture per physical endpoint/application identity feeds per-definition integer timelines. Application windows resolving to the same process at startup share one process-loopback lane. If unresolved definitions later collide with an already opened process, the duplicate remains silent and reports that a new session is needed to deduplicate it safely. Sources not assigned to the active scene still have planned stems, but contribute silence. All originally planned sources are opened for scene-switch readiness; this includes inputs assigned only to other scenes. Close the session to release them.

Application Audio uses process loopback for the exact selected executable/title; Desktop Audio is explicit whole-system loopback for its selected output endpoint. Microphones use the selected input, with eConsole default resolved/pinned. Device disconnection or process closure reports unavailable status and continues with silence; reconnect tries the same identity at most three times. There is no silent system/device fallback.

The existing repaired WASAPI converter is unchanged: fractional resampling/lookahead state, flagged silence and QPC-anchor/integer sample positions remain. `AudioTimeline::extract` consumes floating-point source lanes before final conversion. Separate source gains/mutes affect stems; one physical alias is mixed once. For aliases, the first non-muted, nonzero-gain assignment in persisted definition order supplies the Mix gain; each alias still has its own stem gain. Dashboard meters aggregate only physical inputs assigned to the selected scene, before recording gain/mute, and report the opened endpoint names. Gain transitions use 240 samples (5 ms) only when controls/scene assignments change, avoiding abrupt control-induced jumps; no filtering/suppression or smoothing of captured samples is added. Sum clipping is measured and clamped once.

Audio layout is Mix first, then enabled source stems in persisted definition order; all are stereo AAC at 48 kHz. Game-track flag covers application/desktop stems; microphone flag covers mic stems. Mix-only, stems-only and video-only are supported. With every audio-track flag disabled, no scene audio provider or recording audio device is started. Named `.tracks` metadata supports spaces/Unicode and all 17 audio streams. Keep metadata alongside finalized MP4s; readers may default to different tracks.

Gain/mute and switching existing scenes are live without encoder restart. Endpoint/new-source/sync changes apply between sessions and explicitly warn when saved during encoding. Shared source definitions/physical aliases must have consistent sync adjustments across scenes. Positive milliseconds delay PCM, negative advance it; defaults remain zero. Replay uses the same compressed packets, dynamic stream layout, complete AAC access units and common timestamp offset; no independent track reset. Editor handles/guides never enter recording.

## Migration and recovery

Scene JSON format remains version 1 with a persisted migrationVersion 2. Existing scene contents and selection are preserved; previous Capture configuration becomes Gameplay when empty or a separate Migrated Capture scene otherwise. Old executable/title, mic ID, explicit audio mode, gain/mute/sync migrate; recording dimensions/FPS/quality/output/replay/shortcuts remain in existing preferences. Capacity/future-format/corrupt configuration failures preserve existing files and report errors. Unresolved old targets remain blank with a selection warning.

Suspension drains the encoder and releases runtime source sessions. Wake/unlock/GPU recovery restarts preserved scene definitions, bounded to three attempts, and resumes only after composed frames exist. Exhausted recovery stays exhausted across health ticks; explicit preview Start resets the budget. Controlled tests exercise exhaustion and manual recovery without disrupting Windows devices. Idle replay/recording status cannot overwrite the capture recovery reason while draining or exhausted. A resumed recording uses a new file; replay clears history and warms again. Output write faults finalize honestly, publish no successful filename for failed work, and do not stop healthy replay after a failed export. Worker/context ordering uses shared D3D11 multithread protection, explicit context locks and leased buffers; joining encoders stays off the UI thread.

## Verification ledger

Both configurations built successfully: `build-release-recovery-status.log` and `build-debug-recovery-status.log` under `build/scenes-stage2`. Release executable SHA-256: `e9036162a9f743da0a591655ed1de8b7d91a48ffb8e9d9621a32a2e6226a7f46`.

Actual deployed Release, passive physical-desktop inspection: `build/scenes-stage2/deployed-delivery-100` and `deployed-delivery-150`, both launcher exit 0 / failures 0. Screenshots include `capture-blue.png`, `scenes-deployed.png`, `scenes-small.png`, `scenes-maximized.png`, page navigation and all five gears. Owned image/text scene sources display the same canonical content in Capture and Scenes. Normal/small/maximized/restore geometry and native client bounds were checked. Observed foreground activation was zero; no physical cursor/focus changes, recording or personal source capture occurred. Qt 1.25 inspection also passed in `deployed-stage2-final-125` before the final recovery-status-only correction. These are Qt scale factors, not changed physical Windows DPI. Screenshots returned 0 for some overlapping-window cases (including Capture behind Settings); those are not passing screenshot evidence. No new physical active-Acrylic/focus-transition claim is made; its unchanged policy retains the prior user-confirmed repair.

Real WGC attempt: `build/scenes-stage2/wgc-release-2` reported **BLOCKED**, no moving WGC frames on the inactive owned desktop. The launcher exit 0 is not a capture pass (`failures=-1`). Read-only discovery found 13 selectable application windows and no Geometry Dash; foreground was unchanged. Generated GPU fixtures and image/text screenshots are not substitutes for actual game capture. Real speech, loopback music, webcam, visible border and physical listening were not performed.

The final full-suite outcomes are recorded below. Generated fixtures, inactive-desktop WGC, deployed UI screenshots and physical gameplay/listening are distinct evidence.

A full quiet frontend run initially hung during encoding. Removing the old WGC startup had also removed D3D11 multithread protection needed for NVENC's internal context access. Protection now belongs to shared graphics initialization; the rerun passed the previous point. Subsequent failures were stale assertions expecting absent scene microphones to stay enabled, an obsolete direct-source picker workflow, and a test-only null Settings pointer. Failed artifacts were preserved rather than counted as passes.

### Final quiet suite results

- Release: `build/quiet-tests/run-20261010-031042-Release`, `build/scenes-stage2/quiet-release-delivery-final.log`, exit 0 / **PASS quiet verification**.
- Debug: `build/quiet-tests/run-20261010-031042-Debug`, `build/scenes-stage2/quiet-debug-delivery-final.log`, exit 0 / **PASS quiet verification**.
- Scene integration: 95 assertions, including final UTF-8 metadata round-trip (the metrics file records 94 before that last assertion). GPU pixels verify crop/rotation/alpha/layer/visibility/text, live-source aspect fit vs explicit stretch, and letterboxing. Actual NVENC recording/replay decoded frames verify initial composition and two scene switches. All emitted streams decode, including Mix-only, eight game stems, eight mic stems, video-only and Mix + 16 stems. Video-only proves the audio provider never starts.
- Production audio-session fixtures verify integer PCM continuity, per-source gains/mutes, scene switching, alias deduplication (including different windows in one process), and incompatible sync rejection. Existing continuous-SRC tests compare partitioned/unpartitioned samples at 32/44.1/48/96 kHz and drift rates; lossless checkpoint comparison, AAC delay and common replay-offset regressions pass. These are silent fixtures, not HyperX/application-loopback listening.
- Editor/application-picker tests run at Qt scale 1.0/1.25/1.5: real owned window enumeration/search/multi-window/close/restart, direct transforms/crop/rotation/locking/hiding/alignment and persistence. Quiet frontend checks cover existing player seek/track/pause controls, audio meters, settings scrolling, five gears, captions/layout restore, bounded logging and replay eviction/export.
- Controlled recording and export faults publish no successful failed file; replay keeps encoding after export failure. Recovery fails exactly three attempts, stays exhausted, retains its reason, and explicitly restarts/records successfully. All three successful files remain intact and decode. No disk filling, Windows device disruption or personal application control occurred.
- Release generated frontend polling mean/max: 1.62 / 4.99 ms; Debug 4.53 / 18.95 ms. These are lightweight UI ticks in the concurrent quiet suite, not whole-UI latency or gameplay timings.

Failed runs `run-20261010-030459-Release` and `run-20261010-030509-Debug` caught the recovery-error overwrite: the idle replay status replaced the capture exhaustion reason. The controller now protects capture recovery status while draining/exhausted. Both final reruns pass; failed evidence remains preserved.

## Release performance evidence

Evidence: `build/scenes-stage2/performance-20261010-030856/{direct,scene,layers}`; raw metrics, owned-PID Windows GPU counters and `summary.json` are preserved. RTX 5070 Ti, 1920×1080/60, Balanced NVENC, ordinary recording plus replay sharing one encoder. Each generated moving-GPU run lasts 30 seconds; CPU/private-memory measurement covers the last 25 seconds after five seconds warm-up. Audio and previews are off. Other applications were left running. These are sequential pipeline measurements, not an isolated whole-PC/game benchmark.

| Metric | Test-only direct baseline | One scene layer | Scene + alpha image + text |
| --- | ---: | ---: | ---: |
| Process CPU, percent of one core | 4.00% | 4.38% | 6.75% |
| Compose CPU mean / max, ms | — | 2.39 / 28.65 | 2.48 / 21.07 |
| Compose GPU query mean / max, ms | — | 0.85 / 30.49 | 1.17 / 31.56 |
| Owned-PID 3D engine utilization mean | 0.779% | 0.865% | 0.843% |
| Private memory early → late, MiB | 270.2 → 272.0 | 297.5 → 308.5 | 304.1 → 315.7 |
| Compositor-owned texture memory, MiB | — | 31.64 | 32.24 |
| Encoder wall mean, ms | 3.82 | 3.77 | 3.85 |
| Encoded frames | 1801 | 1801 | 1799 |
| Encoder queue / compositor pool / producer drops | 0 / — / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| Missed composition slots | — | 5 | 11 |
| Encoder repeated input frames | 4 | 49 | 36 |

GPU query intervals include scheduling/preemption, and compose CPU includes context-lock waiting/submission. GPU engine counters are per-engine, not summed into a fictitious whole-GPU percentage. Both NVENC engines reported approximately 4.8% each; dedicated usage and all raw samples are retained. GPU counter sampling spans the full run rather than only the 25-second CPU interval.

The initial one-layer timing probe (`performance-20261010-024436`) had 342 repeated composed frames in 30 seconds. Scheduled-slot timestamps and the bounded delivery period reduced that to 49 in the final probe, with zero queue drops. Remaining repeats and max timings above a frame period are real scheduling limitations; no perfect-pacing or actual GD smoothness claim is made. The private-memory increase during this short run does not establish long-session stability. A 10+ minute moving gameplay/load check remains manual.

Repeat silently with `& .\tools\measure_scene_performance.ps1`. Only its positively identified child processes are bounded/terminated on timeout; no capture or personal input is used.

## Changed files

New: `src/scene_runtime.{h,cpp}`, `src/scene_audio.{h,cpp}`, `tests/scene_integration.cpp`, `tests/scene_audio_checks.h`, `tests/scene_performance.cpp`, `tools/measure_scene_performance.ps1`, this report.

Updated production: `CMakeLists.txt`, `src/main.cpp`, `src/workspace_ui.inl`, `src/frontend_bridge.h`, `src/frontend_adapter.inl`, `src/frontend_qt.cpp`, `src/scene_model.{h,cpp}`, `src/scene_editor.{h,cpp}`, `src/recording.{h,cpp}`, `src/audio_timeline.h`, `src/replay.{h,cpp}`, `src/clip_track_names.h`, `src/clip_library.h`.

Updated verification/docs: `tests/quiet_app_source.inl`, `tests/quiet_preview.inl`, `tests/dashboard_checks.inl`, `tests/qt_frontend_checks.inl`, `tests/qt_fault_checks.inl`, `tests/qt_wgc_checks.inl`, `tests/qt_quiet_launcher.cpp`, `tests/replay_sync_verify.cpp`, `tests/run_quiet_tests.ps1`, `tests/prepare_scene_profile.py`, `tools/review_scenes.ps1`, `README.md`. Existing Acrylic/QML design-system files and VS Code configuration are unchanged.

## Manual acceptance checklist

1. Use `tools/review_scenes.ps1` for an isolated profile; select Gameplay. Add actual GD from the picker, confirm live source status and the same image in Scenes/Capture. Close/reopen GD; confirm exact reconnect, no unrelated target.
2. Add alpha Image/Text and a camera if available. Move/resize/crop/rotate/reorder/hide/lock. Record and export replay; inspect encoded output and absence of guides, including switching scenes during the session.
3. Add Application Audio for GD and the exact HyperX mic. Speak/music/effects with mic-only, app-only and both; listen to each named stem and Mix externally and in Media. Compare the same track/time/gains against Windows Sound Recorder or OBS. Nonzero meters and decoding are not listening proof.
4. Test a 10+ minute moving GD/replay session under normal and heavy GPU load. Compare game frame times capture off versus replay on, inspect recording cadence and synchronization near beginning/end. Disconnect/reconnect the selected mic and camera; verify reported silence and no source substitution.
5. Test physical 100/125/150% scaling, source handles/alignment, resizing, captions/drag/snap, popovers and two-monitor focus transitions. Quiet tests only set Qt scale factors and never alter Windows settings or personal focus.

Limitations: no physical GD/WASAPI speech/camera/visible-border/listening claim from fixtures; webcam CPU upload; exact-title reconnect may need picker reselection if an app changes title; session-wide audio stem plan is fixed; new audio devices/sync apply next session; no live speaker monitoring. No unrelated features or UI redesign were added.
