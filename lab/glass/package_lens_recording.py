"""Package actual captured Qt sample frames as a silent five-second recording.

Verification utility only. No generated frames, interpolation, or desktop input.
Requires Pillow; it is not a deployed Glass Lab dependency.
"""
import json
import sys
from pathlib import Path
from PIL import Image, ImageChops

folder = Path(sys.argv[1])
files = sorted((folder / "moving-lens-frames").glob("frame-*.png"))
if len(files) != 150:
    raise SystemExit(f"Expected 150 frames, found {len(files)}")
frames = [Image.open(file).convert("RGB") for file in files]
changed = sum(ImageChops.difference(a, b).getbbox() is not None
              for a, b in zip(frames, frames[1:]))
destination = folder / "moving-in-app-lens.gif"
# 10 ms GIF timing granularity: 30/30/40 ms pattern is exactly 30 fps on average.
durations = [30, 30, 40] * 50
frames[0].save(destination, save_all=True, append_images=frames[1:],
               duration=durations, loop=0, disposal=2, optimize=False)
decoded = Image.open(destination)
duration = 0
for index in range(decoded.n_frames):
    decoded.seek(index)
    decoded.load()
    duration += decoded.info.get("duration", 0)
report = {"frames": decoded.n_frames, "duration_ms": duration,
          "changed_source_frame_pairs": changed, "size": list(frames[0].size),
          "passed": decoded.n_frames == 150 and duration == 5000 and changed > 100,
          "scope": "recording of actual moving in-app Qt sample; no desktop transmission evidence"}
(folder / "lens-recording-report.json").write_text(json.dumps(report, indent=2))
print(json.dumps(report))
if not report["passed"]:
    raise SystemExit(1)
