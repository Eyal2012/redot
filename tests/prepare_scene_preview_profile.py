"""Owned static scene evidence; no personal capture, preferences or input."""
import argparse
import json
import shutil
import uuid
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("root", type=Path)
p.add_argument("--case", choices=("filling", "smaller", "multiple"), default="filling")
p.add_argument("--width", type=int, choices=(1280, 1920, 2560), default=1920)
a = p.parse_args()
project = Path(__file__).resolve().parents[1]
root = a.root.resolve()
if not root.is_relative_to(project / "build") or root.exists():
    raise SystemExit("Use a fresh owned folder under build")
local = root / "local/PandaClip"
local.mkdir(parents=True)
(root / "output").mkdir()
asset = root / "owned-scene.png"
shutil.copy2(project / "build/scenes-preview-before/owned-scene.png", asset)
uid = lambda: str(uuid.uuid4())
source, scene = uid(), uid()
w, h = a.width, a.width * 9 // 16
v = dict(id=uid(), source=source, x=0, y=0, width=w, height=h, scale=1,
         rotation=0, crop=[0, 0, 0, 0], visible=True, locked=False, aspectLocked=True)
if a.case == "smaller":
    v.update(x=w/4, y=h/4, width=w/2, height=h/2)
visuals = [v]
if a.case == "multiple":
    visuals.append(dict(v, id=uid(), x=w/4, y=h/4, width=w/2, height=h/2, rotation=15))
model = dict(format="PandaClipScenes", version=1, migrationVersion=2,
             selected=scene, definitions=[dict(id=source, name="Owned 16:9 grid", kind=4,
             settings={"path": str(asset)}, intrinsicWidth=1920, intrinsicHeight=1080)],
             scenes=[dict(id=scene, name=a.case.title(), width=w, height=h, visuals=visuals, audio=[])])
(local / "scenes-v1.json").write_text(json.dumps(model, indent=2), encoding="utf-8")
settings = dict(folder=(root/"output").as_posix(), width=str(w), height=str(h), fps="60",
                gameEnabled="0", micEnabled="0", gameMode="0", closeTray="0", recordFormat="1")
(local / "settings-v1.txt").write_text("PandaClip settings 1\n" + "".join(
    f'"{k}" "{v}"\n' for k, v in settings.items()), encoding="utf-8")
(root / "scene-preview-cases.txt").write_text("Initial persisted scene only; no UIA row selection claim")
