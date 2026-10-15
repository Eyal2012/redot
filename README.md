# ReDot

ReDot is a Windows game and screen recorder with instant replay. It captures a selected game or application, lets you arrange capture sources into scenes, and records or saves recent footage without interrupting the session.

## Recording and replay

Ordinary recordings use MKV by default. The file is written as recording progresses, and ReDot can recover an interrupted recording at the next launch while preserving the original partial file. MP4 is also available for ordinary recordings. Instant replay exports MP4 clips.

Replay keeps a bounded history of compressed video and audio in memory. Saving a clip writes that existing history without encoding the video again. The clip begins at a valid video keyframe, so its duration can vary slightly with keyframe and audio packet boundaries.

## Scenes and capture

A scene combines supported sources such as game or window capture, display capture, camera, images, and text. Sources can be positioned and arranged together. The selected scene is used for the preview, recording, and replay.

ReDot uses Windows Graphics Capture and Direct3D 11 for capture and composition, and NVIDIA NVENC for H.264 video encoding.

## Audio

Game or application audio and microphone audio are captured separately. Recordings include a mixed audio track and separate tracks for enabled sources. Whole-system desktop audio is available as an explicit source; ReDot does not silently switch to it when application audio is unavailable.

Audio is encoded as AAC. Replay and recording use the same configured capture sources, and source synchronization adjustments are optional.

## Interface

The app includes Capture, Scenes, Media, Settings, and Diagnostics views. Quick Settings provide separate controls for recording, replay, clip saving, game audio, and microphone input. The Windows taskbar icon indicates idle, replay, or recording state.

ReDot does not start recording or replay automatically when launched. The updater is a separate Windows executable; it verifies signed release packages before installing them.

## Requirements

- Windows
- An NVIDIA GPU and driver that support NVENC H.264 encoding
- Qt 6.8.3 powers the dynamically linked interface; the required runtime components are deployed with the app.

## Existing data

For compatibility, ReDot continues to use `%LOCALAPPDATA%\PandaClip\` for settings, scenes, layout, and logs, and `Videos\PandaClip` as the default recording folder. It recognizes existing `PandaClip_` clips as well as `ReDot_` clips.

## More information

- [Recording formats and recovery](docs/mkv-recording.md)
- [Updater behavior](docs/updater.md)
- [Qt and third-party notices](docs/QT-NOTICE.txt)

