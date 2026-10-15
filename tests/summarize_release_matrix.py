"""Summarize actual artifacts from run_release_matrix.ps1's redirected transcript."""
import argparse
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("transcript", type=Path)
args = parser.parse_args()
workspace = Path.cwd().resolve()
transcript = args.transcript.resolve()
if not transcript.is_relative_to(workspace):
    raise SystemExit("Keep artifacts inside the ReDot workspace.")
data = transcript.read_bytes()
text = data.decode("utf-16" if data.startswith(b"\xff\xfe") else "utf-8-sig")
runs = re.findall(r"^Started (Replay|Recording|Both); preview hidden=(True|False);.*?artifacts=(.*?)\s*$", text, re.MULTILINE)
if len(runs) != 6 or len({(mode, hidden) for mode, hidden, _ in runs}) != 6:
    raise SystemExit("Need exactly six completed mode/preview cells.")
if text.count("Finished; artifacts:") != 6:
    raise SystemExit("A matrix cell did not finish; inspect its transcript.")
rows = []
for mode, hidden, path in runs:
    run = Path(path).resolve()
    if not run.is_relative_to(workspace):
        raise SystemExit("Run path is outside this project.")
    folder = run / "profile/Videos/PandaClip"
    reports = sorted(folder.glob("PandaClip_Diagnostics_*.txt"), key=lambda p: p.stat().st_mtime)
    if not reports:
        raise SystemExit(f"Missing live diagnostic report: {run}")
    report = reports[0].read_text(encoding="utf-8")
    state, preview = re.search(r"State: (.*?); preview rendering: (\w+)", report).groups()
    assert preview == ("hidden" if hidden == "True" else "visible"), run
    available = re.search(r"WGC available before GPU copy: (\d+); cadence: ([\d.]+) fps; largest gap: ([\d.]+) ms; GPU pool drops: (\d+)", report).groups()
    accepted, fps = re.search(r"WGC frames: (\d+); cadence: ([\d.]+)", report).groups()
    cfr = re.search(r"CFR frames: (\d+); repeated: (\d+); late/replaced: (\d+); dropped: (\d+)", report).groups()
    avg, maximum, slow = re.search(r"Encoder\+GPU\+mux call avg/max: ([\d.]+)/([\d.]+) ms; calls >16.67 ms: (\d+)", report).groups()
    private, working = re.search(r"Process private bytes/working set: ([\d.]+)/([\d.]+)", report).groups()
    queues = re.search(r"Queue video now/high/cap: (\d+)/(\d+)/(\d+); audio: (\d+)/(\d+)/(\d+)", report).groups()
    audio_loss = int(re.search(r"Audio queue loss: (\d+);", report)[1])
    logging = re.search(r"Diagnostic log queue pending/cap: (\d+)/256; dropped messages/write errors: (\d+)/(\d+)", report)
    log = (run / "local/PandaClip/logs/panda.log").read_text(encoding="utf-8")
    times = [int(x) for x in re.findall(r"Replay export completed: success=1, elapsed=(\d+) ms", log)]
    clips = list(folder.glob("*.mp4"))
    assert clips, run
    for clip in clips:
        data = Path(str(clip) + ".verify.txt").read_bytes()
        decoded = data.decode("utf-16" if data.startswith(b"\xff\xfe") else "utf-8-sig")
        assert "decodeResult=PASS" in decoded, clip
        assert "1920x1080 @ 60/1" in decoded and "tracks: video=1 audio=2" in decoded, clip
    rows.append(dict(run=run.name, mode=state, preview=preview, fps=float(fps),
                     available=int(available[0]), accepted=int(accepted), pool_drops=int(available[3]),
                     frames=int(cfr[0]), repeats=int(cfr[1]), late=int(cfr[2]), queue_drops=int(cfr[3]),
                     avg=float(avg), maximum=float(maximum), slow_calls=int(slow),
                     private=float(private), working=float(working), video_high=int(queues[1]),
                     audio_high=int(queues[4]), audio_loss=audio_loss,
                     log_pending=int(logging[1]) if logging else None,
                     log_dropped=int(logging[2]) if logging else None,
                     log_errors=int(logging[3]) if logging else None,
                     exports_ms=times, decoded_files=len(clips)))

table = "| Mode | Preview | Source FPS | CFR frames / repeats / late | GPU + encoder + mux avg/max (ms) | Private RAM (MiB) |\n"
table += "| --- | --- | ---: | ---: | ---: | ---: |\n"
for row in rows:
    table += f"| {row['mode']} | {row['preview']} | {row['fps']:.3f} | {row['frames']} / {row['repeats']} / {row['late']} | {row['avg']:.3f} / {row['maximum']:.3f} | {row['private']:.3f} |\n"
transcript.with_suffix(".md").write_text(table, encoding="utf-8")
transcript.with_suffix(".json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
print(table)
print("Decoded files:", sum(row["decoded_files"] for row in rows))
print("Successful export ms:", [t for row in rows for t in row["exports_ms"]])
print("Video/audio queue high water:", [(row["video_high"], row["audio_high"]) for row in rows])
print("Pool/queue drops, slow calls:", [(row["pool_drops"], row["queue_drops"], row["slow_calls"]) for row in rows])
print("Audio loss, log dropped/write errors:", [(row["audio_loss"], row["log_dropped"], row["log_errors"]) for row in rows])
