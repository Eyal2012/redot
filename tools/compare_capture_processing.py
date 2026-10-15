"""Independent reference of packet conversion and mixing against opt-in raw WAVs.
Quiet/offline. Requires a finalized trace with no device loss/format changes.
Float comparison allows rounding; does not infer audible quality from samples.
"""
import argparse,array,csv,json,math,struct
from pathlib import Path
from analyze_capture_checkpoints import wave
f32=lambda value:struct.unpack('<f',struct.pack('<f',value))[0]
def convert(folder,stage,name,end,sync=0,legacy=False):
    info,raw=wave(folder/name);rate=corrected=info['rate'];channels=info['channels']
    rows=[r for r in csv.DictReader((folder/'packets.csv').open()) if int(r['stage'])==stage]
    if not rows:return array.array('f',[0])*end*2
    anchor=math.floor(int(rows[0]['qpc_relative_ns'])*48000/1e9+.5)+sync*48
    out=array.array('f',[0])*end*2;index=0;phase=0.;prev=None;last_qpc=last_pos=0;remainder=0.;step=rate/48000
    for row in rows:
        frames=int(row['frames']);first=int(row['file_first_frame']);at=int(row['qpc_relative_ns']);pos=int(row['device_position'])
        if last_qpc and at-last_qpc>=1e9 and pos>last_pos:
            measured=(pos-last_pos)*1e9/(at-last_qpc);ppm=max(-500,min(500,(measured/rate-1)*1e6));corrected=corrected*.8+rate*(1+ppm/1e6)*.2;last_qpc=at;last_pos=pos
        if not last_qpc:last_qpc=at;last_pos=pos
        step=corrected/48000
        if legacy:
            exact=frames*48000/corrected+remainder;count=int(exact);remainder=exact-count
            packet_anchor=int(at*48000/1e9)+sync*48
            for n in range(count):
                x=min(frames-1,n*step);a=int(x);b=min(a+1,frames-1);frac=f32(x-a);destination=packet_anchor+n
                if 0<=destination<end:
                    for ch in range(2):
                        va=raw[(first+a)*channels+min(ch,channels-1)];vb=raw[(first+b)*channels+min(ch,channels-1)]
                        out[destination*2+ch]=f32(va+f32(f32(vb-va)*frac))
            continue
        while phase<frames:
            a=math.floor(phase);frac=phase-a
            if frac<1e-6:frac=0.
            elif 1-frac<1e-6:a+=1;frac=0.
            if a>=frames or (frac>0 and a+1>=frames):break
            if a<0 and (prev is None or a< -1):break
            destination=anchor+index
            for ch in range(2):
                va=prev[ch] if a<0 else raw[(first+a)*channels+min(ch,channels-1)]
                vb=va if not frac else raw[(first+a+1)*channels+min(ch,channels-1)]
                sample=f32(va+f32(f32(vb-va)*f32(frac)))
                if 0<=destination<end:out[destination*2+ch]=sample
            index+=1;phase+=step
        phase-=frames
        if abs(phase)<1e-6:phase=0.
        prev=[raw[(first+frames-1)*channels+min(ch,channels-1)] for ch in range(2)]
    while prev is not None and phase< -1e-6:
        if 0<=anchor+index<end:out[(anchor+index)*2:(anchor+index)*2+2]=array.array('f',prev)
        index+=1;phase+=step
    return out

def compare(a,b):
    count=min(len(a),len(b));square=0;peak=0;mismatches=0
    for x,y in zip(a,b):
        difference=abs(x-y);square+=difference*difference;peak=max(peak,difference);mismatches+=difference>1e-7
    return {'samples':count,'rms_error':math.sqrt(square/count) if count else 0,'max_error':peak,'differences_over_1e-7':mismatches}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('folder',type=Path);ap.add_argument('--game-gain',type=float,default=1);ap.add_argument('--mic-gain',type=float,default=1);ap.add_argument('--game-sync',type=int,default=0);ap.add_argument('--mic-sync',type=int,default=0);args=ap.parse_args();folder=args.folder
    summary=(folder/'summary.txt').read_text();assert 'finalized=1\n' in summary and 'dropped_events=0\n' in summary,'incomplete trace'
    info,actual=wave(folder/'02-Processed Desktop.wav');end=info['frames']
    game=convert(folder,0,'01-Raw Desktop.wav',end,args.game_sync);mic=convert(folder,2,'03-Raw Microphone.wav',end,args.mic_sync)
    result={'actual_raw_to_processed_reference':compare(game,actual)}
    old=convert(folder,0,'01-Raw Desktop.wav',end,args.game_sync,legacy=True);result['legacy_packet_conversion_counterfactual']=compare(old,actual)
    mi,actual_mix=wave(folder/'04-Final Mix.wav');expected=array.array('f')
    for g,m in zip(game,mic):
        total=f32(f32(g*args.game_gain)+f32(m*args.mic_gain));expected.append(int(f32(max(-1,min(1,total))*32767))/32768)
    result['actual_raw_sources_to_mix_reference']=compare(expected,actual_mix)
    result['passed']=result['actual_raw_to_processed_reference']['max_error']<2e-6 and result['actual_raw_sources_to_mix_reference']['max_error']<=1/32768
    result['note']='Legacy values are an offline counterfactual using the new raw capture, not a pre-fix physical recording. Gain/mute/sync must match the recording. This is PCM evidence, not a listening verdict.'
    (folder/'processing-comparison.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2));return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
