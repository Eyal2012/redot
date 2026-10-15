"""Rebuild only Qt 6.8.3 Multimedia's core with the playback timer repair.

The installed SDK and FFmpeg/QML plugins are unmodified. Outputs and corresponding
patched sources remain under build. Invoked by CMake, not at application startup.
"""
import ctypes
import hashlib
import os
import re
import shutil
from pathlib import Path
import subprocess
import sys
import tarfile
import urllib.request

root = Path(__file__).resolve().parents[1]
configuration = "Debug" if sys.argv[1] == "Debug" else "RelWithDebInfo"
archive = root / "third_party/Qt/sources/qtmultimedia-everywhere-src-6.8.3.tar.xz"
source_root = root / "build/qt-audio-runtime-source"
source = source_root / "qtmultimedia-everywhere-src-6.8.3"
binary = root / "build/qt-output-runtime-ninja"
sdk = root / "third_party/Qt/6.8.3/msvc2022_64"
if not archive.exists():
    sys.exit("Missing Qt 6.8.3 source archive; run py -3 tools/qt_sources.py")
if hashlib.sha256(archive.read_bytes()).hexdigest() != "32e82307d783172a3b984cc3c47c5e4e8b819cee3cbfc702c7012c47f15f6b01":
    sys.exit("Unexpected Qt source archive; refusing an unaudited playback patch")
runtime = binary / "bin" / ("Qt6Multimediad.dll" if configuration == "Debug" else "Qt6Multimedia.dll")
stamp = binary / (configuration + ".pandaclip-audio-stamp")
trace_header = root / "tools/qt_playback_trace.h"
recipe_hash = hashlib.sha256(Path(__file__).read_bytes() + trace_header.read_bytes() + archive.read_bytes()).hexdigest()
if runtime.exists() and stamp.exists() and stamp.read_text(encoding="ascii") == recipe_hash:
    print("Playback runtime is current:", runtime)
    sys.exit(0)
vulkan = root / "build/qt-audio-vulkan/Vulkan-Headers-1.3.290/include"
if not (vulkan / "vulkan/vulkan.h").exists():
    download = root / "build/qt-audio-vulkan/vulkan-headers-1.3.290.tar.gz"
    download.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen("https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/refs/tags/v1.3.290", timeout=30) as response, download.open("wb") as output:
        shutil.copyfileobj(response, output)
    if hashlib.sha256(download.read_bytes()).hexdigest() != "f38a653bf93cab7a2a229a53d2d53b1cba9a2819e4c0a7de13c54085bde9bcf5":
        sys.exit("Vulkan header archive did not match the pinned hash")
    with tarfile.open(download) as tar:
        tar.extractall(download.parent, filter="data")
source_root.mkdir(parents=True, exist_ok=True)
if not (source / "CMakeLists.txt").exists():
    with tarfile.open(archive) as tar:
        tar.extractall(source_root, filter="data")
relative = "src/multimedia/windows/qwindowsaudiosink.cpp"
# Always patch pristine archived bytes, so reruns cannot stack changes.
with tarfile.open(archive) as tar:
    pristine = tar.extractfile("qtmultimedia-everywhere-src-6.8.3/" + relative).read().decode("utf-8")
old_timer = """    m_timer->callOnTimeout(this, [this](){
        deviceStateChange(QAudio::IdleState, QAudio::UnderrunError);
    });"""
new_timer = """    m_timer->callOnTimeout(this, [this](){
        // PandaClip playback repair: a timer is a wake-up hint, not proof
        // WASAPI has consumed every frame. Check padding before stopping.
        if (deviceState != QAudio::ActiveState || !m_client)
            return;
        const auto remaining = m_client->remainingPlayTime();
        if (remaining > std::chrono::microseconds::zero()) {
            m_timer->start(std::chrono::ceil<std::chrono::milliseconds>(remaining));
            return;
        }
        deviceStateChange(QAudio::IdleState, QAudio::UnderrunError);
    });"""
