# Desktop audio continuity repair - 2026-10-09

## Scope and status

Work performed inside the verified PandaClip workspace. Source checkpoint: `build/checkpoints/desktop-audio-20261009-202425/source.zip`. No recording, preferences, evidence files or frontend implementation was removed. Qt/QML materials, Acrylic policy and `.vscode` remain unchanged.

The user physically tested the isolated Release candidate with Geometry Dash and the HyperX Cloud III Wireless microphone and replied **"yea i think its fixed."** This is a tentative positive short listening result. It does not establish long-session, GPU-load or OBS equivalence. No automated audio output, personal cursor/focus manipulation or control of the user's sessions was performed.

The final delivery is `build/desktop-audio-fixed/Release/PandaClip.exe`. The earlier `build/desktop-audio` candidate was used for the listening review and is left intact while running. The final delivery retains the same audio repair; its additional change is correcting diagnostic CSV times for indexed Processed/Mix entries. That metadata correction does not modify audio samples or timing. A new build directory avoids replacing DLLs/executables used by the review or existing sessions.

## Demonstrated implementation defects

1. **Packet-local sample-rate conversion:** `AudioCapture::run` restarted interpolation position at zero per WASAPI packet and clamped its last interpolation to the last sample within that packet. A fractional output-count remainder alone cannot preserve source position. At 44.1 -> 48 kHz and 32 -> 48 kHz, interpolated boundary samples need the next packet's first sample. Irregular packet sizes or device-rate correction also change phase, creating discontinuities. The stateful linear converter carries fractional position and one native frame of lookahead; it does not add smoothing/filtering/noise suppression.
2. **Flagged silent packets were skipped:** SRC phase/count did not advance through them. Silent packets now contain the correct number of zero native samples (unsigned PCM8 uses 128), go through the same converter and occupy their correct timeline interval.
3. **Independent packet timestamp rounding:** every converted packet was placed from its QPC timestamp with integer truncation. Jitter/rate conversion could cause overwritten or missing sample slots. Capture now uses one calibrated QPC anchor per stream epoch plus the converter's integer output index. Device clock correction still adjusts conversion rate; video and audio remain on the common monotonic mapping. Actual loss/reset is handled explicitly, not disguised by a new timestamp for every packet.

The first corrupted stage in the deterministic reproduction is conversion/timeline placement, before mixing/AAC. These demonstrate software defects capable of distortion. They do not retroactively prove that every pop in every old clip had one cause. The real new raw input was not clipped, and its processed/mixed samples match the independent reference exactly; mixer clipping was not demonstrated in the user's review.

## Exact implementation changes

- `src/audio_capture.cpp`, `AudioCapture::run`: copy/return native WASAPI packets before processing, check ReleaseBuffer HRESULT; enqueue flagged silence; stateful SRC; per-epoch clock/index; bounded measured device gap/duplicate handling; same-device recovery retained; MMCSS Audio thread registration with failure log; raw WAV tap. Preserve native float headroom for diagnostics.
- `src/audio_resampler.h`, `AudioResampler::process/drain`: continuous fractional position and one-frame lookahead, no phase restart or per-packet end clamping. Exact 48 kHz identity path; final held fractional sample drained.
- `src/audio_pcm.h`, `readPcm/stereoPcm`: native stride/type conversion shared with tests; mono duplication, stereo preservation, extensible channel-mask downmix. Do not misinterpret quad rear-left as center. No undocumented normalization/boost. Unsupported unmasked multichannel layouts retain the front pair.
- `src/recording.h`, `RecordedAudio/RecordingIngress`: continuous stream clock/index metadata and continuity/clipping counters; optional shared trace writer.
- `src/audio_timeline.h`, `AudioTimeline::ingest/build`: extracted production mixer unchanged in gain/quantization semantics; map anchor once plus integer sample index; count overlaps/empty slots/mix clipping; pre-gain Desktop and exact pre-AAC Mix taps.
- `src/recording.cpp`, `RecordingPipeline::run`: create opt-in trace on worker, share with source threads, set common origin, drain/finalize checkpoints and log failures. Recording/replay AAC/NVENC/mux algorithms remain unchanged; both consumers use the corrected PCM path.
- `src/audio_capture_trace.h`: bounded asynchronous native/processed/mixed lossless WAV writer, exact packet CSV and honest finalization status. At most 120 seconds/four sessions/64 MiB per file; 16 MiB/2,048-event queue. Diagnostic failure does not stop recording/replay.
- `src/diagnostics.h`: measured discontinuity/loss/duplicate/clock reset/over-range/mix clipping counters in the existing diagnostics report.
- `tests/audio_continuity_test.cpp`, `tests/run_quiet_tests.ps1`, CMake: new actual production converter/timeline/writer component regressions; no generated seam in the shipping executable.
- `tools/review_capture_audio.ps1`, `play_capture_checkpoint.ps1`, `analyze_capture_checkpoints.py`, `compare_capture_processing.py`: isolated user review, explicit playback commands, waveform statistics and independent processing reconstruction. Existing launch configuration stays intact.

