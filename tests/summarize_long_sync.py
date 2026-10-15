"""Read independent decodes and export offsets from an owned long-session fixture."""
from pathlib import Path
import argparse,json,re,statistics
p=argparse.ArgumentParser();p.add_argument('root',type=Path);a=p.parse_args();root=a.root.resolve();project=Path(__file__).resolve().parents[1]
if not root.is_relative_to(project/'build'):raise SystemExit('Use owned build evidence')
pattern=re.compile(r'event \d+: flash=([-\d.]+)ms beep=([-\d.]+)ms audio-minus-video=([-\d.]+)ms')
def read(path):
 s=path.read_text(errors='replace');return s,[tuple(map(float,m)) for m in pattern.findall(s)]
ordinary=list((root/'decode').glob('ReDot_20*.txt'))
if len(ordinary)!=1:raise SystemExit('Expected one ordinary recording decode')
text,events=read(ordinary[0]);fail=[];matched=[];clips=[]
logs=(root/'local/PandaClip/logs/panda.log').read_text(errors='replace')
exports=re.findall(r'Replay clip saved: (.*?\.mp4); actual duration=([\d.]+) seconds,.*?common offset=(\d+) hns',logs)
for file,duration,offset in exports:
 path=root/'decode'/(Path(file).stem+'.txt')
 if not path.exists():fail.append('Missing decode '+path.name);continue
 s,cues=read(path);actual=float(duration);common=int(offset)/10000;deltas=[]
 if 'decodeResult=PASS' not in s or 'tracks: video=1 audio=3' not in s:fail.append('Invalid decode/layout '+path.name)
 for flash,beep,delta in cues:
  # Initial/final truncated cues are different observations; compare complete interior events.
  if flash<200 or flash>actual*1000-200:continue
  original=min(events,key=lambda e:abs(e[0]-(flash+common))) if events else None
  if not original or abs(original[0]-flash-common)>1:fail.append('Unmatched interior flash '+path.name);continue
  difference=(beep+common)-original[1];deltas.append(difference);matched.append(difference)
  if abs(difference)>1:fail.append('Audio rebasing differs '+path.name+' '+str(difference))
 clips.append({'file':path.name,'seconds':actual,'common_offset_ms':common,'interior_events':len(deltas),'max_audio_timestamp_difference_ms':max(map(abs,deltas)) if deltas else None})
late=[e[2] for e in events if e[0]>=60000]
result={'method':'Generated timed GPU/PCM cues; independent MF decoding; interior ordinary/replay events compared using the logged common offset. No physical WGC, WASAPI or perceptual listening result.','ordinary_decode_pass':'decodeResult=PASS' in text,'ordinary_events':len(events),'ordinary_offset_range_ms':[min(e[2] for e in events),max(e[2] for e in events)] if events else None,'post_warmup_offset_range_ms':[min(late),max(late)] if late else None,'first_last_offset_ms':[events[0][2],events[-1][2]] if events else None,'replays':len(clips),'matched_interior_events':len(matched),'max_rebase_difference_ms':max(map(abs,matched)) if matched else None,'failures':fail,'clips':clips}
(root/'sync-summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in result.items() if k!='clips'},indent=2));raise SystemExit(bool(fail))
