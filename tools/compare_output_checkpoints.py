"""Compare decoded PCM, accepted renderer bytes and actual WASAPI payload.

Continuous playback only: commands/seek/track transitions need segmented source
matching rather than comparing their concatenation to one continuous track.
Creates reviewable WAVs as well as preserving exact float checkpoints.
"""
import array
import csv
import json
from pathlib import Path
import struct
import sys
import math

project = Path(__file__).resolve().parents[1]
folder = Path(sys.argv[1]).resolve()
if not folder.is_relative_to(project / "build"):
    sys.exit("Keep all output checkpoints under the ReDot project build directory")
rows = list(csv.DictReader((folder / "sink/writes.csv").open()))
clients = sorted({row["client"] for row in rows if row["kind"] == "render"}, key=int)
summary = dict(line.strip().split("=", 1) for line in (folder / "sink/summary.txt").read_text().splitlines())
report = {"trace_loss": int(summary["dropped_or_failed"]), "clients": []}

def floats(path):
    return array.array("f", path.read_bytes())

def wav(path, data, rate, channels):
    raw = data.tobytes()
    # IEEE float WAV, with fact sample count. No quantization of evidence.
    fmt = struct.pack("<HHIIHHH", 3, channels, rate, rate * channels * 4, channels * 4, 32, 0)
    body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt
    body += b"fact" + struct.pack("<II", 4, len(data) // channels)
    body += b"data" + struct.pack("<I", len(raw)) + raw
    path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)

decoded = floats(folder / "decoded.f32")
wav(folder / "A-decoded.wav", decoded, 48000, 2)
accepted_by_client = {}
offsets = {}
for client in clients:
    prefix = folder / "sink" / client
    fmt = dict(line.split("=", 1) for line in Path(str(prefix) + "-format.txt").read_text(encoding="utf-8").splitlines())
    if any(int(fmt[x]) != expected for x, expected in dict(input_rate=48000, input_channels=2, input_type=4, input_frame_bytes=8,
                                                       output_rate=48000, output_channels=2, output_type=4, output_frame_bytes=8).items()):
        sys.exit("Format differs: preserve PCM and convert explicitly before comparison: " + str(fmt))
    b = floats(Path(str(prefix) + "-B.pcm"))
    converted = floats(Path(str(prefix) + "-converted.pcm"))
    c = floats(Path(str(prefix) + "-C.pcm"))
    accepted_by_client[client] = b
    offsets[client] = 0
    selected = [r for r in rows if r["client"] == client and r["kind"] == "render"]
    index = 0
    expected = array.array("f")
    for r in selected:
        count = int(r["output_frames"]) * 2
        factor = struct.unpack("f", struct.pack("f", float(r["volume"])))[0]
        expected.extend(value * factor for value in converted[index:index + count])
        index += count
    rms = math.sqrt(sum(x*x for x in c) / len(c)) if c else 0
    pcm_gain_error = max((abs(x-y) for x,y in zip(c, expected)), default=0)
    stops = [r for r in rows if r["client"] == client and r["kind"] == "stop"]
    item = dict(client=client, format=fmt, accepted_frames=len(b)//2, submitted_frames=len(c)//2,
                decoded_frames=len(decoded)//2, A_equals_B=decoded == b, B_equals_converted=b == converted,
                C_equals_expected_gain=c == expected, maximum_gain_error=pcm_gain_error,
                malformed_write_counts=sum(int(r["accepted_bytes"]) % 8 != 0 or int(r["accepted_bytes"]) > int(r["requested_bytes"]) for r in selected),
                payload_counts_match=sum(int(r["accepted_bytes"]) for r in selected)==len(b)*4 and sum(int(r["output_frames"]) for r in selected)*2==len(c)==len(converted),
                short_writes=sum(int(r["accepted_bytes"]) < int(r["requested_bytes"]) for r in selected),
                zero_padding_writes=sum(int(r["padding_frames"]) == 0 for r in selected),
                errors=sum(int(r["hresult"]) != 0 for r in rows if r["client"] == client),
                clipped_samples=sum(abs(x) >= .9999 for x in c), nonfinite_samples=sum(not math.isfinite(x) for x in c),
                peak=max(map(abs,c), default=0), rms=rms,
                final_device_clock=int(stops[-1]["device_clock"]) if stops else None,
                final_padding_frames=int(stops[-1]["padding_frames"]) if stops else None,
                note="Session muted after submission; this is not physical listening verification.")
    for name, data in (("B-accepted",b),("converted",converted),("C-submitted",c)):
        wav(folder / f"{client}-{name}.wav", data, 48000, 2)
    report["clients"].append(item)
# Reconstruct renderer acceptance in event order, including deliberate seek and
# track changes. Match the public decoder sequence, not a continuous source's
# timestamps across commands. Discarded preroll would be reported as a mismatch.
ordered = array.array("f")
for r in rows:
    if r["kind"] != "render":
        continue
    client = r["client"]
    count = int(r["accepted_bytes"]) // 4
    offset = offsets[client]
    ordered.extend(accepted_by_client[client][offset:offset+count])
    offsets[client] += count
report["A_equals_ordered_accepted"] = decoded == ordered
(folder / "checkpoint-comparison.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps(report, indent=2))
if not clients or not report["A_equals_ordered_accepted"] or report["trace_loss"] or any(not item["payload_counts_match"] or not item["B_equals_converted"] or not item["C_equals_expected_gain"] or item["errors"] or item["malformed_write_counts"] or item["nonfinite_samples"] for item in report["clients"]):
    sys.exit(1)
if len(clients) == 1 and not report["clients"][0]["A_equals_B"]:
    sys.exit(1)
