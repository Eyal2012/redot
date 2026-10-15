"""Summarize manually collected PresentMon CSVs; this does not run a game.

Usage: py -3 tests/summarize_game_frame_times.py build/gd-benchmark/*.csv
Rows are grouped by process and swap chain, never merged across unrelated apps.
"""
import argparse
import csv
import glob
import math
import statistics
import re
from collections import defaultdict
from pathlib import PureWindowsPath, Path


def percentile(values, fraction):
    position = (len(values) - 1) * fraction
    low, high = math.floor(position), math.ceil(position)
    return values[low] + (values[high] - values[low]) * (position - low)


def metrics(samples):
    values = sorted(samples)
    count = max(1, math.ceil(len(values) * .01))
    return {"fps": 1000 / statistics.mean(values),
            "low": 1000 / statistics.mean(values[-count:]) if len(values) >= 1000 else None,
            "median": statistics.median(values), "p95": percentile(values, .95),
            "p99": percentile(values, .99)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csvs", nargs="+")
    parser.add_argument("--process", default="GeometryDash.exe")
    parser.add_argument("--column", default="MsBetweenPresents")
    args = parser.parse_args()
    paths = [Path(path) for pattern in args.csvs for path in glob.glob(pattern)]
    if not paths:
        parser.error("No CSV files matched. Collect manual game benchmarks first.")
    print(f"Timing column: {args.column}; percentiles use linear interpolation; each swap chain is separate.")
    print("1% low = reciprocal of mean slowest ceil(1%) intervals; withheld below 1000 samples.")
    print("File | Process / PID / swap chain | samples | average FPS | 1% low FPS | median ms | p95 ms | p99 ms")
    repeats = defaultdict(list)
    for path in paths:
        groups = defaultdict(list)
        invalid = 0
        with path.open(encoding="utf-8-sig", newline="") as file:
            rows = csv.DictReader(file)
            if not rows.fieldnames or args.column not in rows.fieldnames:
                parser.error(f"{path}: missing {args.column}; use the documented metrics version or select --column explicitly.")
            for row in rows:
                process = PureWindowsPath(row.get("Application", "")).name
                if process.lower() != args.process.lower():
                    continue
                try:
                    value = float(row[args.column])
                except (ValueError, TypeError):
                    invalid += 1
                    continue
                if not math.isfinite(value) or value <= 0:
                    invalid += 1
                    continue
                key = (process, row.get("ProcessID", "?"), row.get("SwapChainAddress", "?"))
                groups[key].append(value)
        if not groups:
            parser.error(f"{path}: no valid timing rows for {args.process}.")
        for key, samples in sorted(groups.items()):
            values = sorted(samples)
            m = metrics(values)
            low = f"{m['low']:.3f}" if m['low'] is not None else "N/A"
            print(f"{path.name} | {' / '.join(key)} | {len(values)} | {m['fps']:.3f} | {low} | {m['median']:.3f} | {m['p95']:.3f} | {m['p99']:.3f}")
            label = re.fullmatch(r"(.+)-run\d+", path.stem)
            if label and len(groups) == 1:
                repeats[label[1]].append(m)
        if invalid:
            print(f"{path.name}: excluded {invalid} missing/nonfinite/nonpositive measurements.")
    for label, runs in sorted(repeats.items()):
        if len(runs) < 2:
            continue
        fps = [m['fps'] for m in runs]
        print(f"Repeat variation {label}: {len(runs)} separate runs, average FPS range {min(fps):.3f}–{max(fps):.3f}, sample CV {statistics.stdev(fps)/statistics.mean(fps)*100:.2f}%; p99 range {min(m['p99'] for m in runs):.3f}–{max(m['p99'] for m in runs):.3f} ms. No intervals are pooled across runs.")
    print("These are game presentation intervals, not WGC cadence, encoded frame times, or a listening/smoothness judgment.")


if __name__ == "__main__":
    main()
