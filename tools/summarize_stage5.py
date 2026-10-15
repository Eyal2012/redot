"""Summarize a Stage 5 benchmark manifest (tools/stage5_benchmark_matrix.ps1) or a single endurance run folder.

Everything is computed from raw artifacts (metrics.txt, resources.csv, gpu-counters.csv, timing-histograms.csv).
Usage:  py -3 tools/summarize_stage5.py <manifest.json>            -> matrix table (median of rounds) + per-run rows
        py -3 tools/summarize_stage5.py --run <run folder> [seconds-to-skip]  -> one run, time-series windows
"""
import csv
import json
import statistics
import sys
from pathlib import Path

LOGICAL_CPUS = 24  # i9-285K; used only to express process CPU as whole-machine percent


def read_metrics(folder: Path) -> dict:
    values = {}
    path = folder / "metrics.txt"
    if path.exists():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if "=" in line:
                key, _, value = line.partition("=")
                values[key.strip()] = value.strip()
    return values


def number(values: dict, key: str, default=0.0) -> float:
    try:
        return float(values.get(key, default))
    except ValueError:
        return float(default)


def slope(points):
    """Least-squares slope (y per x) of [(x, y)]."""
    n = len(points)
    if n < 3:
        return 0.0
    mx = sum(p[0] for p in points) / n
    my = sum(p[1] for p in points) / n
    den = sum((p[0] - mx) ** 2 for p in points)
    return sum((p[0] - mx) * (p[1] - my) for p in points) / den if den else 0.0


def resources(folder: Path, skip: float, seconds: float):
    rows = []
    path = folder / "resources.csv"
    if not path.exists():
        return rows
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row["phase"] == "running" and skip <= float(row["seconds"]) <= seconds:
                rows.append({k: (float(v) if k != "phase" else v) for k, v in row.items()})
    return rows


def gpu(folder: Path, skip: float):
    """Mean utilization per owned engine type (max over engine instances; instances of one type are not summed)."""
    path = folder / "gpu-counters.csv"
    result = {}
    if not path.exists():
        return result
    per_engine = {}
    dedicated, shared = [], []
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            instance = row["instance"]
            if row["counter"] == "utilization percentage" and "_engtype_" in instance:
                per_engine.setdefault(instance, []).append(float(row["value"]))
            elif row["counter"] == "dedicated usage":
                dedicated.append(float(row["value"]))
            elif row["counter"] == "shared usage":
                shared.append(float(row["value"]))
    by_type = {}
    for instance, values in per_engine.items():
        values = values[int(skip):] if len(values) > skip + 2 else values
        kind = instance.rsplit("_engtype_", 1)[1]
        by_type.setdefault(kind, []).append(statistics.fmean(values))
    for kind, means in by_type.items():
        result[kind] = max(means)
    if dedicated:
        result["dedicated_mib"] = statistics.fmean(dedicated) / 1048576
    if shared:
        result["shared_mib"] = statistics.fmean(shared) / 1048576
    return result


def percentile_from_histogram(folder: Path, column: str, q: float) -> float:
    path = folder / "timing-histograms.csv"
    if not path.exists():
        return 0.0
    buckets = []
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            buckets.append((float(row["bucket_low_us"]), float(row[column])))
    total = sum(count for _, count in buckets)
    if not total:
        return 0.0
    running = 0.0
    for low, count in buckets:
        running += count
        if running >= total * q:
            return (low + 50) / 1000.0  # upper edge of the 50 us bucket, in ms
    return buckets[-1][0] / 1000.0