old_start = "m_timer->start(duration_cast<milliseconds>(m_client->remainingPlayTime()));"
new_start = "m_timer->start(ceil<milliseconds>(m_client->remainingPlayTime()));"
if pristine.count(old_timer) != 1 or pristine.count(old_start) != 1:
    sys.exit("Qt playback patch does not match the pinned source; refusing to build")
patched = pristine.replace(old_timer, new_timer).replace(old_start, new_start)

# Checkpoints surround the actual Qt WASAPI render operation, not a parallel
# decoder. Disabled by default; payloads go to a bounded background writer.
patched = patched.replace('#include "qcomtaskresource_p.h"', '#include "qcomtaskresource_p.h"\n#include "pandaclip_playback_trace.h"')
patched = patched.replace('    : m_device{ device }, m_inputFormat{ format }\n{\n}', '    : m_device{ device }, m_inputFormat{ format }\n{\n    m_traceClientId = PandaClipTrace::nextClient();\n}')
patched = patched.replace('    m_audioClient->Start();', '    PandaClipTrace::event(m_traceClientId, "start", m_audioClient->Start());')
patched = patched.replace('    m_audioClient->Stop();', '    PandaClipTrace::event(m_traceClientId, "stop", m_audioClient->Stop());')
patched = patched.replace('        return -1;\n    }\n\n    if (volume', '        PandaClipTrace::event(m_traceClientId, "get-buffer-error", hr);\n        return -1;\n    }\n\n    if (volume')
patched = patched.replace('    return true;\n}\n\nstd::chrono::microseconds AudioClient::remainingPlayTime()', '''    if (PandaClipTrace::writer().enabled()) {
        ComPtr<IAudioClock> clock;
        UINT64 frequency{};
        if (SUCCEEDED(m_audioClient->GetService(IID_PPV_ARGS(clock.GetAddressOf()))))
            clock->GetFrequency(&frequency);
        LPWSTR deviceId{};
        m_device->GetId(&deviceId);
        PandaClipTrace::writer().format(m_traceClientId, m_inputFormat, m_outputFormat, deviceId ? deviceId : L"", frequency);
        CoTaskMemFree(deviceId);
    }
    // Verification silences only this owned output session. Samples and gain
    // still traverse the real WASAPI client; no global endpoint mute is used.
    if (qEnvironmentVariableIsSet("PANDACLIP_PLAYBACK_SILENT_SESSION")) {
        ComPtr<ISimpleAudioVolume> session;
        BOOL muted{};
        hr = m_audioClient->GetService(IID_PPV_ARGS(session.GetAddressOf()));
        if (FAILED(hr) || FAILED(session->SetMute(TRUE, nullptr)) || FAILED(session->GetMute(&muted)) || !muted)
            return false;
    }
    return true;
}

std::chrono::microseconds AudioClient::remainingPlayTime()''')
patched = patched.replace('    return writeSize;\n};', '''    if (PandaClipTrace::writer().enabled()) {
        PandaClipTrace::Write w;
        w.client = m_traceClientId; w.tick = PandaClipTrace::tick();
        w.requested = len; w.accepted = writeSize; w.frames = writeFramesNum;
        w.padding = allocatedFrames(m_audioClient.Get()).value_or(*framesAvailable) - *framesAvailable; w.volume = volume; w.result = hr;
        w.input.assign(data, data + writeSize);
        w.converted.assign(writeBytes.constData(), writeBytes.constData() + writeBytes.size());
        // The WASAPI pointer is invalid after ReleaseBuffer: the copy below
        // was retained immediately before releasing that buffer.
        w.submitted = std::move(traceSubmitted);
        ComPtr<IAudioClock> clock;
        if (SUCCEEDED(m_audioClient->GetService(IID_PPV_ARGS(clock.GetAddressOf()))))
            clock->GetPosition(&w.clock, nullptr);
        PandaClipTrace::writer().enqueue(std::move(w));
    }
    return writeSize;
};''')
patched = patched.replace('    DWORD flags = writeBytes.isEmpty()', '''    std::vector<char> traceSubmitted;
    if (PandaClipTrace::writer().enabled() && !writeBytes.isEmpty())
        traceSubmitted.assign(reinterpret_cast<char*>(buffer), reinterpret_cast<char*>(buffer) + writeBytes.size());
    DWORD flags = writeBytes.isEmpty()''')
