# ReDot Stage 4 — remaining original requirements

## Subsequent Settings accessibility verification

The deployed Settings inspection and labels/order correction are documented in [redot-stage4-accessibility.md](redot-stage4-accessibility.md). Current candidate: `build/redot-stage4-accessibility/Release/ReDot.exe`. Both tray checkboxes are first in Settings → General → Window behavior. Earlier delivery measurements and paths below remain historical evidence; the old Stage 4 and approved Stage 3 builds are preserved.

## Scope and preservation

Size sorting, existing tray completion, picker wording and current acceptance documentation only. Live scene-preview initialization remains approved; no automatic recording/replay, new startup automation, optimization stage or engine redesign. Stage 3's seven manual checks were all reported passed by the user. Historical reports keep their original results.

Recoverable checkpoint: `build/checkpoints/redot-stage4-20261010-134117/source.zip`. Approved fallback: `build/redot-stage3/Release/ReDot.exe`, SHA-256 `f9004eda4fb427e6d7ee525a84f6c79f995df18c706c0609446ab2e1b48767d7`. Candidate: `build/redot-stage4/Release/ReDot.exe`. Personal sessions, preferences, recordings and scene data are not replaced.

## Completed behavior

- Media offers Size (largest first) and Size (smallest first). Byte lengths already supplied by the bounded library worker are sorted in RAM; equal lengths sort by full path in both directions. No UI filesystem reads. Search/filters combine with either direction. External changes remain visible after Refresh; stale/missing records keep their last-known metadata until discovery removes them, and playback errors remain visible.
- Settings General has independent Minimize-to-tray and Close-to-tray preferences; minimize defaults off for old/new profiles. Both persist through the existing bounded atomic settings writer and can change while encoding without changing encoder configuration.
- One existing Qt tray object/menu, now with neutral ReDot icon. Text displays Idle, Replay running, Recording or Recording and replay running only when the existing confirmed engine mode permits it; startup, recovery and finalization are separately named. Command labels/enabled states follow the same controller/actions as Capture. The taskbar status implementation is unchanged.
- Minimize hides only when enabled and the notification area is available. Open restores normal/maximized state and geometry. Close-to-tray retains the existing backend; explicit tray Exit bypasses hide and drains recording and accepted exports through the existing shutdown path.
- Notification-area availability is checked with Qt's supported `QSystemTrayIcon::isSystemTrayAvailable`. When unavailable, minimize remains on the taskbar and Close exits safely. If it disappears while hidden, the main window resurfaces with show-without-activation. The reason is visible in General settings. No balloon/sound is generated.
- Application picker explains connecting the selected window to its scene source; no Stage 2 pending message remains. Association/discovery behavior is unchanged.

## Verification

Release and Debug compiled with MSVC x64 / Qt 6.8.3 in `build/redot-stage4`, with Glass Lab disabled. Complete quiet suites passed at `build/quiet-tests/run-20261010-135838-Release` and `build/quiet-tests/run-20261010-140245-Debug`. Release's final additional restart/Close-disabled checks passed using the same owned profile. Build and suite logs are under `build/redot-stage4`; pre-existing compiler warnings remain, with no compilation errors.

Quiet tray checks use an owned inactive desktop and a verification-only availability override, exercise actual Qt state events/menu actions, and never publish a tray icon in the personal Explorer notification area. They do not prove physical Explorer rendering or mouse interaction.

