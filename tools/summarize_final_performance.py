"""Summarize owned GPU fixtures; never infer gameplay or physical listening results."""
import csv
import json
import statistics
from collections import defaultdict
from pathlib import Path

root=Path(__file__).resolve().parents[1]/'build/final-performance'
def metrics(path):
    values={}
    for line in path.read_text(encoding='utf-8').splitlines():
        if '=' in line:
            key,value=line.split('=',1)
            try:values[key]=float(value)
            except ValueError:values[key]=value
    return values

def slope(rows,key):
    x=[float(r['seconds']) for r in rows];y=[float(r[key]) for r in rows]
    if len(x)<2:return None
    a,b=statistics.mean(x),statistics.mean(y)
    variance=sum((v-a)**2 for v in x)
    return sum((i-a)*(j-b) for i,j in zip(x,y))/variance if variance else None

runs=[]
for path in sorted(root.glob('*/*/metrics.txt')):
    m=metrics(path)
    if not m:continue
    resources=list(csv.DictReader((path.parent/'resources.csv').open(encoding='utf-8')))
    gpu=list(csv.DictReader((path.parent/'gpu-counters.csv').open(encoding='utf-8-sig')))
    engines=defaultdict(list);dedicated=[];shared=[];threads=[]
    for r in gpu:
        value=float(r['value']);counter=r['counter'].lower()
        if counter=='utilization percentage':engines[r['instance']].append(value)
        if counter=='dedicated usage':dedicated.append(value)
        if counter=='shared usage':shared.append(value)
        threads.append(int(r['threads']))
    stable=[r for r in resources if float(r['seconds'])>=60]
    row={'run':path.parent.parent.name,'mode':path.parent.name,'metrics':m,
         'cpu_machine_percent':m['one_core_cpu_percent']/24,
         'output_mp4_bytes':sum(p.stat().st_size for p in path.parent.glob('*.mp4')),
         'gpu_per_engine':{k:{'mean_percent':statistics.mean(v),'max_percent':max(v)} for k,v in engines.items()},
         'dedicated_gpu_mean_bytes':statistics.mean(dedicated) if dedicated else None,
         'shared_gpu_mean_bytes':statistics.mean(shared) if shared else None,
         'threads_range':[min(threads),max(threads)] if threads else None,
         'handles_range':[min(int(r['handles']) for r in resources),max(int(r['handles']) for r in resources)] if resources else None,
         'private_slope_after_60_bytes_per_second':slope(stable,'private_bytes'),
         'post_60_private_range':[min(int(r['private_bytes']) for r in stable),max(int(r['private_bytes']) for r in stable)] if stable else None,
         'source_fixture_fps':m.get('source_fixture_frames',0)/m['run_seconds'],
         'composed_fps':m['composed_frames']/m['run_seconds'],
         'encoded_fps':m['encoded_frames']/m['run_seconds'],
         'peak_replay_charged_bytes':max(int(r['replay_bytes']) for r in resources) if resources else 0,
         'max_pending_exports':max(int(r['pending']) for r in resources) if resources else 0}
    runs.append(row)
out={'method':'Quiet generated textures. GPU intervals include scheduling/preemption. Per-engine percentages are not summed. I/O transfer counters are not physical disk-only bytes. CPU normalization uses measured 24 logical processors. No WGC/gameplay/UI/audio-hardware conclusions.','runs':runs}
(root/'summary.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
print('run | mode | one-core CPU % | encode ms | compose GPU ms | private end MiB | repeated / drops')
for r in runs:
    m=r['metrics'];print(f"{r['run']} | {r['mode']} | {m['one_core_cpu_percent']:.3f} | {m['encoder_wall_mean_ms']:.3f} | {m['compositor_gpu_mean_ms']:.3f} | {m['private_late_bytes']/2**20:.2f} | {m['encoder_repeated_composed_frames']:.0f} / {m['encoder_queue_drops']:.0f}")
