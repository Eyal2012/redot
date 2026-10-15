# Stage 5 - performance, stability and reliability (fast verification)

Scope was cut short by request: the 33-run "after" matrix and any multi-hour endurance run were **not** performed. This note
records what was measured, the one confirmed defect that was fixed, and what remains unverified.

## Confirmed defect and fix: compositor stalled every 2 seconds

* **Symptom.** Every compositor-bearing benchmark mode skipped ~65-75 of 3600 frame slots per minute at 60 FPS (~150-200 of 7200 at
  120 FPS) and the CFR encoder repeated the same number of frames (~2%). 4-of-6 and 0-of-6 flash frames were lost in the encoded video.
* **Root cause (measured).** `Scenes::Runtime` called `discoverApplications()` (desktop-wide window and process enumeration, with a
  blocking `WM_GETICON` round trip of up to 15 ms per window and icon conversion) on the frame-pacing thread every 2 s for any scene
  that contains a Game/Window source, even when that source was live and healthy. Measured on this machine (16 windows): median
  **48.7-63 ms** per call (3.8 frames at 60 FPS, 7.6 at 120 FPS). Predicted ~66 late slots/min at 60 FPS and ~165 at 120 FPS; observed
  61-75 and 150-198.
* **Hypotheses tested and rejected:** the measurement sampler (62/68/64 late slots with/without `Get-Counter`), Windows power throttling
  (61-72 default/off/on), the fixture generator's `Flush()` while holding the shared context mutex (63-70 with or without), and timer
  wake latency (bare high-resolution timer: p99.9 < 1.1 ms, 0 late wakes).
* **Fix.** (1) Discovery runs only when a Game/Window source is about to be opened or retried (no entry yet, definition changed, closed
  window, or retry window reached with attempts < 3) - never while the session is live. (2) The compositor passes
  `withIcons=false`; the Scenes picker keeps icons (default parameter). Files: `src/scene_runtime.cpp`, `src/application_picker.*`.
* **Before/after (same harness, same workload).** `discoverApplications` median 48.7 ms -> 1.4 ms (no icons). `capture` 60 FPS:
  late slots 61-75 -> **0**, composed 3531-3540 -> 3600-3601 of 3600. `capture` 120 FPS: 150-198 -> **0**, 7033-7042 -> 7201.
  Full-pipeline round (one round, 11 configurations): late slots 0 (one config 1), repeated frames 0 (one config 9 at 120 FPS), encoder
  queue drops 0.
* **Regression test.** `tests/run_quiet_tests.ps1` now runs a 20 s `capture` 60 FPS run and fails above 3 late slots or below 1190 composed
  frames. Focused re-run after the fix: pacing 0/1201, `ReDotScenesTest` pass, `ReDotSceneIntegration mkv` 114 checks pass.

## Audio-sync measurements: fixture/verifier artifacts, not application defects

* A constant 50-80 ms beep-before-flash offset in the live fixture has a measured cause. The encoder's shared clock origin was 44 ms
  **after** the harness loop start (`fixture_loop_start_steady_ns` vs the logged origin) and the harness anchors PCM to its loop start,
  so the beep landed at 9955 ms for a nominal 10000 ms (exact match). The video flash sits on the compositor timeline: its ~16-30 ms lag
  is the documented one-render-period delivery latency. Offsets had a 4 ms spread and no drift. The writer-level fixture (no live
  clocks) measures 0-1 ms. The earlier "spread ~10 s" results were verifier mispairing caused by flash frames lost to the stalls above
  (now gone), and event 1 (t = 0) is inherently ill-defined. Real WASAPI/WGC sync is still unmeasured (needs the manual test).
* Startup `audio_late_frames`/`audio_timeline_missing` (thousands of frames) are a one-time start-up artifact of the same origin offset;
  the earlier 30-minute run showed 4718 after 30 min vs ~5000 after 15 s.

## Existing evidence reused (not re-run)

