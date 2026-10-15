"""Compare Qt float decoding with an independently decoded MF s16 track.

No offset search, resampling or filtering hides differences. Both inputs must be
48 kHz interleaved stereo; the verifier and playback probe enforce those formats.
Decoder s16 quantization/clamping is applied only to the comparison reference.
"""
import argparse
import array
import json
import math
from pathlib import Path
import wave

parser = argparse.ArgumentParser()
parser.add_argument('qt_pcm', type=Path)
parser.add_argument('mf_pcm', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
project = Path(__file__).resolve().parents[1]
for path in (args.qt_pcm,args.mf_pcm,args.output):
    if not path.resolve().is_relative_to(project/'build'):
        parser.error('Use project-local build evidence only')
if args.output.exists(): parser.error('Use a fresh report filename')
a = array.array('f',args.qt_pcm.read_bytes())
b = array.array('h',args.mf_pcm.read_bytes())
n = min(len(a),len(b))
if not n or n % 2: parser.error('Empty or misaligned PCM')
aa = bb = ab = square = largest = 0.0
different = clipped = 0
for x, raw in zip(a,b):
    y = raw/32768
    delta = max(-1,min(32767/32768,x))-y
    aa += x*x; bb += y*y; ab += x*y; square += delta*delta
    largest = max(largest,abs(delta))
    different += abs(delta) > 1/32768
    clipped += abs(x) >= 1
report = dict(qt_frames=len(a)//2,mf_frames=len(b)//2,same_length=len(a)==len(b),
              correlation=ab/math.sqrt(aa*bb) if aa*bb else None,
              rms_difference=math.sqrt(square/n),max_difference=largest,
              samples_differing_over_one_s16_step=different,qt_samples_outside_unit_range=clipped,
              note='Quantization-aware decoder comparison; not audible/device-output verification.')
args.output.write_text(json.dumps(report,indent=2))
with wave.open(str(args.output.with_suffix('.wav')),'wb') as file:
    file.setnchannels(2);file.setsampwidth(2);file.setframerate(48000);file.writeframes(b.tobytes())
print(json.dumps(report,indent=2))
