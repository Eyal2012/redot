# Playback audio repair — 2026-10-09

## Scope and evidence

The workspace was verified as `C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip`. A recoverable pre-edit source checkpoint is `build/checkpoints/audio-continuity-20261009-174637/source.zip`. No Git repository exists here.

The user clarified that the same MP4 sounds clean in the external Windows player and crackles only inside PandaClip. That points to playback rather than evidence of a damaged microphone recording. No capture converter, mixing, AAC encoder, recording/replay worker, appearance or preference change was made. No noise suppression, smoothing or extra buffering was introduced.

## Demonstrated defect

The installed [Qt 6.8.3 WASAPI output implementation](https://github.com/qt/qtmultimedia/blob/v6.8.3/src/multimedia/windows/qwindowsaudiosink.cpp) rounds its push-mode timer wait down to whole milliseconds. The timeout unconditionally declares an underrun and stops the WASAPI output client. It does not first establish that the hardware has consumed the queued frames. An early timer wake-up can therefore interrupt playback even while data remains queued. This is an output-buffer lifecycle bug; it does not establish a bad wireless microphone link or justify altering captured PCM.

`PandaClipPlaybackAudioProbe --sink` submits 1,024 zero-valued frames to the real Windows default output and checks residual WASAPI padding when Qt reports Idle. Volume is zero and no UI is opened. The actual endpoint was **Headphones (HyperX Cloud III Wireless)**, 48 kHz, float stereo, 8 bytes/frame:

| Runtime | Trials | Premature Idle/stops | Maximum queued bytes |
| --- | ---: | ---: | ---: |
| Stock Qt Release | 40 | 32 | 2,568 = 321 frames = 6.6875 ms |
| Repaired Qt Release | 40 | 0 | 0 |
| Repaired Qt Debug | 40 | 0 | 0 |

Reports: `build/audio-playback/baseline-sink`, `repaired-sink`, `debug-sink`. This proves the premature-stop defect and its repair on that output endpoint. It **does not prove the audible symptom on the user's exact clip is eliminated**: no human listening or audible playback was performed.

## Changes

- Round the output timer up, and on timeout read the actual remaining WASAPI play time. Re-arm while padding remains; declare an underrun only after the queue is empty. Ignore stale timer callbacks unless the sink is active and its client exists.
- Build only the Qt Multimedia core DLL against the same Qt 6.8.3 ABI. Keep the installed SDK and existing FFmpeg/QML plugins untouched. The production deployment copies the repaired DLL after windeployqt and also refreshes it when no executable relink is needed.
- Remove the redundant seek after changing an audio track in `MediaController::track`. [Qt's FFmpeg backend already preserves the playback clock](https://github.com/qt/qtmultimedia/blob/v6.8.3/src/plugins/multimedia/ffmpeg/qffmpegplaybackengine.cpp). The extra seek recreated/flushed the renderer a second time. Selecting the already active track is now a no-op.
- Add a verification-only silent playback/PCM probe and optional raw PCM output to the independent Media Foundation verifier. These are not shipping capture seams.

The complete patch is `docs/qt-audio-padding.patch`; rebuild script is `tools/build_qt_audio.py`. Both are deployed under `licenses/Qt/changes`, alongside corresponding pristine source archives/notices. Application runtime remains dynamically linked and replaceable. The patch is also applied to the matching extracted source under `build/qt-audio-runtime-source`.

## Decoded-sample and regression results

The probe uses the same QMediaPlayer/QAudioOutput/QVideoSink configuration as Media, with output muted. [QAudioBufferOutput](https://doc.qt.io/qt-6.8/qaudiobufferoutput.html) supplies decoded float PCM **before** output volume/mute. Packet PTS and sample counts are saved; this distinguishes decoder content from the Windows output path. `tools/analyze_playback_pcm.py` checks finite samples, clipping, adjacent sample steps and optional independent PCM comparison. Natural speech/music transients are not automatically labeled pops.

- A six-second generated microphone stem decoded to **288,768 stereo frames**, 48 kHz. Zero PTS gaps, zero clipped samples, peak 0.306865. Before/after decoded PCM is bit-identical: SHA256 `ccfbc7c38ab3f77cf1d0839e14d1d09fed23a591986e3c453c189aab42693d80`. Artifacts `baseline-mic`, `repaired-mic`.
- An existing 27.62-second MP4 was read without modifying it: `C:\Users\eyald\Videos\PandaClip\PandaClip_20261009_174215.mp4`. Its Mix/Game/Microphone AAC streams independently decode at 48 kHz stereo with contiguous/monotonic timestamps. The microphone stem contains **1,325,056 frames / 27.605333 s**, zero PTS gaps, zero clipped samples, peak 0.0628267, RMS 0.00135105. Qt float PCM vs independent Windows MF PCM16 differs by at most **0.0000152602**, approximately half a PCM16 quantization step. Maximum adjacent microphone sample step is 0.0491487; this alone is not evidence of a pop. No claim that this was the exact affected clip or that it contains audible speech. Artifacts `recent-clip-mic`, `recent-mf`, `recent-mf-report.txt`.
- Sixty-second two-tone PCM fixture: **2,880,000 frames**, zero PTS gaps/output errors, one output start/stop. Stock continuous playback did not reproduce a detectable underrun in this case. Do not confuse the targeted 40-trial reproduction with every playback session.
- Debug and Release shipping builds succeeded in `build/audio-playback`. Quiet Release UI checks passed with zero failures, including muted playback, RHI video frames, Mix/Game/Microphone track layout, paused seek, track-change position retention, volume/mute, all gears, recording, replay/export and frontend lifecycle. Artifact `qt-regression`. Actual shipping Release launches/terminates on an inactive owned desktop without SDK PATH, artifact `shipping-smoke`.
- Fresh generated GPU/PCM recordings and replay exports passed for **video-only, game-only, mic-only and both**: all **eight MP4s** decode independently. Actual NVENC/AAC/mux/replay code is exercised; fixtures do not use a real microphone, game WASAPI loopback, WGC gameplay or audible output. Artifacts `media-regression/*/*-decode.txt`.
- Floating-point meters passed all six existing component checks. Debug quiet UI also passed with zero failures (`debug-ui/result.txt`).
- **Ten-minute** two-tone PCM playback on the actual HyperX output, muted, delivered **28,800,000 frames / 600 s**, 7,032 packets, zero PTS gaps, zero output errors, zero noninitial Idle syncs and exactly one output start/stop. Decoded PCM is bit-identical to the known input converted to float: SHA256 `2d405d3c6045e514f91ebd5886c0bc4cbfcb99cff2d21442d50c94110525ba92`; no clipped or nonfinite samples. Artifacts `repaired-ten-minute`. This is synthetic PCM output stability; it is not a ten-minute spoken-microphone or gameplay test. MSVC builds and quiet fixtures ran concurrently for part of this test, but CPU/GPU load was not calibrated and no game-load performance claim is made.

All artifacts are under `build/audio-playback`; no global input, personal focus change, sound, microphone recording or control of personal PandaClip/GD sessions was used. Saved recordings and personal settings were preserved.

Final Release SHA256: `PandaClip.exe` = `8fa48333a4ce13ff5aba5d12df01b3213e4c1af212758c7d1d81da94d14847c6`; repaired `Qt6Multimedia.dll` = `8178d9ecef53686d4608f31a87be1675905f41bb1ef2949b02f52bd518aaa754`. Final deployed DLL matches the rebuilt core, differs from the stock SDK DLL, and passes a second 40-trial zero-premature-stop check (`final-release-sink`). Final SDK-free shipping launch passes (`final-shipping-smoke`). Checkpoint comparison confirms `main.cpp`, recording/audio-capture/replay sources, `desktop_backdrop.h`, QML and VS Code configuration are unchanged; the only production frontend source edit is the track-switch method described above.

## Reproduce quietly

```powershell
cmake -S . -B build\audio-playback -G "Visual Studio 17 2022" -A x64 -DPANDACLIP_BUILD_REPLAY_STRESS=ON -DPANDACLIP_BUILD_GLASS_LAB=OFF
cmake --build build\audio-playback --config Release --target PandaClip PandaClipPlaybackAudioProbe PandaClipReplaySyncVerify --parallel 4
& .\build\audio-playback\Release\PandaClipPlaybackAudioProbe.exe unused --sink .\build\audio-playback\new-sink-result
& .\build\audio-playback\Release\PandaClipPlaybackAudioProbe.exe 'path-to-clip.mp4' 2 .\build\audio-playback\new-mic-result
py -3 tools\analyze_playback_pcm.py .\build\audio-playback\new-mic-result
```

Use fresh output folders; audio ordinal 2 is Microphone only when Mix/Game/Microphone are all enabled. Qt ordinals, MP4 stream IDs and MF reader indexes are different: identify stems using labels/waveforms, never hard-code an assumed MF stream index. The probe needs the deployed Qt DLLs; no SDK PATH is needed alongside a completed app deployment. It mutes output and never opens a window. Raw analysis files are intentional local test evidence, not a new production recording mode.

Normal CMake/F5 builds run the cached runtime builder automatically. To rebuild it explicitly: `py -3 tools\build_qt_audio.py Release` or `Debug`. Python 3.12+, MSVC 2022 x64, CMake and Ninja are build dependencies. The Qt source archive is SHA-checked; build-only Vulkan headers are downloaded from the pinned official Khronos archive and SHA-checked if absent. Unicode workspace source tools use the existing Windows short-path alias to this same project. No files outside PandaClip are written by the builder.

## Short listening check still required

1. Finish active saves and close only your old PandaClip normally. Launch `build\audio-playback\Release\PandaClip.exe`.
2. Play the **same previously crackling MP4** in Media. Listen to Microphone and Mix during uninterrupted playback for 30–60 seconds. Pause/resume, seek and change Audio track; compare the external player using the same Windows output endpoint.
3. If still affected, supply that clip, selected track, output-device name and exact affected times. The fixed timer defect does not rule out other driver/scheduling issues under real load.
4. For capture quality separately, speak into the HyperX in mic-only and game+mic recordings, including replay enabled and moving GD under normal/high load. Listen to each stem externally and in PandaClip. A Windows Sound Recorder comparison is useful only if the external recording also crackles. Do not start a second recording application during performance comparison.

No physical microphone speech, actual GD load, wireless-link diagnosis, reference Sound Recorder recording or human listening test was performed. The user's clean external playback is the strongest current reason to preserve capture/encoding rather than changing it speculatively.