* 10 min record + replay + exports (2026-10-10, MP4): 35,998/36,000 frames, 0 repeats/drops, 120 files all decode.
* 30 min record (MP4, concurrent load): 13.7% of one core, private 351 -> 436 MiB, 111 encoder queue drops under load.
* 2 h soak (`reliability-two-hour-20261010-194839`, MP4 + replay + exports + scene switching + injected faults): no crash/hang,
  431,997 frames encoded, 756 exports (84 injected failures), replay history bounded (~12 MiB). **Private memory 361 -> 543 MiB (steady
  slope ~25 KiB/s, ~88 MiB/h, root cause not identified); handles ~990-1010 flat; 132 queue drops and 1935 late slots (pre-fix
  discovery stall plus ambient load); `audio_timeline_missing` = 15.9 M frames in this run vs 0 in the replay-only 2 h run - unexplained.**
* 2 h replay-only soak (`reliability-final-replay-two-hour-20261010-202706`): private 381 -> 456 MiB (~10 MiB/h), 65 queue drops.
* This stage, benchmark baseline (3 rounds, 60 s, 1080p, generated sources, machine shared with a running game, OBS and Discord):
  GPU 3D ~1.0% (1.7% at 120 FPS) and video-encode engine ~4.3% (8.6%) for the process; encoder wall time 2-5 ms per frame (budget 16.7 ms);
  0 encoder queue drops in every mode; MKV file ~5.3 MiB/min vs MP4 ~5.2 MiB/min for the simple fixture. Process CPU is too noisy under
  the concurrent load to separate MP4 from MKV (same config 6%-16% of one core between rounds).
* Start/stop stress (`ReDotStartStopStress`, 90 cycles alternating MKV/MP4, with replay and exports): 0 failures, threads flat (~90),
  private +4.4 MiB, handles 750 -> 762; a small positive trend (~0.15-0.2 handles and ~80-110 KiB per cycle) appeared in both runs and is
  not proven to be either noise or a leak.

## Final investigation: missing audio frames and memory growth (source and log analysis, no new long runs)

* **15.9 M `audio_timeline_missing` is a fixture artifact, not audio loss.** The counter (`src/audio_timeline.h`) counts every output frame
  where an enabled lane has no data (startup, device-silence recovery, tail padding), not discontinuities. In the 2 h record run it
  tracks `audio_late_frames` almost 1:1 (15,935,442 vs 15,941,586): PCM arrived after its slot was already written. That run's
  `metrics.txt` has no `independent_fixture_audio_producer` line, i.e. it used the older generator that fed audio from the GPU/video
  thread, which the harness comment says "manufactured late/missing samples under stress". Runs with the independent producer show
  1,818 late / 0 missing (2 h replay-only), 4,718 / 622 (30 min record) and 168-4,168 / 0-1,224 (55 s post-fix), consistent with the
  one-time startup offset. The replay-only vs record difference was the fixture version, not the mode. Recorded files were not
  inspected for real gaps (no ffprobe/ffmpeg available); real WASAPI audio remains unmeasured.
* **Memory growth: unresolved, no leaked object identified.** Private memory returns to 15-31 MiB at process exit; replay history
  (~12 MiB), threads (~90), handles (~980) and image/mapped regions are flat. Part of the 2 h growth (~+77 MiB) is a one-time step
  (`gpu_owned_bytes` 24 -> 80 MiB near 2,700-3,300 s; probably a one-time cache, not confirmed). The remaining slope is ~40 MiB/h in the
  full soak (vs ~7 MiB in 1.5 h replay-only) and tracks completed exports at roughly **110 KiB per export** (38 MiB over 342
  exports), matching the 80-110 KiB per cycle seen in start/stop stress. It is in private regions, not the muxer or replay ring. Cause
  (allocator fragmentation or a small per-export/per-session object) is **not determined**; it would need a heap/allocation trace of
  a short export loop. No fix was made.

## Not verified

MKV (or MP4) long-run memory growth after this fix and over hours; the 88 MiB/h growth above; the unexplained 15.9 M missing audio
frames; real WGC capture and real WASAPI game/microphone sync; behaviour with a live game window as a scene source (the fix is
unit-verified only with the generated fixture); Debug/Release full quiet suites on the final Stage 5 source; hours-long start/stop cycling.
