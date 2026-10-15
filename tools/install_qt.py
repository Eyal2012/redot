"""Install the pinned dynamic Qt SDK locally; do not change the system PATH."""
from pathlib import Path
import os
import subprocess
import sys

project = Path(__file__).resolve().parents[1]
os.chdir(project)
installer = project / "build" / "qt-installer"
sdk = project / "third_party" / "Qt" / "6.8.3" / "msvc2022_64"
if not (sdk / "bin" / "qmake.exe").exists() or not (sdk / "bin" / "Qt6Multimedia.dll").exists():
    subprocess.run([sys.executable, "-m", "pip", "install", "--target", str(installer),
                    "aqtinstall==3.3.0"], check=True)
    # aqt 3.3.0 assumes UTF-8 qmake output. On Hebrew Windows, qmake can
    # return the ANSI code page. Patch only this project's MIT installer.
    updater = installer / "aqt" / "updater.py"
    text = updater.read_text(encoding="utf-8")
    text = text.replace('line.decode("UTF-8")', '_pandaclip_qmake_decode(line)')
    text += '\n\ndef _pandaclip_qmake_decode(line):\n    try:\n        return line.decode("utf-8")\n    except UnicodeDecodeError:\n        return line.decode("mbcs")\n'
    updater.write_text(text, encoding="utf-8", newline="\n")
    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(installer)
    subprocess.run([sys.executable, "-m", "aqt", "install-qt", "windows", "desktop", "6.8.3",
                    "win64_msvc2022_64", "-O", "third_party/Qt", "-m", "qtshadertools", "qtmultimedia",
                    "-b", "https://mirrors.ocf.berkeley.edu/qt/", "--timeout", "30",
                    "--archives", "qtbase", "qtdeclarative", "qttools", "qtmultimedia", "-k", "-d",
                    "build/qt-installer/archives"], env=environment, check=True)
# Runtime relocation remains relative, including in paths with Hebrew characters.
(sdk / "bin" / "qt.conf").write_text("[Paths]\nPrefix=..\n", encoding="utf-8", newline="\n")
for required in ("bin/qmake.exe", "bin/windeployqt.exe", "lib/cmake/Qt6/Qt6Config.cmake",
                 "bin/Qt6Quick.dll", "bin/Qt6QuickWidgets.dll", "bin/Qt6Widgets.dll", "bin/Qt6Multimedia.dll", "qml/QtQuick/Effects/qmldir"):
    if not (sdk / required).exists():
        raise SystemExit("Incomplete Qt SDK: " + required + "; reinstall the project-local Qt SDK")
result = subprocess.run([str(sdk / "bin" / "qmake.exe"), "-query", "QT_VERSION"],
                        check=True, capture_output=True)
if result.stdout.strip() != b"6.8.3":
    raise SystemExit("Unexpected Qt version")
print("Qt 6.8.3 MSVC 2022 x64 is ready inside ReDot.")
