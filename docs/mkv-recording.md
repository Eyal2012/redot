# MKV recording (default) and MP4 (alternative)

ReDot records ordinary (non-replay) sessions to **MKV by default** or MP4. **Instant-replay exports are always MP4**
and the replay buffer is unchanged. Both formats use the same NVENC H.264 stream and the same Windows AAC encoders;
only the container writer differs.

## Setting

Settings -> Recording -> **Recording format**: `MKV (recommended, recoverable after a crash)` or `MP4`.

| Situation | Result |
| --- | --- |
| Fresh install, or an existing `settings-v1.txt` without `recordFormat` | MKV |
| `recordFormat` = `1` (explicit MP4) | MP4, kept across launches |
| `recordFormat` missing a valid value (`banana`, out of range) | MKV |
| Changed while a recording is running | The running file keeps its format; the new choice applies to the next recording |

The key is `"recordFormat"` (`0` MKV, `1` MP4) in `%LOCALAPPDATA%\PandaClip\settings-v1.txt`; the header and every other
key are untouched. Output names are `ReDot_YYYYMMDD_HHMMSS.mkv` / `.mp4`; Media lists both.

## Container

* libwebm `mkvmuxer` / `mkvparser`, tag `libwebm-1.0.0.31` (commit `6745fd2`), BSD-3-Clause, vendored unmodified under
  `third_party/libwebm` (licence, patent grant and authors deploy to `licenses/libwebm`). libwebm writes DocType
  `matroska` automatically for H.264/AAC tracks.
* Video `V_MPEG4/ISO/AVC` (Annex-B converted to length-prefixed NALs, `avcC` built from the NVENC SPS/PPS), audio
  `A_AAC` with the Windows encoder's AudioSpecificConfig. Track order matches MP4: Video, Mix, Game, Microphone, then
  scene stems; track names (Mix / Game / Microphone / scene stem names) are stored in the file **and** in the existing
  `.tracks` sidecar so Media labels work for both formats.
* Timestamps are whole milliseconds (Matroska's resolution). Each packet is rounded independently, so the error never
  accumulates (<= 0.5 ms). The sync verifier allows 1.5 ms for `.mkv`.
* libwebm requires non-decreasing timestamps across all tracks while AAC packets lag video. `MatroskaWriter` therefore
  holds packets in a bounded interleaver (released once every track has data, or when 300 ms behind the newest packet),
  and raises a late timestamp to the previous one rather than failing (counted in `clampedTimestamps()`).

## Crash resistance: what is and is not protected

How it works:

1. The file is created `CREATE_NEW` as `<name>.partial.mkv` with read-only sharing. Another process can therefore
   tell an **active** recording (sharing violation) from an **orphan** (opens fine). On a clean stop the file is
   finalized (cues, duration, segment size), flushed, and renamed to `<name>.mkv`.
2. After every video or audio packet, the writer hands its bytes to `WriteFile` (no multi-packet backlog). A background
   thread calls `FlushFileBuffers` about once per second when new data exists. Matroska clusters close roughly every
   IDR (2 s) or 8 MiB.
3. The recorder never reports Saved unless finalize **and** the rename succeeded.

What survives:

* **Application crash / `TerminateProcess` / forced kill:** everything already passed to `WriteFile` survives in the
  OS file cache. Measured in the test below: no complete frame lost relative to the last progress report; the design
  bound is the interleaver window (<= 300 ms), libwebm's audio hold (< one video frame) and the frame being encoded.
* **Windows crash / power loss / drive removal:** data not yet flushed is lost. Expect up to about one second of
  footage plus whatever the drive's own volatile write cache holds (consumer SSDs may acknowledge flushes early).
  This was **not** physically tested and is not claimed to be fully protected.
* **Storage caching:** `FlushFileBuffers` is only requested once per second to keep the encode path free of disk stalls;
  folders on network shares or cloud-synced locations may behave differently. Disk-full or a write error fails the
  recording (reported, never "Saved") and keeps the footage as `.partial.mkv`.
* Recording failures keep `.partial.mkv` (only a file that never received a video packet is removed). MP4 recordings are
  unchanged: they are finalized only on stop and a crash leaves an unplayable `.partial.mp4`.

## Recovery

At startup ReDot makes one low-priority pass over the output folder (`mkvRecoverOrphans`):

* Only regular `*.partial.mkv` files that no process holds open for writing are considered; an active recording is
  never touched.
* A file is repaired only if it parses and contains a complete H.264 keyframe. Unreadable, empty or non-Matroska
  partials are left byte-for-byte untouched and are never listed in Media.
* The repair is a **new** finalized file `<name>_recovered.mkv` (cues and duration added) written to a `.recovering`
  staging file, validated (finalized, same video frame count, same tracks), then published. The original is renamed
  to `<name>.partial.mkv.recovered` (kept, never deleted; delete it yourself when you no longer want it).
* A recovered clip appears in Media with its track labels and a status line reports the count. Closing ReDot cancels a
  long repair at the next frame; the original is untouched and is retried next launch.
* Recovery temporarily needs free space about equal to the recovered footage. It runs only for the output folder that
  is set at startup.

## Media

`.mkv` and `.mp4` files are listed (type Recording / Replay / MKV / MP4), sorted and filtered together; `.partial.*`
files are never listed. Metadata and thumbnails use Windows Media Foundation, which opens H.264/AAC Matroska on
Windows 10/11; if it cannot open or time a Matroska file, a narrow fallback reads size, duration and track names
directly from the container (no thumbnail). Playback, seeking and audio-track selection use the existing Qt
Multimedia player and were verified on Matroska files (see Verification).

## Verification (all quiet, owned processes, no WGC/WASAPI/audio output)

* `ReDotMkvPrototype` — standalone libwebm proof (Media Foundation H.264 + AAC, 3 audio tracks, kill, recover).
* `ReDotMkvCrashTest` — production `RecordingPipeline`/NVENC with Mix + two scene stems: live-file-not-orphan check,
  incremental growth, hard kill of the owned child, orphan detection, recovery, original preserved, injected write
  failure keeps footage and is not reported Saved, hostile partial files untouched.
* `ReDotLifecycleTest ... mkv`, `ReDotSceneIntegration ... mkv` (MKV + simultaneous MP4 replay export, Mix-only,
  game-stems-only, mic-stems-only, video-only and 17-track layouts), `ReDotQuietMediaFixture ... mkv` (Mix/Game/
  Microphone), all decoded and checked by `ReDotReplaySyncVerify` (flash/beep offsets 0-1 ms).
* The Qt flow runs a second time with an MKV library: Media listing, size/sort/filter, playback, paused seek, track
  change, tray Exit finalization, format switch mid-recording and persistence. The shipping executable loads absent,
  invalid, MKV and MP4 profiles and saves the expected value without disturbing other settings.
* `ReDotMkvPerformance` — alternating MP4/MKV rounds through the production pipeline (see the delivery report).

## Not verified

Physical power loss / Windows crash / drive pull; MKV from real WGC capture plus real WASAPI game audio and
microphone (generated frames/PCM only); third-party players, Explorer thumbnails and editors; multi-hour recordings;
network and cloud-synced output folders; non-NVIDIA GPUs (NVENC is still required).