## Objective measurements

Deterministic 2-second stereo fixture, 257/441-native-frame packet partitions:

- Old 44.1 kHz packet conversion versus continuous reference: RMS error **0.0788236**, peak **0.277046** (normalized float). This is a generated stress fixture, not a real old recording or perceptual score.
- Corrected packet partition versus continuous reference at 44.1/47.988/48.024 kHz: peak difference **5.96e-8**; 48/96 kHz identity-compatible cases: zero. 32 kHz additionally covered for the actual HyperX format.
- Controlled one-output-sample timestamp jitter over 100 blocks: old placement **49 missing slots / 33 late frames**; corrected continuous placement **0 / 0**, no overwrites.
- Ten-minute offline silence conversion: exactly **28,800,000 stereo output frames**, with one retained native frame. This is a converter duration/memory-bound check, not a ten-minute WASAPI recording.
- Generated four-WAV fixture: Raw Desktop equals Processed Desktop; Raw Microphone preserved; Final Mix equals the exact float-sum/16-bit quantization; equal frame counts and zero trace loss. Controlled blocked diagnostic path terminates and reports failure.

### User-operated real review

Artifacts: `build/audio-capture-review/20261009-223434-1c8f6d`.

- Recording `output/PandaClip_20261009_223452.mp4`: **25.800 s** video; all three AAC streams decode at **48 kHz stereo**, each **1,238,016 frames / 25.792 s**, contiguous timestamps. Read labels from `.tracks`: native Windows reader indexes are not muxed track IDs.
- Actual selected application: **GeometryDash PID 29580**, process-tree loopback. Raw desktop **44,100 Hz stereo S16**, native position constant zero (handled by sample-count continuity, not treated as duplicate packets).
- Actual input: **Microphone (HyperX Cloud III Wireless)**, pinned endpoint, raw **32,000 Hz mono float32**, extensible mask `0x4`. No alternate input substituted. Both sources enabled, gains 1.0, mutes off, sync adjustments zero.
- Raw Desktop: 25.8050 s, peak **0.0607605**, RMS **0.0201427**. Processed Desktop: 25.792 s, peak **0.0597317**, RMS **0.0200566**. Raw Mic: 25.8124 s, peak **0.161469**, RMS **0.00674472**. Final Mix: 25.792 s, peak **0.187683**, RMS **0.0211614**. No nonfinite/over-range samples. Activity does not by itself prove spoken input quality.
- Audio queue loss **0**, late audio frames **0**, native missing/duplicate frames **0/0**, timeline overwrites **0**, over-range source frames **0**, mix clipped frames **0**, timestamp fallbacks **0**. Mic reported one initial discontinuity flag without measured loss; Desktop zero. All 6,115 missing timeline slots equal the initial source offsets (3,121 Desktop + 2,994 Mic), so this take has no additional timeline holes. Desktop packet QPC cadence was exactly 10.000 ms; Mic packet cadence ranged 9.9632-10.0384 ms; neither had an interval over 20 ms.
- Four WAVs finalized, zero dropped diagnostic events; raw startup padding 2,867 Desktop native frames / 1,996 Mic native frames. Raw capture ends differ slightly from the final mix block and retain native clock drift.
- Independent reconstruction from original native WAV samples and packet QPC/device positions matches **2,476,032 Processed Desktop samples exactly** and **2,476,032 Final Mix samples exactly**, including rate correction, stereo mapping and signed-16 quantization. Gain/mute/sync were unchanged during this session.
- Offline legacy conversion applied to these *new* raw Desktop samples differs at 3,896 samples (>1e-7), peak **0.00225144**, RMS **1.8074e-5**. This is a counterfactual, not a before-fix physical take.
- User reported “yea i think its fixed.” No agent physically listened to audio or witnessed gameplay. Selecting the real GD process and fresh WGC availability do not establish active-level gameplay/performance. The generic flash/beep detector's accidental events in this clip are **not** synchronization measurements.

