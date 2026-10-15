"""Analyze probe PCM without playback; never classify natural transients as pops.

Usage: py -3 tools/analyze_playback_pcm.py probe-directory [independent.s16]
The optional reference is stereo PCM16/48 kHz from the MF verifier. Stream
ordinals may differ between readers: select the matching source by waveform.
"""
import array
import hashlib
import json
import math
from pathlib import Path
import sys

project = Path(__file__).resolve().parents[1]
folder = Path(sys.argv[1]).resolve()
if not folder.is_relative_to(project / "build"):
    sys.exit("Analysis artifacts must stay under the ReDot project build directory")
reference = open(sys.argv[2], "rb") if len(sys.argv) == 3 else None
samples = clipped = 0
peak = square = max_step = max_difference = 0.0
previous = [0.0, 0.0]
digest = hashlib.sha256()
with (folder / "decoded.f32").open("rb") as pcm:
    while raw := pcm.read(1024 * 1024):
        digest.update(raw)
        values = array.array("f", raw)
        comparison = None
        if reference:
            comparison = array.array("h", reference.read(len(values) * 2))
            if len(comparison) != len(values):
                sys.exit("Reference PCM length differs")
        for i, value in enumerate(values):
            if not math.isfinite(value):
                sys.exit("Nonfinite PCM sample")
            magnitude = abs(value)
            peak = max(peak, magnitude)
            square += value * value
            clipped += magnitude >= 0.9999
            channel = (samples + i) % 2
            if samples + i >= 2:
                max_step = max(max_step, abs(value - previous[channel]))
            previous[channel] = value
            if comparison is not None:
                max_difference = max(max_difference, abs(value - comparison[i] / 32768.0))
        samples += len(values)
if reference:
    if reference.read(1):
        sys.exit("Reference PCM has additional samples")
    reference.close()
result = dict(frames=samples // 2, sample_rate=48000, channels=2,
              duration_seconds=samples / 96000, peak=peak,
              rms=math.sqrt(square / samples) if samples else 0,
              clipped_samples=clipped, maximum_adjacent_sample_step=max_step,
              sha256=digest.hexdigest(),
              note="Adjacent steps include speech/music transients; this is not an audible-pop detector.")
if len(sys.argv) == 3:
    result["independent_pcm16_maximum_error"] = max_difference
(folder / "waveform-analysis.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
print(json.dumps(result, indent=2))
