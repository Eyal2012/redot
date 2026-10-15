"""Inspect finalized owned MP4 sample tables without loading media payloads.

This is container/layout evidence, not a decoder or a perceptual audio test.
"""
import argparse
import json
import struct
from pathlib import Path

BUILD=Path(__file__).resolve().parents[1]/'build'
def u32(data,at):return struct.unpack_from('>I',data,at)[0]
def u64(data,at):return struct.unpack_from('>Q',data,at)[0]
def boxes(data,start=0,end=None):
    end=len(data) if end is None else end
    while start+8<=end:
        size=u32(data,start);kind=data[start+4:start+8].decode('ascii',errors='replace');header=8
        if size==1:size=u64(data,start+8);header=16
        if size==0:size=end-start
        if size<header or start+size>end:raise ValueError('Invalid '+kind+' box extent')
        yield kind,start+header,start+size,size
        start+=size
    if start!=end:raise ValueError('Trailing incomplete MP4 box')
def child(data,kind):
    return [data[a:b] for k,a,b,_ in boxes(data) if k==kind]
def one(data,kind):
    result=child(data,kind)
    if len(result)!=1:raise ValueError('Expected one '+kind)
    return result[0]
def read_moov(path):
    size=path.stat().st_size;top=[];moov=None
    with path.open('rb') as f:
        at=0
        while at<size:
            f.seek(at);head=f.read(16)
            if len(head)<8:raise ValueError('Truncated box header')
            length=u32(head,0);kind=head[4:8].decode('ascii',errors='replace');header=8
            if length==1:length=u64(head,8);header=16
            if length==0:length=size-at
            if length<header or at+length>size:raise ValueError('Invalid top-level box')
            top.append({'kind':kind,'offset':at,'bytes':length})
            if kind=='moov':
                if length>256*1024*1024:raise ValueError('Unreasonably large metadata')
                f.seek(at+header);moov=f.read(length-header)
            at+=length
    if moov is None:raise ValueError('No finalized moov')
    return moov,top
def audit(path):
    moov,top=read_moov(path);tracks=[]
    for trak in child(moov,'trak'):
        tkhd=one(trak,'tkhd');trackId=u32(tkhd,20 if tkhd[0]==1 else 12)
        mdia=one(trak,'mdia');hdlr=one(mdia,'hdlr');kind=hdlr[8:12].decode('ascii')
        mdhd=one(mdia,'mdhd');timescale=u32(mdhd,20 if mdhd[0]==1 else 12);duration=u64(mdhd,24) if mdhd[0]==1 else u32(mdhd,16)
        stbl=one(one(mdia,'minf'),'stbl');tableBytes={k:n for k,_,_,n in boxes(stbl)}
        stsz=one(stbl,'stsz');sampleSize=u32(stsz,4);count=u32(stsz,8)
        if not sampleSize and len(stsz)!=12+count*4:raise ValueError('Sample size count mismatch')
        offsets=child(stbl,'stco') or child(stbl,'co64')
        if not offsets:raise ValueError('Missing chunk offsets')
        chunks=u32(offsets[0],4);is64=bool(child(stbl,'co64'))
        if len(offsets[0])!=8+chunks*(8 if is64 else 4):raise ValueError('Chunk offset count mismatch')
        descriptions=one(stbl,'stsd');entries=list(boxes(descriptions,8));codec=entries[0][0] if entries else '?'
        stts=one(stbl,'stts');timingCount=u32(stts,4);timedSamples=sum(u32(stts,8+i*8) for i in range(timingCount));timedDuration=sum(u32(stts,8+i*8)*u32(stts,12+i*8) for i in range(timingCount))
        if timedSamples!=count:raise ValueError('Timestamp/sample count mismatch')
        info={'track_id':trackId,'kind':kind,'codec':codec,'timescale':timescale,'duration_seconds':duration/timescale,'sample_count':count,'chunks':chunks,'table_bytes':tableBytes,'table_total_bytes':sum(tableBytes.values()),'timing_table_duration_seconds':timedDuration/timescale}
        stss=child(stbl,'stss');info['first_sync_sample']=u32(stss[0],8) if stss and u32(stss[0],4) else None
        if kind=='vide':
            payload=descriptions[entries[0][1]:entries[0][2]];info['width']=struct.unpack_from('>H',payload,24)[0];info['height']=struct.unpack_from('>H',payload,26)[0]
            avc=child(payload[78:],'avcC');lengthBytes=(avc[0][4]&3)+1 if avc else 4
            firstOffset=(u64 if is64 else u32)(offsets[0],8);firstSize=sampleSize or u32(stsz,12)
            if firstSize>8*1024*1024:raise ValueError('Oversized first access unit')
            with path.open('rb') as f:f.seek(firstOffset);au=f.read(firstSize)
            nalTypes=[];at=0
            while at+lengthBytes<=len(au):
                n=int.from_bytes(au[at:at+lengthBytes],'big');at+=lengthBytes
                if not n or at+n>len(au):raise ValueError('Malformed first AVC access unit')
                nalTypes.append(au[at]&31);at+=n
            info['first_access_unit_nal_types']=nalTypes;info['first_access_unit_has_IDR']=5 in nalTypes
        tracks.append(info)
    return {'file':str(path.relative_to(BUILD.parent)),'file_bytes':path.stat().st_size,'boxes':top,'tracks':tracks,'total_samples':sum(t['sample_count'] for t in tracks),'sample_table_bytes':sum(t['table_total_bytes'] for t in tracks),'note':'Container evidence only; independent decoding is separate.'}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('files',type=Path,nargs='+');p.add_argument('--output',type=Path,required=True);a=p.parse_args();results=[]
    for file in a.files:
        file=file.resolve()
        if not file.is_relative_to(BUILD):p.error('Use owned files under build')
        try:results.append(audit(file))
        except (ValueError,struct.error,OSError) as e:results.append({'file':str(file),'error':str(e)})
    output=a.output.resolve()
    if not output.is_relative_to(BUILD):p.error('Keep evidence under build')
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(results,indent=2))
    for result in results:print(result['file'],result.get('error') or f"{len(result['tracks'])} tracks; {result['total_samples']} samples; {result['sample_table_bytes']} table bytes")
    raise SystemExit(any('error' in r for r in results))
if __name__=='__main__':main()