patched += '''\nextern "C" Q_DECL_EXPORT void pandaclipFlushPlaybackTrace() {
    PandaClipTrace::writer().finish();
}
extern "C" Q_DECL_EXPORT void pandaclipDecodedPlaybackTrace(const char* data, int bytes, long long pts) {
    if (!PandaClipTrace::writer().enabled() || bytes <= 0) return;
    PandaClipTrace::Write w; w.kind = "decoded"; w.tick = PandaClipTrace::tick();
    w.clock = pts; w.requested = bytes; w.accepted = bytes; w.frames = bytes / 8;
    w.input.assign(data, data + bytes); PandaClipTrace::writer().enqueue(std::move(w));
}
extern "C" Q_DECL_EXPORT void pandaclipPlaybackCommandTrace(const char* command, long long value) {
    if (!PandaClipTrace::writer().enabled()) return;
    PandaClipTrace::Write w; w.kind = command; w.tick = PandaClipTrace::tick(); w.clock = value;
    PandaClipTrace::writer().enqueue(std::move(w));
}
'''
# Capture final clock/padding without changing the normal stop operation.
patched = patched.replace('    PandaClipTrace::event(m_traceClientId, "stop", m_audioClient->Stop());', '''    uint64_t position{};
    if (PandaClipTrace::writer().enabled()) {
        ComPtr<IAudioClock> clock;
        if (SUCCEEDED(m_audioClient->GetService(IID_PPV_ARGS(clock.GetAddressOf())))) clock->GetPosition(&position,nullptr);
    }
    const auto pending=usedFrames(m_audioClient.Get()).value_or(0);
    PandaClipTrace::event(m_traceClientId, "stop", m_audioClient->Stop(),position,pending);''')
(source / "src/multimedia/windows/pandaclip_playback_trace.h").write_bytes(trace_header.read_bytes())
with tarfile.open(archive) as tar:
    sink_header = tar.extractfile("qtmultimedia-everywhere-src-6.8.3/src/multimedia/windows/qwindowsaudiosink_p.h").read().decode("utf-8")
sink_header = sink_header.replace('    QAudioFormat m_outputFormat;\n};', '    QAudioFormat m_outputFormat;\n    quint64 m_traceClientId{};\n};')
(source / "src/multimedia/windows/qwindowsaudiosink_p.h").write_text(sink_header, encoding="utf-8", newline="\n")
target = source / relative
if target.read_text(encoding="utf-8") != patched:
    target.write_text(patched, encoding="utf-8", newline="\n")

def native_path(path):
    # Qt 6.8.3 syncqt does not reliably accept Hebrew paths in response files.
    # Use Windows' alias for the SAME workspace, never a checkout elsewhere.
    buffer = ctypes.create_unicode_buffer(32768)
    alias = str(root)
    if ctypes.windll.kernel32.GetShortPathNameW(str(root), buffer, len(buffer)):
        alias = buffer.value
    if not alias.isascii():
        sys.exit("Qt 6.8.3 source tools need an ASCII workspace path or an existing Windows short-path alias. This workspace has neither; enable short names for this project directory or build from an ASCII-named project workspace.")
    return str(Path(alias) / path.relative_to(root))

vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
installation = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
vcvars = Path(installation) / "VC/Auxiliary/Build/vcvars64.bat"
if not vcvars.exists():
    sys.exit("Missing MSVC x64 build tools; install Visual Studio 2022 Desktop development with C++")
environment = os.environ.copy()
variables = subprocess.check_output(f'cmd /d /s /c ""{vcvars}" >nul && set"', encoding="mbcs", errors="replace")
for line in variables.splitlines():
    if "=" in line and not line.startswith("="):
        key, value = line.split("=", 1)
        environment[key] = value
ninja = shutil.which("ninja", path=environment.get("PATH", ""))
if not ninja:
    candidate = Path(installation) / "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
    if candidate.exists():
        ninja = str(candidate)
if not ninja:
    sys.exit("Missing Ninja build tool; install Visual Studio C++ CMake tools or run winget install Ninja-build.Ninja")
