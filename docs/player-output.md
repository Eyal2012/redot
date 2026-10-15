# View Media distortion investigation — 2026-10-09

## Current result: audible distortion is not fixed

The user initially reported clean external MP4 playback. The audio-only WinMM
candidate still sounded distorted. The user then listened to the **same Mix
decoded independently by Windows to WAV** and replied: **“External Mix WAV also
distorts.”** This establishes distortion before PandaClip's device-output path
in this representative clip. The exact source/capture/mixing/encoding cause is
**not established**. The unsuccessful candidate was removed from production.
Normal playback remains Qt's Windows sink, with opt-in diagnostics. No recording,
capture, replay, audio mixing/encoding, Acrylic, QML, preference or VS Code
configuration change was made.

Workspace: `C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip`.
Recoverable checkpoint: `build/checkpoints/player-output-20261009-185428/source.zip`.
No Git repository exists. Older running deployments were not overwritten or
controlled. The user launched the isolated review window themselves.

## Actual playback path

`MediaController` owns one `QMediaPlayer`, `QAudioOutput` and video sink. Qt's
FFmpeg plugin demuxes MP4 and decodes AAC planar float. Its audio renderer uses
swresample to produce interleaved PCM, retains a byte offset across accepted
short writes, and sends that PCM to `QAudioSink`. The Windows sink negotiates
the endpoint mix format, optionally converts with Windows Media Foundation,
applies volume, acquires a WASAPI buffer and releases the submitted frames.
The renderer and native output run on Qt audio workers, not QML paint callbacks.
Video continues through the existing FFmpeg/D3D11 RHI path.

On this PC the actual HyperX **output** endpoint is 48 kHz stereo float32,
eight bytes per frame. Input/output formats match, so the native conversion is
pass-through. Other endpoint formats/sample rates have not been verified here.
This output device is separate from microphone capture. No microphone hardware
fault or capture-clock error was demonstrated by this investigation.

