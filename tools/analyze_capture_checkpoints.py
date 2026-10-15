"""Read lossless capture checkpoints without playing sound or filtering PCM.

Waveform excursions and jumps are measurements, not proof of audible crackle.
Native rate/drift/initial sample rounding can preclude exact raw/processed match.
"""
import argparse, array, json, math, struct
from pathlib import Path

def wave(path):
    raw=path.read_bytes()
    if raw[:4]!=b'RIFF' or raw[8:12]!=b'WAVE': raise ValueError('not WAV')
    if struct.unpack_from('<I',raw,4)[0]+8!=len(raw):raise ValueError('unfinalized RIFF size')
    pos=12;fmt=body=None
    while pos+8<=len(raw):
        tag=raw[pos:pos+4];size=struct.unpack_from('<I',raw,pos+4)[0];chunk=raw[pos+8:pos+8+size]
        if len(chunk)!=size:raise ValueError('truncated chunk')
        if tag==b'fmt ':fmt=chunk
        if tag==b'data':body=chunk
        pos+=8+size+(size%2)
    if fmt is None or body is None:raise ValueError('missing PCM')
    tag,ch,rate,avg,align,bits=struct.unpack_from('<HHIIHH',fmt)
    if tag==0xfffe:tag=struct.unpack_from('<H',fmt,24)[0]
    if len(body)%align:raise ValueError('frame alignment')
    if tag==3 and bits in (32,64):samples=array.array('f' if bits==32 else 'd',body)
    elif tag==1 and bits in (16,32):
        samples=array.array('h' if bits==16 else 'i',body);scale=2**(bits-1);samples=[v/scale for v in samples]
    elif tag==1 and bits==8:samples=[(v-128)/128 for v in body]
    elif tag==1 and bits==24:
        samples=[int.from_bytes(body[i:i+3], 'little',signed=True)/8388608 for i in range(0,len(body),3)]
    else:raise ValueError(f'unsupported native PCM {tag}/{bits}')
    return dict(rate=rate,channels=ch,bits=bits,format=tag,frames=len(body)//align,duration_seconds=len(body)/align/rate),samples

def main():
    ap=argparse.ArgumentParser();ap.add_argument('folder',type=Path);ap.add_argument('--fixture',action='store_true',help='require known 48k float fixture equality');args=ap.parse_args()
    files=['01-Raw Desktop.wav','02-Processed Desktop.wav','03-Raw Microphone.wav','04-Final Mix.wav'];result={'tracks':[]};decoded={};passed=True
    for name in files:
        path=args.folder/name
        if not path.exists():result['tracks'].append({'name':name,'captured':False});passed &= not args.fixture;continue
        info,s=wave(path);decoded[name]=(info,s);finite=[v for v in s if math.isfinite(v)];info.update(name=name,captured=True,nonfinite_samples=len(s)-len(finite),peak=max(map(abs,finite),default=0),rms=math.sqrt(sum(v*v for v in finite)/len(finite)) if finite else 0,over_range_samples=sum(abs(v)>1 for v in finite),near_full_scale_samples=sum(abs(v)>=.999 for v in finite),max_adjacent_same_channel_jump=max((abs(s[i]-s[i-info['channels']]) for i in range(info['channels'],len(s))),default=0));result['tracks'].append(info)
        passed &= info['nonfinite_samples']==0
    summary=args.folder/'summary.txt'
    if not summary.exists():passed=False;result['trace_error']='missing finalization summary'
    else:
        result['trace_summary']=summary.read_text();passed &= 'finalized=1\n' in result['trace_summary'] and 'dropped_events=0\n' in result['trace_summary']
    if args.fixture:
        a=decoded[files[0]][1];b=decoded[files[1]][1];m=decoded[files[2]][1];c=decoded[files[3]][1]
        result['raw_to_processed_exact']=a==b;passed &= a==b
        result['raw_mic_exact']=a==m;passed &= a==m
        # Production pcm(value) truncates after multiplying 32767, WAV decoding
        # divides by 32768. Match quantization rather than mistaking it for loss.
        f32=lambda v:struct.unpack("<f",struct.pack("<f",v))[0]
        expected=[int(f32(max(-1,min(1,f32(x+y)))*32767))/32768 for x,y in zip(a,m)]
        result['mix_matches_exact_sum_quantization']=expected==c;passed &= expected==c
        result['equal_frame_counts']=len(a)==len(b)==len(m)==len(c);passed &= result['equal_frame_counts']
    result['passed']=passed;result['warning']='Signal jumps/clipping statistics are not listening results. Do not label valid transients as pops. Raw native timing retains drift; compare packet CSV before sample alignment.'
    (args.folder/'waveform-analysis.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2));return 0 if passed else 1
if __name__=='__main__':raise SystemExit(main())