def summarize_run(folder: Path, skip: float = 10.0) -> dict:
    m = read_metrics(folder)
    seconds = number(m, "run_seconds", 0)
    rows = resources(folder, skip, seconds)
    out = {"folder": str(folder), "mode": m.get("mode", "?"), "container": m.get("container", "?"), "tracks": m.get("audio_tracks", "?"), "fps": number(m, "configured_fps"), "seconds": seconds}
    if rows:
        window = rows[-1]["seconds"] - rows[0]["seconds"]
        cpu = (rows[-1]["cpu_ticks"] - rows[0]["cpu_ticks"]) / 1e7  # seconds of CPU
        out["cpu_one_core_percent"] = 100 * cpu / window if window else 0
        out["cpu_machine_percent"] = out["cpu_one_core_percent"] / LOGICAL_CPUS
        out["private_mib_start"] = rows[0]["private_bytes"] / 1048576
        out["private_mib_end"] = rows[-1]["private_bytes"] / 1048576
        out["private_mib_max"] = max(r["private_bytes"] for r in rows) / 1048576
        out["working_set_mib_max"] = max(r["working_set_bytes"] for r in rows) / 1048576
        out["private_slope_kib_per_s"] = slope([(r["seconds"], r["private_bytes"]) for r in rows]) / 1024
        out["handles_max"] = max(r["handles"] for r in rows)
        out["handles_slope_per_min"] = 60 * slope([(r["seconds"], r["handles"]) for r in rows])
        out["threads_max"] = max(r["threads"] for r in rows)
        out["write_mib_per_s"] = (rows[-1]["write_bytes"] - rows[0]["write_bytes"]) / window / 1048576 if window else 0
        out["video_queue_max"] = max(r["video_queue"] for r in rows)
        out["audio_queue_max"] = max(r["audio_queue"] for r in rows)
        out["replay_mib_max"] = max(r["replay_bytes"] for r in rows) / 1048576
    out["composed"] = number(m, "composed_frames")
    out["encoded"] = number(m, "encoded_frames")
    out["expected_frames"] = seconds * number(m, "configured_fps")
    out["compose_late_slots"] = number(m, "compositor_late_slots")
    out["repeated"] = number(m, "encoder_repeated_composed_frames")
    out["queue_drops"] = number(m, "encoder_queue_drops")
    out["producer_drops"] = number(m, "producer_drops")
    out["pool_drops"] = number(m, "compositor_pool_drops")
    out["slow_encode_frames"] = number(m, "slow_encode_frames")
    out["encode_mean_ms"] = number(m, "encoder_wall_mean_ms")
    out["encode_p99_ms"] = percentile_from_histogram(folder, "encode_count", 0.99)
    out["encode_p999_ms"] = percentile_from_histogram(folder, "encode_count", 0.999)
    out["compose_p99_ms"] = percentile_from_histogram(folder, "compose_count", 0.99)
    out["audio_late_frames"] = number(m, "audio_late_frames")
    out["audio_missing_frames"] = number(m, "audio_timeline_missing")
    out["audio_overwrites"] = number(m, "audio_timeline_overwrites")
    out["audio_queue_drops"] = number(m, "audio_queue_drops")
    out["file_mib"] = number(m, "ordinary_file_bytes") / 1048576
    out["exports"] = number(m, "exports_completed")
    out["export_failures"] = number(m, "export_failures")
    out["export_max_ms"] = number(m, "export_max_ms")
    out["stop_finalize_ms"] = number(m, "stop_finalize_ms")
    out["gpu"] = gpu(folder, skip)
    return out


def median(values):
    values = [v for v in values if v is not None]
    return statistics.median(values) if values else 0.0


def matrix(manifest_path: Path):
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    if isinstance(manifest, dict):
        manifest = [manifest]
    runs = []
    for entry in manifest:
        summary = summarize_run(Path(entry["folder"]))
        summary["name"] = entry["name"]
        summary["round"] = entry["round"]
        summary["ambient"] = entry.get("ambient", {})
        summary["sync"] = entry.get("sync", [])
        runs.append(summary)
    names = []
    for run in runs:
        if run["name"] not in names:
            names.append(run["name"])
    lines = []
    header = "| config | rounds | CPU % 1 core (med) | CPU % machine | GPU 3D % | GPU enc % | priv MiB end | priv slope KiB/s | write MiB/s | file MiB | enc mean/p99/p99.9 ms | compose p99 ms | composed/expected | late slots | repeated | q-drops | audio late/missing | sync offset |"
    lines.append(header)
    lines.append("|" + "---|" * (header.count("|") - 1))
    for name in names:
        group = [r for r in runs if r["name"] == name]
        g3 = median([r["gpu"].get("3d") for r in group])
        ge = median([max(r["gpu"].get("videoencode", 0), 0) for r in group])
        sync = "; ".join(sorted({s["offset"] for r in group for s in r["sync"] if s.get("offset")})) or "-"
        lines.append("| {} | {} | {:.2f} | {:.3f} | {:.2f} | {:.2f} | {:.0f} | {:.1f} | {:.2f} | {:.1f} | {:.2f}/{:.2f}/{:.2f} | {:.2f} | {:.0f}/{:.0f} | {:.1f} | {:.1f} | {:.1f} | {:.0f}/{:.0f} | {} |".format(
            name, len(group),
            median([r.get("cpu_one_core_percent") for r in group]), median([r.get("cpu_machine_percent") for r in group]), g3, ge,
            median([r.get("private_mib_end") for r in group]), median([r.get("private_slope_kib_per_s") for r in group]),
            median([r.get("write_mib_per_s") for r in group]), median([r["file_mib"] for r in group]),
            median([r["encode_mean_ms"] for r in group]), median([r["encode_p99_ms"] for r in group]), median([r["encode_p999_ms"] for r in group]),
            median([r["compose_p99_ms"] for r in group]), median([r["composed"] for r in group]), median([r["expected_frames"] for r in group]),
            median([r["compose_late_slots"] for r in group]), median([r["repeated"] for r in group]), median([r["queue_drops"] for r in group]),
            median([r["audio_late_frames"] for r in group]), median([r["audio_missing_frames"] for r in group]), sync))
    print("\n".join(lines))
    print()
    print("Ambient load immediately before each run (other applications were running):")
    for run in runs:
        a = run["ambient"]
        print("  {} r{}: CPU {}%  GPU3D {}%  GPUenc {}%  free RAM {} GB".format(run["name"], run["round"], a.get("cpu_percent"), a.get("gpu_3d_percent"), a.get("gpu_videoencode_percent"), a.get("free_ram_gb")))
    (manifest_path.parent / "summary.json").write_text(json.dumps(runs, indent=2), encoding="utf-8")
    (manifest_path.parent / "summary.md").write_text("\n".join(lines), encoding="utf-8")