## Verification and remaining work

Debug and Release builds and full quiet regression suites passed for the listening candidate **and final delivery**. Final delivery rebuilds the minor indexed-CSV correction and verifies nonnegative/correct Processed/Mix CSV times.

- Candidate Release: `build/quiet-tests/run-20261009-223322-Release`.
- Candidate Debug: `build/quiet-tests/run-20261009-223400-Debug`.
- Final Release: `build/quiet-tests/run-20261009-224254-Release`.
- Final Debug: `build/quiet-tests/run-20261009-224511-Debug`.

Build logs are under `build/desktop-audio/build-delivery-release.log` and `build-delivery-debug.log`; no compilation errors. The existing verifier has a signedness warning unrelated to this patch. The production capture fixes compile without new warnings. Suites cover synthetic direct-NVENC/AAC video-only/game-only/mic-only/both ordinary recordings and replay export decoding; bounded replay stress; controlled write faults; real Qt controls/lifecycle; sample-order/byte-count/short-write/underrun playback regression fixtures. Generated recordings do not validate audible live loopback or microphone quality.

Manual checklist still needed:

- Listen separately to real Raw Desktop, Processed Desktop, Raw Mic and Final Mix; note first stage/time of any remaining pop.
- Repeat real desktop-only, mic-only and both; listen to Game/Microphone/Mix AAC tracks explicitly in PandaClip and an external player.
- Both-source instant replay, repeated saves and seeking; confirm audio remains equally clean.
- Ten-minute moving GD gameplay and usual/heavy GPU/CPU load; watch diagnostics for actual missing frames/queue drops/clipping.
- Compare OBS with the same game section, HyperX endpoint, sample format and gain (reference only; no integration). Windows Sound Recorder is a microphone reference, not a desktop-loopback reference.
- Check Windows/device enhancement settings, wireless connectivity and endpoint driver only if **raw** native samples still audibly distort. None were blamed or changed automatically.

Use `tools/review_capture_audio.ps1` again for four lossless files, or `-GameMode WholeSystem` for explicit system loopback. Normal runs do not save raw audio. The first diagnostic session spans up to 120 seconds; long MP4/replay sessions remain unrestricted by the trace duration. No claim of full long-term elimination or OBS parity is made from one short take.

## Official references used in the investigation

- [IAudioCaptureClient::GetBuffer](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer): native packet ownership, silence/discontinuity/timestamp flags, first-frame position and QPC units.
- [IAudioCaptureClient::ReleaseBuffer](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-releasebuffer): matched packet release on the same thread.
- [WASAPI loopback recording](https://learn.microsoft.com/en-gb/windows/win32/coreaudio/loopback-recording): loopback capture semantics.
- [Microsoft Application Loopback sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/): selected process-tree capture; no whole-system fallback.

