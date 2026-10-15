"""Prepare only owned, quiet test media and settings beneath this project's build."""
from pathlib import Path
import argparse
import shutil

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("fixtures", type=Path)
parser.add_argument("--format", choices=("mp4", "mkv", "absent", "invalid"), default="mp4",
                    help="recordFormat written to the owned profile (absent omits the key; invalid writes junk)")
args = parser.parse_args()
project = Path(__file__).resolve().parents[1]
root = args.root.resolve()
if not root.is_relative_to(project / "build"):
    raise SystemExit("Quiet artifacts must be under the ReDot project build directory")
if root.exists():
    raise SystemExit("Use a fresh test artifact folder; previous results are preserved")
output = root / "output"
output.mkdir(parents=True)
for path in args.fixtures.glob("*/*"):
    if path.is_file() and path.suffix in (".mp4", ".mkv", ".tracks"):
        prefix = "ReDot_Clip_" if path.name.startswith(("PandaClip_Clip_", "ReDot_Clip_")) else "ReDot_"
        shutil.copy2(path, output / (prefix + path.parent.name + "_" + path.name))
local = root / "local" / "PandaClip"
local.mkdir(parents=True)
settings = {'folder': output.as_posix(), 'width': '1280', 'height': '720', 'fps': '30',
            'gameEnabled': '0', 'micEnabled': '0', 'gameMode': '0', 'closeTray': '0',
            }
if args.format == 'mp4':
    settings['recordFormat'] = '1'
elif args.format == 'mkv':
    settings['recordFormat'] = '0'
elif args.format == 'invalid':
    settings['recordFormat'] = 'banana'
with (local / "settings-v1.txt").open("w", encoding="utf-8", newline="\n") as file:
    file.write("PandaClip settings 1\n")
    for key, value in settings.items():
        file.write(f'"{key}" "{value}"\n')
