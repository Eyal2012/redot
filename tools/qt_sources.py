"""Download Qt's matching unmodified sources and license texts for distribution."""
from pathlib import Path
import hashlib
import tarfile
import urllib.request

project = Path(__file__).resolve().parents[1]
sources = project / "third_party" / "Qt" / "sources"
notices = project / "third_party" / "Qt" / "notices"
sources.mkdir(parents=True, exist_ok=True)
for module in ("qtbase", "qtdeclarative", "qtshadertools", "qttranslations", "qttools", "qtmultimedia"):
    name = f"{module}-everywhere-src-6.8.3.tar.xz"
    url = "https://download.qt.io/archive/qt/6.8/6.8.3/submodules/" + name
    path = sources / name
    if not path.exists():
        with urllib.request.urlopen(url, timeout=90) as response, path.open("wb") as output:
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
    expected = urllib.request.urlopen(url + ".sha256", timeout=40).read().decode().split()[0]
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise SystemExit("SHA-256 mismatch: " + name)
    with tarfile.open(path) as archive:
        for member in archive.getmembers():
            parts = Path(member.name).parts
            if member.isfile() and ("/LICENSES/" in member.name or
                (len(parts) == 2 and ("LICENSE" in parts[-1] or parts[-1].startswith("COPYING")))):
                destination = notices / module / Path(*parts[1:])
                if not destination.resolve().is_relative_to(notices.resolve()):
                    raise SystemExit("Invalid archive license path")
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(archive.extractfile(member).read())
    print(module + ": SHA-256 verified, source and notices retained")

# Qt 6.8.3's dynamically linked playback backend ships FFmpeg 7.1 (LGPL 2.1+).
# Qt's attribution bundle contains notices, not the complete FFmpeg source.
ffmpeg = sources / "ffmpeg-7.1.tar.xz"
if not ffmpeg.exists():
    with urllib.request.urlopen("https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz",timeout=90) as response, ffmpeg.open("wb") as output:
        while chunk := response.read(1024*1024): output.write(chunk)
digest = hashlib.sha256(ffmpeg.read_bytes()).hexdigest()
if digest != "40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6":
    raise SystemExit("FFmpeg 7.1 source does not match the pinned upstream archive")
hashfile = sources / "ffmpeg-7.1.sha256"
if hashfile.exists() and hashfile.read_text().strip() != digest:
    raise SystemExit("FFmpeg source checksum mismatch")
hashfile.write_text(digest+"\n",encoding="utf-8")
with tarfile.open(ffmpeg) as archive:
    for member in archive.getmembers():
        parts=Path(member.name).parts
        if member.isfile() and len(parts)==2 and parts[-1].startswith(("COPYING","LICENSE")):
            destination=notices/"ffmpeg"/parts[-1]
            destination.parent.mkdir(parents=True,exist_ok=True)
            destination.write_bytes(archive.extractfile(member).read())
print("FFmpeg 7.1: upstream HTTPS source retained; SHA-256 " + digest)
