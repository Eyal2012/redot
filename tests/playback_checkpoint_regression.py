"""Fault-test sample accounting; never open a speaker or user media file."""
import array
import csv
from datetime import datetime
import json
import os
from pathlib import Path
import subprocess
import sys

project = Path(__file__).resolve().parents[1]
root = project / 'build/player-checkpoint-tests' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
root.mkdir(parents=True)
samples = array.array('f', ((i % 97 - 48) / 256 for i in range(4096)))
columns = 'kind,client,qpc_ticks,requested_bytes,accepted_bytes,output_frames,padding_frames,device_clock,volume,hresult'.split(',')
cases = ['clean', 'missing', 'duplicate', 'reordered', 'planar', 'gain-corruption',
         'conversion-corruption', 'count-mismatch', 'misaligned', 'short-write',
         'underrun', 'trace-loss', 'unfinalized', 'track-order', 'track-clean']
results = []
for case in cases:
    folder = root / case
    sink = folder / 'sink'
    sink.mkdir(parents=True)
    a, b = samples[:], samples[:]
    if case == 'missing': del b[512:514]
    if case == 'duplicate': b[512:512] = b[512:514]
    if case == 'reordered': b[512:516] = b[514:516] + b[512:514]
    if case == 'planar': b = array.array('f', b[::2] + b[1::2])
    converted = b[:]
    if case == 'conversion-corruption': converted[700] += .25
    c = converted[:]
    if case == 'gain-corruption': c[700] += .25
    (folder / 'decoded.f32').write_bytes(a.tobytes())
    if case != 'unfinalized': (sink / 'summary.txt').write_text('dropped_or_failed=' + ('1' if case == 'trace-loss' else '0') + '\n')
    fmt = dict(input_rate=48000,input_channels=2,input_type=4,input_frame_bytes=8,
               output_rate=48000,output_channels=2,output_type=4,output_frame_bytes=8,renderer='fixture')
    rows = []
    groups = [(b,converted,c)]
    if case.startswith('track-'):
        groups = [(b[:2048],converted[:2048],c[:2048]),(b[2048:],converted[2048:],c[2048:])]
    for client, (input_pcm, converted_pcm, output_pcm) in enumerate(groups, 1):
        for suffix, data in [('B',input_pcm),('converted',converted_pcm),('C',output_pcm)]:
            (sink / f'{client}-{suffix}.pcm').write_bytes(data.tobytes())
        (sink / f'{client}-format.txt').write_text(''.join(f'{k}={v}\n' for k,v in fmt.items()))
        accepted = len(input_pcm)*4
        if case == 'count-mismatch': accepted -= 8
        if case == 'misaligned': accepted -= 1
        requested = accepted + (8 if case == 'short-write' else 0)
        rows.append(['render',client,client*100,requested,accepted,len(output_pcm)//2,1024,0,1,0])
    if case == 'track-order': rows.reverse()
    if case == 'underrun': rows.append(['underrun',1,300,0,0,0,0,0,0,-1])
    with (sink / 'writes.csv').open('w', newline='') as file:
        writer = csv.writer(file); writer.writerow(columns); writer.writerows(rows)
    process = subprocess.run([sys.executable,str(project/'tools/compare_output_checkpoints.py'),str(folder)],capture_output=True,text=True,encoding='utf-8',errors='replace',
                             env={**os.environ,'PYTHONIOENCODING':'utf-8'})
    (folder/'comparison.log').write_text(process.stdout+process.stderr,encoding='utf-8')
    expected = case in ('clean','short-write','track-clean')
    passed = (process.returncode == 0) == expected
    if case == 'short-write' and passed:
        passed = json.loads((folder/'checkpoint-comparison.json').read_text())['clients'][0]['short_writes'] == 1
    results.append(dict(case=case,passed=passed,returncode=process.returncode))
    print(('PASS' if passed else 'FAIL') + ': ' + case)
(root/'results.json').write_text(json.dumps(results,indent=2))
print('Artifacts: '+str(root))
sys.exit(0 if all(r['passed'] for r in results) else 1)
