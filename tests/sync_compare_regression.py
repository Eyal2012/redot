"""Regression fixtures for the report comparator, not MP4 decoding tests.

Run from the project root: py -3 tests/sync_compare_regression.py
Generated placeholder media/reports stay under a new build/milestone5 folder.
"""
from datetime import datetime
from pathlib import Path
import subprocess
import sys

workspace = Path.cwd().resolve()
if not (workspace / "tests/compare_sync_run.py").is_file():
    raise SystemExit("Run from the PandaClip project root.")
root = workspace / "build/milestone5" / ("sync-fixtures-" + datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
root.mkdir(parents=True)
ordinary = "PandaClip_ordinary.mp4"
clip_a, clip_b = "PandaClip_Clip_a.mp4", "PandaClip_Clip_b.mp4"


def report(events):
    return "decodeResult=PASS\n" + "\n".join(
        f"event {i}: flash={flash}ms beep={flash + 50}ms audio-minus-video=50ms"
        for i, flash in enumerate(events, 1)
    )


def fixture(name):
    folder = root / name
    media = folder / "profile/Videos/PandaClip"
    media.mkdir(parents=True)
    log = folder / "local/PandaClip/logs/panda.log"
    log.parent.mkdir(parents=True)
    log.write_text(f"Replay clip saved: {clip_a}; common offset=40000000 hns\n", encoding="utf-8")
    (folder / "beep.wav.source.csv").write_text("event,paint_seconds,scheduled_seconds\npaint,3.001,3\n")
    for filename, events in ((ordinary, [10000, 18000, 26000]), (clip_a, [6000, 14000])):
        (media / filename).write_bytes(b"Comparator placeholder: NOT a playable media fixture")
        (media / (filename + ".verify.txt")).write_text(report(events), encoding="utf-8")
    return folder, media, log


def check(name, expected, mutate):
    folder, media, log = fixture(name)
    mutate(folder, media, log)
    result = subprocess.run([sys.executable, str(workspace / "tests/compare_sync_run.py"), str(folder)],
                            capture_output=True, text=True, timeout=30)
    (folder / "comparison.txt").write_text(result.stdout + result.stderr, encoding="utf-8")
    if (result.returncode == 0) != expected:
        raise SystemExit(f"FAIL: {name}: unexpected exit {result.returncode}\n{result.stdout}{result.stderr}")
    print(f"PASS: {name}: {'accepted' if expected else 'rejected'} as expected")


check("valid", True, lambda *_: None)
check("no-replay", False, lambda f, m, l: ((m / (clip_a + ".verify.txt")).unlink(), l.write_text("")))
check("no-replay-events", False, lambda f, m, l: (m / (clip_a + ".verify.txt")).write_text("decodeResult=PASS\n"))
check("ordinary-one-event", False, lambda f, m, l: (m / (ordinary + ".verify.txt")).write_text(report([10000])))
check("missing-media", False, lambda f, m, l: (m / clip_a).unlink())
check("failed-decode", False, lambda f, m, l: (m / (clip_a + ".verify.txt")).write_text(report([6000, 14000]).replace("PASS", "FAIL")))
check("independent-audio-reset", False, lambda f, m, l: (m / (clip_a + ".verify.txt")).write_text(report([6000, 14000]).replace("6050", "6000")))
check("missing-report", False, lambda f, m, l: (m / (clip_a + ".verify.txt")).unlink())
check("legacy-unambiguous", True, lambda f, m, l: l.write_text(f"Replay snapshot: common offset=40000000 hns\nReplay clip saved: {clip_a}; done\n"))
check("legacy-ambiguous", False, lambda f, m, l: l.write_text(f"Replay snapshot: common offset=40000000 hns\nReplay snapshot: common offset=120000000 hns\nReplay clip saved: {clip_a}; done\n"))
check("legacy-failure", False, lambda f, m, l: l.write_text(f"Replay snapshot: common offset=40000000 hns\nReplay clip export failed: fixture\nReplay clip saved: {clip_a}; done\n"))


def concurrent(f, m, log):
    (m / clip_b).write_bytes(b"Comparator placeholder, NOT decoded media")
    (m / (clip_b + ".verify.txt")).write_text(report([6000, 14000]))
    log.write_text(f"Replay snapshot: common offset=40000000 hns\nReplay snapshot: common offset=120000000 hns\n"
                   f"Replay clip saved: {clip_b}; common offset=120000000 hns\n"
                   f"Replay clip saved: {clip_a}; common offset=40000000 hns\n")


check("explicit-concurrent-reversed-completion", True, concurrent)
print(f"PASS: 12 comparator regression cases. These are simulated reports, not media decoding. Artifacts: {root}")