| Check | Observed result |
|---|---|
| Size sorting | Actual cached bytes agree with eight fixture files; both directions, combined search/filter, equal-size path ties and missing-file playback error pass. Equal-size/unavailable entries use metadata fixtures. |
| Tray lifecycle | Minimize off/on, normal/maximized restore and geometry, Close-to-tray, notification-area loss, one neutral Qt tray object, independent preference persistence and Close-disabled drain pass. Notification-area availability is simulated only for these owned tests. |
| Hidden encoding | Recording alone, replay alone and both remain hidden while encoded-frame counts increase. Restore retains the active worker. |
| Hotkeys | Existing production registration and conflict detection pass on the private desktop. A targeted `WM_HOTKEY` to the owned controller saves a clip while hidden. This is not a physical/global keypress test. |
| Explicit Exit | With Close-to-tray enabled, an active recording and accepted export finalize. The existing test hook delays that export by a bounded three seconds; final file count is checked after process exit, with no partial MP4. |
| Existing Debug/Release regressions | 33 MP4 decode passes per configuration, including named AAC streams, chronological timestamps and generated recording/replay; scene switching/composition, audio continuity/checkpoints, meters, faults, logging, settings, Media playback, five gears, focus policy and native taskbar status checks pass. 12 synchronization-report comparator cases and 15 playback-PCM comparator cases pass per build; those comparator inputs are simulated, not perceptual listening tests. |
| Scene/editor scaling | 104 checks at each Qt scale 1.0 / 1.25 / 1.5 pass. This does not change/test the personal desktop's Windows display settings. |

The first run (`build/quiet-tests/run-20261010-135115-Release`) is preserved with five failed checks. Four exposed a real Qt ordering problem: hiding synchronously in `WindowStateChange` was undone by `showMinimized()` completing its show operation. A coalesced deferred hide now rechecks state/preference/availability. The fifth exposed incorrect test sequencing: Save Clip posts a command, and the test requested Exit before that command ran. The corrected test waits for acceptance before testing drain. No recording/replay engine change was needed.

`build/redot-stage4/preservation.json` records the checkpoint comparison. No checkpoint file was deleted. Recording/audio/replay/compositor/model/editor, backdrop/chrome/material tokens, taskbar branding and VS Code configuration remain byte-identical. The Stage 3 fallback hash remains unchanged. The user's running Stage 3 session was not replaced or controlled.

Tested Release SHA-256: `7d21ccf0b7aecdffd937cd307b83ad836503bfc797078cd86ad680a48dbd845f`. The exact shipping Debug and Release executables were launched only on owned inactive desktops for smoke/controls checks. Real GD, audible source capture/listening, physical tray clicks/global keypresses, notification-area restart and Windows display-scale changes were not performed in this stage. Stage 3's seven user-approved physical checks remain prior evidence, not newly repeated Stage 4 measurements.

## Changed files

- `qml/Media.qml`: two size directions in the existing sort choice.
- `src/frontend_qt.cpp`: cached sorting, existing tray menu/status, hide/restore and notification-area handling, General availability text.
- `src/frontend_bridge.h`, `src/frontend_adapter.inl`: minimize preference state; verification-only targeted shortcut seam.
- `src/workspace_ui.inl`: independent persisted minimize preference and safe immediate General toggles.
- `src/application_picker.cpp`: current scene-connection wording.
- `src/frontend_qt.h`, `tests/stage4_checks.inl`, `tests/qt_frontend_checks.inl`, `tests/run_quiet_tests.ps1`: metadata fixtures, focused checks and post-exit finalization validation.
- `README.md`, `docs/redot-rebrand.md`, `docs/redot-stage4.md`: current delivery/acceptance and evidence. Original historical report results are retained.

## Launch

Keep the deployed DLLs/plugins/QML/licenses alongside the executable. After finishing your current session and closing it normally:

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\build\redot-stage4\Release\ReDot.exe'
```

## Manual acceptance

Finish ongoing personal recording/export before changing which build uses personal preferences. Launch the Stage 4 candidate manually; it is not started automatically by this delivery.

1. Media: compare Size largest/smallest, search/filter and equal-sized clips; try Refresh after an external file change.
2. General: toggle Minimize-to-tray off/on and Close-to-tray independently. Minimize, restore through neutral tray Open/double-click, including a maximized window. Confirm position/state and one tray icon.
3. Start recording plus replay, minimize to tray, inspect operational text, save a clip with tray or your shortcut, restore and stop. Listen to the saved named audio tracks.
4. With Close-to-tray on, use tray Exit during a short recording and an accepted save. Relaunch and verify both finalized files. Close-to-tray off must perform a real exit.
5. Check the unchanged taskbar dot and two-monitor focus appearance. No physical Explorer interruption is needed; unavailability is fault-tested only in owned verification.

Stage 4 still needs user manual approval. Long-session performance/reliability remains the next stage after approval, not part of this delivery.
