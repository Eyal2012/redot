"""Read owned resource measurements, separating live operation from teardown.

No hardware/gameplay inference. Run after tests finish, or read partial CSVs
explicitly as incomplete evidence. Optional plots use build/analysis-python.
"""
import argparse
import csv
import json
import math
import statistics
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
BUILD = PROJECT / 'build'

def rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return [r for r in csv.DictReader(stream) if None not in r and all(v is not None for v in r.values())]

def fit(data, key, time='seconds'):
    if len(data) < 3:
        return None
    x = [float(r[time]) for r in data]
    y = [float(r[key]) for r in data]
    a, b = statistics.mean(x), statistics.mean(y)
    variance = sum((v-a)**2 for v in x)
    return sum((i-a)*(j-b) for i,j in zip(x,y))/variance if variance else None

def quantile(v, p):
    v=sorted(v)
    if not v:return None
    position=(len(v)-1)*p; i=math.floor(position); j=math.ceil(position)
    return v[i]+(v[j]-v[i])*(position-i)

def parse_metrics(path):
    result={}
    if path.exists():
        for line in path.read_text().splitlines():
            if '=' not in line:continue
            key,value=line.split('=',1)
            try:result[key]=float(value)
            except ValueError:result[key]=value
    return result

def summarize(path):
    data=rows(path)
    if not data:raise ValueError('Empty measurements: '+str(path))
    active=[r for r in data if r.get('phase','running')=='running']
    # A shipping idle CSV records actual visibility states instead of running.
    if not active:active=data
    last=float(active[-1]['seconds'])
    warm=[r for r in active if float(r['seconds'])>=max(60,last*.2)]
    tail=[r for r in active if float(r['seconds'])>=max(60,last-600)]
    warm_slope=fit(warm,'private_bytes');tail_slope=fit(tail,'private_bytes')
    result={'file':str(path.relative_to(PROJECT)), 'last_running_second':last,
            'running_private_first_MiB':int(active[0]['private_bytes'])/2**20,
            'running_private_last_MiB':int(active[-1]['private_bytes'])/2**20,
            'running_private_peak_MiB':max(int(r['private_bytes']) for r in active)/2**20,
            'private_slope_post_warmup_KiB_s':warm_slope/1024 if warm_slope is not None else None,
            'private_slope_last_600_seconds_KiB_s':tail_slope/1024 if tail_slope is not None else None,
            'private_tail_range_MiB':[min(int(r['private_bytes']) for r in tail)/2**20,max(int(r['private_bytes']) for r in tail)/2**20] if tail else None,
            'phases':{}}
    for key in ('working_set_bytes','commit_bytes','handles','threads','gpu_owned_bytes',
                'replay_bytes','replay_rolling_bytes','replay_retained_bytes','pending','video_queue','audio_queue'):
        if key in active[0]:result[key+'_running_range']=[min(int(r[key]) for r in active),max(int(r[key]) for r in active)]
    for phase in dict.fromkeys(r.get('phase','running') for r in data):
        subset=[r for r in data if r.get('phase','running')==phase]
        result['phases'][phase]={k:subset[-1][k] for k in ('seconds','private_bytes','working_set_bytes','commit_bytes','handles','threads','gpu_owned_bytes','replay_bytes') if k in subset[-1]}
    result['metrics']=parse_metrics(path.parent/'metrics.txt')
    if 'ping_ms' in data[0]:
        pings=[float(r['ping_ms']) for r in data]
        result['ui_ping_ms']={'p50':quantile(pings,.5),'p95':quantile(pings,.95),'p99':quantile(pings,.99),'max':max(pings),'failed':sum(r['ping_success']!='1' for r in data)}
    # Sampled Windows counters are per engine, not additive total GPU usage.
    gpu=path.parent/'gpu-counters.csv'
    if gpu.exists():
        engines={}
        for r in rows(gpu):
            if r['counter'].lower()=='utilization percentage':engines.setdefault(r['instance'],[]).append(float(r['value']))
        result['gpu_engines_including_shutdown_tail']={k:{'mean_percent':statistics.mean(v),'p95_percent':quantile(v,.95),'max_percent':max(v)} for k,v in engines.items()}
    histogram=path.parent/'timing-histograms.csv'
    if histogram.exists():
        bins=rows(histogram)
        for key in ('encode_count','compose_count'):
            total=sum(int(r[key]) for r in bins)
            percentiles={}
            for p in (.5,.95,.99):
                cumulative=0
                for r in bins:
                    cumulative+=int(r[key])
                    if total and cumulative>=total*p:
                        lower=int(r['bucket_low_us']);percentiles[str(p)]=[lower/1000,(lower+50)/1000] if lower<20000 else [20,None];break
            result[key+'_ms_bin_bounds']=percentiles
    return result,data

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csvs',type=Path,nargs='+');parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve()
    if not out.is_relative_to(BUILD):parser.error('Output must stay under build')
    out.mkdir(parents=True,exist_ok=True);results=[];plots=[]
    for path in args.csvs:
        path=path.resolve()
        if not path.is_relative_to(BUILD):parser.error('Read owned build evidence only')
        result,data=summarize(path);results.append(result);plots.append((path.parent.parent.name+'/'+path.parent.name,data))
    (out/'memory-summary.json').write_text(json.dumps({'method':'Real wall-time CSV measurements; live and shutdown phases separated. Slopes are observational, not leak attribution. GPU counters include post-stop tail. Concurrent workloads are not controlled performance comparisons.','runs':results},indent=2))
    sys.path.insert(0,str(BUILD/'analysis-python'))
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        print('JSON written; matplotlib unavailable for plots')
    else:
        fig,axes=plt.subplots(3,1,figsize=(12,10),sharex=True)
        for label,data in plots:
            times=[float(r['seconds'])/60 for r in data]
            axes[0].plot(times,[int(r['private_bytes'])/2**20 for r in data],label=label,lw=1)
            if 'replay_bytes' in data[0]:axes[1].plot(times,[int(r['replay_bytes'])/2**20 for r in data],label=label,lw=1)
            axes[2].plot(times,[int(r['handles']) for r in data],label=label,lw=1)
        for ax,title in zip(axes,('Process private bytes (MiB)','Charged compressed replay + snapshots (MiB)','Native handles')):
            ax.set_ylabel(title);ax.grid(alpha=.25)
        axes[-1].set_xlabel('Real wall time (minutes)');axes[0].legend(fontsize=7)
        fig.suptitle('Owned generated-source reliability fixtures — not gameplay');fig.tight_layout()
        fig.savefig(out/'memory-history.png',dpi=150);plt.close(fig)
    for r in results:
        slope=r['private_slope_last_600_seconds_KiB_s']
        print(r['file'],round(r['last_running_second'],1),'s',round(r['running_private_last_MiB'],2),'MiB',round(slope,3) if slope is not None else 'insufficient samples','KiB/s tail')

if __name__=='__main__':main()
