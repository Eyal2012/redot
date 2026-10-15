"""Compare ordinary/replay cue timestamps from run_reliability_test artifacts.

Usage: py -3 tests/compare_sync_run.py build/milestone5/run-...
Run the MP4 verifier first; this reads its reports and the production trace log.
"""
import csv
import re
import statistics
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
def read_report(path):
    data = path.read_bytes()
    return data.decode("utf-16" if data.startswith(b"\xff\xfe") else "utf-8-sig")

log = (root / "local/PandaClip/logs/panda.log").read_text(encoding="utf-8")
saved = re.findall(r"Replay clip saved: (.*?);", log)
explicit = re.findall(r"Replay clip saved: (.*?);[^\n]*?common offset=(\d+) hns", log)
replay_offsets = {Path(path).name: int(offset) / 10000 for path, offset in explicit}
if explicit:
    if len(explicit) != len(saved):
        raise SystemExit("Some saved clips lack their own common offset; use one complete current-version trace.")
else:
    # Compatibility for the original ten-minute trace. Never guess which
    # different snapshot belongs to a parallel or failed legacy export.
    pending = []
    for line in log.splitlines():
        snapshot = re.search(r"Replay snapshot: common offset=(\d+) hns", line)
        if snapshot:
            pending.append(int(snapshot[1]) / 10000)
        clip = re.search(r"Replay clip saved: (.*?);", line)
        if clip:
            if not pending or len(set(pending)) != 1:
                raise SystemExit("Ambiguous legacy export offsets; repeat with the current saved-path offset logging.")
            replay_offsets[Path(clip[1]).name] = pending.pop(0)
        if "Replay clip export failed:" in line:
            raise SystemExit("Failed legacy exports cannot be paired reliably; repeat with current logging.")
    if pending:
        raise SystemExit("Legacy trace has unfinished/unmatched snapshots; comparison is incomplete.")
reports = list((root / "profile/Videos/PandaClip").glob("*.verify.txt"))
normal = [x for x in reports if "_Clip_" not in x.name]
if len(normal) != 1:
    raise SystemExit("Compare one uninterrupted session; recovery tests have multiple sessions.")
cue_pattern = re.compile(r"event (\d+): flash=([\d.]+)ms beep=([\d.]+)ms audio-minus-video=([-\d.]+)ms")
ordinary_report = read_report(normal[0])
if "decodeResult=PASS" not in ordinary_report or not normal[0].with_name(normal[0].name.removesuffix(".verify.txt")).is_file():
    raise SystemExit("Ordinary MP4 is missing or independent decoding did not pass.")
ordinary = [tuple(float(x) for x in match) for match in cue_pattern.findall(ordinary_report)]
if len(ordinary) < 2:
    raise SystemExit("Need at least two ordinary timed cue pairs for comparison.")
deltas = [x[3] for x in ordinary]
print(f"ordinary {len(deltas)} pairs: lag min/median/max {min(deltas):.3f}/{statistics.median(deltas):.3f}/{max(deltas):.3f} ms")
print(f"first/last lag {deltas[0]:.3f}/{deltas[-1]:.3f} ms; span {(ordinary[-1][1]-ordinary[0][1])/1000:.3f} s")
print("Endpoint change alone does not establish accumulating drift.")
times = [item[1] / 1000 for item in ordinary]
mean_time, mean_lag = statistics.mean(times), statistics.mean(deltas)
variance = sum((t - mean_time) ** 2 for t in times)
if variance:
    slope = sum((t - mean_time) * (lag - mean_lag) for t, lag in zip(times, deltas)) / variance
    print(f"least-squares offset change {slope * (times[-1] - times[0]):.3f} ms across {times[-1] - times[0]:.3f} s; source/capture scheduling can affect this fit")
pre_aac = [float(value) for value in re.findall(r"Sync trace pre-AAC cue: frame=\d+, pts=([\d.]+) ms", log)]
if pre_aac:
    cue_errors = [min(abs(item[2] - cue) for cue in pre_aac) for item in ordinary]
    print(f"decoded versus pre-AAC cue onset: max {max(cue_errors):.3f} ms (detector thresholds/windowing also contribute)")
maximum_difference = 0.0
matched_clips = 0
matched_events = 0
for report in reports:
    name = report.name.removesuffix(".verify.txt")
    if "_Clip_" not in name:
        continue
    if name not in replay_offsets:
        raise SystemExit(f"{name}: saved clip has no verified common timestamp offset.")
    if not report.with_name(name).is_file():
        raise SystemExit(f"{name}: MP4 is missing; a report alone is incomplete evidence.")
    decoded_report = read_report(report)
    if "decodeResult=PASS" not in decoded_report:
        raise SystemExit(f"{name}: independent decoding did not pass.")
    offset = replay_offsets[name]
    pairs = cue_pattern.findall(decoded_report)
    if len(pairs) < 2:
        raise SystemExit(f"{name}: need at least two complete cue pairs; decoding alone cannot establish synchronization.")
    differences = []
    for _, flash, beep, lag in pairs:
        flash, beep, lag = float(flash), float(beep), float(lag)
        nearest = min(ordinary, key=lambda item: abs(item[1] - flash - offset))
        if abs(nearest[1] - flash - offset) > 1:
            raise SystemExit(f"Unmatched flash in {name}; incorrect session or offset pairing.")
        if abs(nearest[2] - beep - offset) > 1:
            raise SystemExit(f"Unmatched audio timestamp in {name}; track rebasing changed synchronization.")
        differences.append(abs(nearest[3] - lag))
    matched_clips += 1
    matched_events += len(differences)
    maximum_difference = max(maximum_difference, max(differences, default=0))
    print(f"{name}: common offset {offset:.3f} ms, {len(differences)} matched events, max lag difference {max(differences,default=0):.4f} ms")
if not matched_clips or not matched_events:
    raise SystemExit("No replay events matched. Synchronization comparison has not been performed.")
if len(replay_offsets) != matched_clips:
    raise SystemExit("Some saved clips lack verification reports; compare every saved replay export.")
source = list(csv.DictReader((root / "beep.wav.source.csv").read_text().splitlines()))
jitter = [(float(row["paint_seconds"]) - float(row["scheduled_seconds"])) * 1000 for row in source if row["event"] == "paint"]
if jitter:
    print(f"source paint lateness min/median/max {min(jitter):.3f}/{statistics.median(jitter):.3f}/{max(jitter):.3f} ms; SoundPlayer speaker presentation is unmeasured")
print(f"comparisonResult={'PASS' if maximum_difference < 1 else 'FAIL'} (same-session rebasing only, not perceptual sync)")
sys.exit(0 if maximum_difference < 1 else 1)