def windows(folder: Path, skip: float, window_s: float):
    """Time series for endurance runs: per-window means so growth can be separated from warm-up."""
    m = read_metrics(folder)
    seconds = number(m, "run_seconds", 0)
    rows = resources(folder, 0, seconds)
    print("window_start_s, private_MiB_mean, private_MiB_max, working_set_MiB, handles_max, threads_max, cpu_core_%, write_MiB_s, video_q_max, replay_MiB, composed_fps, encoded_fps, repeated, exports")
    start = 0.0
    while rows and start < rows[-1]["seconds"]:
        part = [r for r in rows if start <= r["seconds"] < start + window_s]
        if len(part) >= 2:
            span = part[-1]["seconds"] - part[0]["seconds"]
            print("{:.0f}, {:.1f}, {:.1f}, {:.1f}, {:.0f}, {:.0f}, {:.2f}, {:.3f}, {:.0f}, {:.1f}, {:.2f}, {:.2f}, {:.0f}, {:.0f}".format(
                start, statistics.fmean(r["private_bytes"] for r in part) / 1048576, max(r["private_bytes"] for r in part) / 1048576,
                statistics.fmean(r["working_set_bytes"] for r in part) / 1048576, max(r["handles"] for r in part), max(r["threads"] for r in part),
                100 * (part[-1]["cpu_ticks"] - part[0]["cpu_ticks"]) / 1e7 / span if span else 0,
                (part[-1]["write_bytes"] - part[0]["write_bytes"]) / span / 1048576 if span else 0, max(r["video_queue"] for r in part),
                max(r["replay_bytes"] for r in part) / 1048576, (part[-1]["composed"] - part[0]["composed"]) / span if span else 0,
                (part[-1]["encoded"] - part[0]["encoded"]) / span if span else 0, part[-1]["repeated"], part[-1]["exports"]))
        start += window_s
    steady = [r for r in rows if r["seconds"] >= skip]
    if len(steady) > 10:
        print("steady-state (t >= {:.0f}s): private slope {:.2f} KiB/s ({:.1f} MiB/h); working-set slope {:.2f} KiB/s; handle slope {:.2f}/h; thread slope {:.2f}/h".format(
            skip, slope([(r["seconds"], r["private_bytes"]) for r in steady]) / 1024, slope([(r["seconds"], r["private_bytes"]) for r in steady]) * 3600 / 1048576,
            slope([(r["seconds"], r["working_set_bytes"]) for r in steady]) / 1024, slope([(r["seconds"], r["handles"]) for r in steady]) * 3600,
            slope([(r["seconds"], r["threads"]) for r in steady]) * 3600))


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "--run":
        folder = Path(sys.argv[2])
        skip = float(sys.argv[3]) if len(sys.argv) > 3 else 600.0
        summary = summarize_run(folder)
        print(json.dumps(summary, indent=2))
        windows(folder, skip, 600.0)
    elif len(sys.argv) == 2:
        matrix(Path(sys.argv[1]))
    else:
        raise SystemExit(__doc__)
