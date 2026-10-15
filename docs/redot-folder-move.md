# Moving the workspace folder from PandaClip to ReDot

The project is renamed in source and configuration. The folder itself is **not** renamed by Claude Code because the active session, VS Code and build tools hold the old path. Do this once, manually.

Old: `C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip`
New: `C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\ReDot`

## Before the move

1. Close ReDot (finish any save in progress) and every `build\*\Release\ReDot.exe` you started.
2. Exit the Claude Code session in this folder and close VS Code / any terminal whose current directory is inside it.
3. Pause OneDrive sync (tray icon -> Pause syncing) or wait until it reports "Up to date". `build` is about 67 GB; the rename is a single folder rename and does not re-upload content, but a running sync can lock files.
4. Optional safety copy: `build\checkpoints\20261011-pre-rename\` holds the pre-rename source zip, a copy of `%LOCALAPPDATA%\PandaClip` and SHA256 lists.

## Move

PowerShell, from a directory *outside* the project:

```powershell
Set-Location 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid'
Rename-Item -LiteralPath 'PandaClip' -NewName 'ReDot'
```

(Or rename the folder in File Explorer.) If it reports that the folder is in use, a process still has it open; close it and retry. Do not copy and delete.

## After the move

1. Resume OneDrive and let it settle.
2. Open VS Code on the new `ReDot` folder; start Claude Code from that folder:

   ```powershell
   Set-Location 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\ReDot'
   claude
   ```

   Claude Code keys its conversation history and memory by directory, so the new folder starts a fresh history. The old one remains under `C:\Users\eyald\.claude\projects\` and can be copied or resumed manually if wanted.
3. Do not reuse old CMake caches: every `build\*` directory created before the move records the old absolute path and CMake will refuse to configure them. The prebuilt executables are standalone and still run:

   ```powershell
   & 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\ReDot\build\redot-scenes-preview\Release\ReDot.exe'
   & 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\ReDot\build\redot-final\Release\ReDot.exe'
   ```

4. Configure and build into a fresh directory:

   ```powershell
   cmake -S . -B build/redot-moved -G 'Visual Studio 17 2022' -A x64 -DREDOT_BUILD_REPLAY_STRESS=ON
   cmake --build build/redot-moved --config Debug --parallel
   cmake --build build/redot-moved --config Release --parallel
   & tests/run_quiet_tests.ps1 -BuildDirectory build/redot-moved -Configuration Release -SkipBuild
   ```

   The first Qt playback-runtime step reuses `build\qt-output-runtime-ninja`; if it complains about paths, delete only that directory and let it regenerate (the Qt sources archive is under `third_party\Qt\sources`).
5. For F5 debugging, the "ReDot (Debug)" launch entry uses `build/production/Debug/ReDot.exe`. `build\production` predates the move, so first run `cmake -S . -B build/production ...` after removing only `build\production\CMakeCache.txt` and `build\production\CMakeFiles`, or change the launch `program` to your new build directory.
6. Your data does not move: settings, scenes and layout stay in `%LOCALAPPDATA%\PandaClip\` and recordings stay where they are.

## Rollback

Rename the folder back (`Rename-Item ... -NewName 'PandaClip'`). Source can also be restored from `build\checkpoints\20261011-pre-rename\source-pre-rename.zip`.