Qt source inspected is the pinned, corresponding 6.8.3 archive supplied with
the app. Relevant upstream references: [Qt multimedia backends](https://doc.qt.io/qt-6.8/qtmultimedia-index.html),
[audio buffer output](https://doc.qt.io/qt-6.8/qaudiobufferoutput.html),
[WASAPI render buffer ownership](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nn-audioclient-iaudiorenderclient).

## Diagnostic checkpoints

Opt-in `PANDACLIP_PLAYBACK_TRACE` writes only to a **fresh folder under this
workspace's build directory**. Normal sessions produce no raw PCM trace.

- A: public Qt decoded output, float32 interleaved stereo 48 kHz.
- B: bytes actually accepted by the Windows sink, after FFmpeg conversion.
- Converted: native format conversion before playback gain.
- C: the gained PCM submitted to WASAPI or the explicit WinMM candidate.
- `writes.csv`: QPC, requested/accepted bytes, output frames, queued frames,
  output clock, volume and native result. Commands include open/seek/track/pause.
- `summary.txt`: explicit writer finalization and trace loss/write failures.

The writer is bounded by 16 MiB, 8,192 events and 1 GiB of payload per process.
Disk operations occur on its worker. Close the review app normally to finalize
the trace; no finalization summary means incomplete evidence. Original MP4s are
read or copied, never rewritten. The comparator generates IEEE float WAVs for
each checkpoint, checks frame/byte counts and reconstructs accepted PCM in
event order across renderer replacement, seeks and track changes.

Silent hardware checks preserve normal 70% sample gain and mute **only their own
Windows audio session**, verifying mute before starting. They never change
endpoint/global volume or another process's session. They exercise actual
device submission and clocks but are not listening tests.

## Measurements

Representative existing microphone replay:
`PandaClip_Clip_20261009_184420_972_000000.mp4` (35.68 seconds).
Representative ordinary recording: `PandaClip_20261009_174215.mp4` (27.62 seconds).
The user says all clips are affected; these are real existing clips, not a
user-identified precise crackle timestamp.

| Check | Result |
|---|---|
| WASAPI real Microphone, Release | 1,712,128 decoded/accepted/submitted frames; A=B exactly; conversion unchanged; C exact 0.7 gain; zero gaps/errors/short writes |
| WASAPI real Microphone, Debug | Same frame count and exact PCM relationships; zero gaps/errors/short writes; final queue zero |
| WinMM candidate, same Microphone | Same exact samples/counts; final native clock 1,712,128 frames and final queue zero; **user still reports distortion** |
| Real ordinary Mix seek/pause/track exercise | 7 starts/stops; 6 renderer clients; zero output errors; ordered acceptance equals decoded sequence; conversion/gain exact across all clients |
| Same Microphone played three times | 5,136,384 frames, 5,016 packets; 3 completed playbacks, 3 starts/stops; zero output errors or noninitial Idle events; 2 intentional source timestamp resets |
| Independent MF decoder, Microphone | 1,712,128 frames; RMS difference 0.000007694, max 0.000015274; **zero samples differ by more than one s16 step** |
| Independent MF decoder, same replay Mix | 1,712,128 frames; RMS difference 0.000008785, max 0.000015497 after quantization/clamping; zero differences over one s16 step |
| Same replay Mix submitted at 70% gain | Peak 0.77346; no submitted clipping/nonfinite samples; zero dropped/duplicate PCM frames or errors |

Mix's float decode has 13 samples outside [-1,1] (AAC overshoot); none exceed
the submitted unit range at 70% gain. This does not establish the audible
crackle's cause or justify changing recording/adding suppression.

Independent source-reader inspection shows native MF audio order
Microphone/Game/Mix, selecting Microphone by default. Qt FFmpeg and the sidecar
use Mix/Game/Microphone, defaulting to Mix. Native presentation IDs are **not**
the original MP4 track IDs. PCM correlation identifies the stems. This means a
default-track external comparison may compare different stems; the actual
external player's selection still needs confirmation. The user describes the
symptom as mostly Mix rather than Microphone, not an unequivocal clean
Microphone listening result. PandaClip's explicit
track ordinals remain correct. No default was silently switched to hide Mix.

Artifacts under `build/player-output`: `complete-mic`, `wave-final-mic`,
`wave-final-mix`, `same-replay-mix`, `debug-real-mic`, `repeated-real-mic`,
`independent-real-replay`, `mic-independent-final.json`,
`mix-independent-final.json`, `independent-real-replay.log`.
The first `baseline-real-*` traces did not finalize and are preserved honestly
as incomplete; explicit flush was then added. The review trace under
`build/playback-review/20261009-194824-waveout` is incomplete until its user-owned
window closes. Later commands include explicit Microphone and Game choices.
Complete inspected renderer sequences preserve conversion/gain; 302,080
Microphone frames match a contiguous decoded source slice exactly. Observed
delivery gaps were below 24 ms with no noninitial empty queue in inspected
complete rows. `live-review-comparison.json` labels this unfinished evidence.

## Alternatives tested and rejected

Forcing Qt's native Media Foundation backend failed the existing GPU-video
texture check. It was reverted, not shipped as a fix. `native-ui` preserves the
failed evidence. It also presents a different audio order and cannot safely use
FFmpeg ordinals without mapping.

The explicit `PANDACLIP_PLAYBACK_OUTPUT=waveout` investigation changes only
native audio submission. It maps the exact IMM endpoint using
[the supported legacy endpoint mapping](https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-roles-for-legacy-windows-multimedia-applications),
uses the same negotiated frame budget, holds at most 64 immutable native packets,
and releases each PCM lease only after WHDR_DONE. No second decoder, device
fallback, noise suppression, arbitrary delay or buffer increase is used. It
passed objective checks but **failed the user's audible comparison**, so it
was removed from production. Source/recipe/patch evidence is retained under
`build/player-output/rejected-waveout`; the previously deployed experimental
binary is left intact for the user-owned review session. New builds have no
alternate output switch or renderer. No trial is promoted as a distortion fix.

## Build and regressions actually run

MSVC x64 Debug and Release built successfully in `build/player-output`. Initial
all-target compilation exposed a pre-existing missing comctl32 link for the
BackdropProbe test and missing direct QAudioFormat/QAudioBuffer includes; both
were repaired. The verifier now releases its diagnostic media source before
MFShutdown; the earlier trial's shutdown crash is retained in its artifacts.

After removing the rejected renderer, **Debug and Release built successfully**
in `build/player-diagnostics`. Both full suites passed again on that final
deployment: `build/quiet-tests/run-20261009-201118-Release` and
`build/quiet-tests/run-20261009-201311-Debug`. Its real Microphone output check
(`build/player-diagnostics/final-real-mic`) repeats the same 1,712,128 frames,
exact ordering/conversion/gain and zero output errors/gaps/short writes, with a
zero final queue. The 15 new fault cases were also run separately and are now
included by the quiet-suite runner for future runs.

Final Release SHA256: `PandaClip.exe`
`114c00c1dd92cf3c7d308dea3d7b81119ace1c0e0a4a014b7cc53eb1c29e2a36`;
`Qt6Multimedia.dll`
`14d0bc65f6737b47a831070f95de9a7c7fa827098ccd4f0550abcbe1a79641d3`.

Full existing quiet suites passed:

- Release candidate: `build/quiet-tests/run-20261009-194549-Release`.
- Debug normal WASAPI: `build/quiet-tests/run-20261009-195442-Debug`.

These include real NVENC/AAC/mux/replay code with generated GPU/PCM fixtures,
eight source-combination MP4s, independent decoding, logging/border/meters,
deployed shipping smoke, actual Qt player controls/RHI video, restoration,
export-write faults and synchronization comparator regressions. They use an
owned inactive desktop without affecting personal mouse/focus or sounding
audio. They do not establish game capture cadence, microphone speech quality
or audible playback. No physical microphone, OBS, VLC or external player was
automatically recorded/listened to.

`tests/playback_checkpoint_regression.py` passed 15 deterministic cases:
missing, duplicated, reordered, planar/misaligned samples, conversion/gain
corruption, mismatched counts, reported output failure, trace loss, incomplete
finalization and wrong track sequence are detected. A valid short write is
reported but not treated as corruption. These are injected diagnostic faults,
not induced hardware underruns. Existing full suites were explicitly requested;
F5 still never runs them.

## Changed files/functions

- `src/frontend_qt.cpp`: MediaController diagnostic buffer/command hooks and
  explicit trace finalization after frontend/player destruction. Normal UI and
  playback routing are unchanged.
- `tools/build_qt_audio.py`: pinned Qt core trace injection and separate cached
  runtime directory so old sessions survive; the failed output trial is removed.
- `tools/qt_playback_trace.h`: bounded worker and PCM/count/clock evidence.
- `build/player-output/rejected-waveout/qt_wave_output.h`: archived failed
  audio-only comparison renderer; no production reference remains.
- `tests/playback_audio_probe.cpp`: actual submitted-PCM silent checks,
  pause/seek/track exercise and three consecutive playbacks.
- `tests/replay_sync_verify.cpp`: native stream-descriptor evidence and safe
  diagnostic source teardown.
- `tools/compare_output_checkpoints.py`, `tools/compare_independent_audio.py`,
  `tests/playback_checkpoint_regression.py`: sample-level comparisons/fault tests.
- `tools/review_playback.ps1`: user-launched isolated listening profile.
- `CMakeLists.txt`, Qt license/patch and this documentation: deploy matching
  modified-source files and the test link repair.

`build/player-output/checkpoint-diff.json` confirms all engines, QML,
desktop-backdrop code and VS Code configuration match the checkpoint.

## Boundary of this playback-only task

The confirmed external Mix WAV distortion contradicts the initial inference
that the saved audio is completely clean and only the player introduces it.
This does not rule out additional playback issues, but prevents an honest claim
that changing playback fixed the source distortion. Investigating raw game
audio, capture conversion, mixing and AAC requires expanding the explicit
restriction to View Media only. Those components remain unchanged. The existing
recording log reports no audio queue drops, late audio or timestamp fallbacks
around 18:44; these counters do not prove waveform continuity or exclude a
resampling defect. No hardware fault is inferred.

Close only the user-launched review app normally when finished to finalize its
trace. Personal recording/replay sessions remain untouched.

For a new baseline review (not an automatic playback command):

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\tools\review_playback.ps1' -Output wasapi
```

It opens `build/player-diagnostics/Release/PandaClip.exe`, copies the two
existing clips into its own profile, enables bounded traces, and leaves play,
track selection and listening to the user. `-Output waveout` explicitly opens
the archived `build/player-output` deployment for the rejected trial only.
New production builds use WASAPI with no alternate renderer.
**No claim is made that this build eliminates the popping.**