subprocess.run(["cmake", "-S", native_path(source), "-B", native_path(binary),
                "-G", "Ninja Multi-Config",
                "-DCMAKE_MAKE_PROGRAM=" + ninja,
                "-DCMAKE_PREFIX_PATH=" + native_path(sdk),
                "-DVulkan_INCLUDE_DIR=" + native_path(vulkan),
                "-DQT_BUILD_EXAMPLES=OFF", "-DQT_BUILD_TESTS=OFF",
                "-DQT_FEATURE_ffmpeg=OFF"], check=True, env=environment)
# MSVC's linker treats non-BOM response files as ANSI. Normalize build-rule
# paths to the workspace alias before Ninja generates linker response files.
for rule in binary.rglob("*.ninja"):
    text = rule.read_text(encoding="utf-8")
    lines = []
    for line in text.splitlines(keepends=True):
        # Do not merge separate CMake dependency spellings into duplicate rules.
        if line.strip().startswith(("LINK_LIBRARIES =", "LINK_PATH =")):
            for long_part, short_part in zip(root.parts, Path(native_path(root)).parts):
                if long_part != short_part:
                    line = line.replace(long_part.replace(" ", "$ "), short_part).replace(long_part, short_part)
        lines.append(line)
    fixed = "".join(lines)
    if fixed != text:
        rule.write_text(fixed, encoding="utf-8", newline="\n")
# syncqt's generated-header forwarding rules are incomplete with this Qt/CMake
# combination. Materialize only the core's generated forwarding headers, and
# give syncqt ASCII aliases in its response file (all resolve inside root).
args = binary / "src/multimedia/Multimedia_syncqt_args"
text = args.read_text(encoding="utf-8").replace(root.as_posix(), native_path(root).replace("\\", "/"))
args.write_text(text, encoding="utf-8", newline="\n")
subprocess.run([str(sdk / "bin/syncqt.exe"), "@" + native_path(args)], check=True)
subprocess.run(["cmake", "--build", native_path(binary), "--config", configuration,
                "--target", "Multimedia_autogen", "--parallel", "4"], check=True, env=environment)
include = binary / "include/QtMultimedia"
for name in ("qtmultimedia-config.h", "qtmultimedia-config_p.h"):
    destination = include / "6.8.3/QtMultimedia/private" if name.endswith("_p.h") else include
    destination.mkdir(parents=True, exist_ok=True)
    (destination / name).write_bytes((binary / "src/multimedia" / name).read_bytes())
for staged in (include / ".syncqt_staging").iterdir():
    if staged.is_file():
        (include / staged.name).write_bytes(staged.read_bytes())
# Qt's forwarding generator canonicalizes aliases and writes Windows ANSI
# filenames into UTF-8 compilation inputs. Use ASCII aliases for those includes.
for generated in include.rglob("*.h"):
    data = generated.read_bytes()
    replacement = native_path(root).replace("\\", "/").encode("ascii")
    for encoding in ("utf-8", "mbcs"):
        data = data.replace(root.as_posix().encode(encoding), replacement)
    if data != generated.read_bytes():
        generated.write_bytes(data)
for generated in (binary / "src/multimedia/Multimedia_autogen").rglob("*.cpp"):
    text = generated.read_text(encoding="utf-8")
    def include_alias(match):
        path = (generated.parent / match[1]).resolve()
        if path.is_relative_to(root) and path.is_file():
            return '#include "' + native_path(path).replace("\\", "/") + '"'
        return match[0]
    fixed = re.sub(r'#include "([^"]+)"', include_alias, text)
    if fixed != text:
        generated.write_text(fixed, encoding="utf-8", newline="\n")
# The core DLL exports the unchanged 6.8.3 ABI. Retain the original FFmpeg plugin
# and zero-copy video path; we do not build or replace any media backend plugin.
subprocess.run(["cmake", "--build", native_path(binary), "--config", configuration,
                "--target", "Multimedia", "--parallel", "4"], check=True, env=environment)
stamp.write_text(recipe_hash, encoding="ascii")
print("Built playback-only Qt runtime:", runtime)
