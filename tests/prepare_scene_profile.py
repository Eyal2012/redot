"""Owned scene composition fixture only. No captured desktop/video is used as styling."""
from pathlib import Path
import json, struct, zlib, sys, uuid

root = Path(sys.argv[1]).resolve()
project = Path(__file__).resolve().parents[1]
if not root.is_relative_to(project / "build"):
    raise SystemExit("Scene fixtures must be inside the ReDot project build directory")
folder = root / "local" / "PandaClip"
folder.mkdir(parents=True, exist_ok=True)
path = folder / "scenes-v1.json"
if path.exists():
    raise SystemExit("Refusing to overwrite an existing scene fixture")
uid = lambda: str(uuid.uuid4())
image_path = root / "owned-scene-image.png"
w, h = 384, 216
raw = bytearray()
for y in range(h):
    raw.append(0)
    for x in range(w):
        color = (44, 37, 66) if (x//48+y//48) % 2 else (78, 64, 118)
        if (x-192)**2 + (y-108)**2 < 54**2: color=(198, 176, 244)
        raw.extend(color)
def chunk(name, data):
    return struct.pack('>I', len(data)) + name + data + struct.pack('>I', zlib.crc32(name+data))
image_path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB',w,h,8,2,0,0,0)) + chunk(b'IDAT',zlib.compress(raw)) + chunk(b'IEND',b''))
defs=[]
def definition(kind,name,settings,width=1920,height=1080):
    id=uid();defs.append(dict(id=id,kind=kind,name=name,settings=settings,intrinsicWidth=width,intrinsicHeight=height));return id
game=definition(0,'Game Capture',{'target':'Select your game in Scenes','executable':'__owned_unavailable_demo__'})
title=definition(5,'Stream title',{'text':'ReDot Scenes','fontSize':88,'color':'#F5F0FF'},1100,150)
badge=definition(4,'Image overlay',{'path':image_path.as_posix()},w,h)
mic=definition(6,'Microphone',{'deviceId':'','deviceName':'Windows default recording input (eConsole)'})
desktop=definition(7,'Desktop Audio',{'deviceId':'','deviceName':'Whole system — explicit'})
def visual(source,x,y,width,height):
    return dict(id=uid(),source=source,x=x,y=y,width=width,height=height,scale=1,rotation=0,crop=[0,0,0,0],visible=True,locked=False,aspectLocked=True)
def audio(source,gain):
    return dict(id=uid(),source=source,gain=gain,muted=False,syncMs=0,monitoring=False)
first=uid();second=uid()
data=dict(format='PandaClipScenes',version=1,migrationVersion=2,selected=first,definitions=defs,scenes=[
    dict(id=first,name='Gameplay',width=1920,height=1080,visuals=[visual(game,0,0,1920,1080),visual(title,64,56,1100,150),visual(badge,1450,790,w,h)],audio=[]),
    dict(id=second,name='Just the title',width=1920,height=1080,visuals=[visual(title,410,460,1100,150)],audio=[])])
path.write_text(json.dumps(data,indent=2),encoding='utf-8')
print(path)
