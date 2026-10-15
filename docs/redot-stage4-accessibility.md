# ReDot Stage 4 — Settings accessibility and deployed verification

## Findings and correction

Workspace verified: `C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip`. Checkpoint: `build/checkpoints/redot-stage4-accessibility-20261010-1421/source.zip`.

The previous Stage 4 Release did contain both checkboxes in General. An external screenshot of its actual Settings HWND showed them fully visible at normal size, under the output-folder actions. They were called **Closing the window / Keep capture running in the tray** and **Minimizing the window / Hide in the system tray (default: off)**. There was no collapsed section or clipping in that observed window. The production Qt frontend omitted the adapter's WINDOW AND DEFAULTS heading because its heading parser only displayed headings containing ` | `. The controls were not prominently named like the documentation.

There was also a build-selection problem: the user's only personal ReDot process at the beginning of this check was `build/redot-stage3/Release/ReDot.exe` (PID 13144). It was left running untouched. `tools/review_appearance.ps1` also still selected Stage 3. That older binary lacks the new minimize option. This is evidence about the observed process/script, not an assumption about every earlier window the user reviewed.

Small production change: reuse the existing checkboxes at the top of **Settings → General → Window behavior**, explicitly named **Minimize to tray** and **Close to tray**. Other settings, page styling, engine settings and recording UI are unchanged. The appearance-review script now selects the current candidate. No source/recording/replay architecture or startup behavior changes.

Both settings save immediately through the existing debounced/atomic preference path; **Apply settings is not needed** for these two switches. Their mappings remain `setting2039 → minimizeTray` and `setting2038 → closeTray`. Missing keys in old profiles still mean off. There is no preference migration or storage relocation.

## Actual screenshots

These are bounded external compositor screenshots of the exact deployed Release Settings HWND over an owned background, not QML/widget grabs. Both test and background windows were nonactivating and mouse-transparent. No personal desktop content was saved. Observed owned foreground activation was zero for all three runs.

- [Before: old labels and placement](../build/stage4-accessibility/before/settings-deactivated.png)
- [100% Qt: normal 900×700](../build/stage4-accessibility/verified-100/settings-normal.png) · [minimum 640×480](../build/stage4-accessibility/verified-100/settings-small.png)
- [125% Qt: normal 1125×875 physical](../build/stage4-accessibility/verified-125/settings-normal.png) · [minimum 800×600 physical](../build/stage4-accessibility/verified-125/settings-small.png)
- [150% Qt: normal 1350×1050 physical](../build/stage4-accessibility/verified-150/settings-normal.png) · [minimum 960×720 physical](../build/stage4-accessibility/verified-150/settings-small.png)

Both controls were visible, enabled and inside the window bounds at each size, without scrolling. The tester queried production `WM_GETMINMAXINFO` geometry to obtain Qt's actual scale/minimum size; it did not infer Qt scale from Windows DPI. Qt factors 1 / 1.25 / 1.5 were tested without changing global Windows display settings.

## Methods and evidence

Windows UI Automation invoked the shipping Settings navigation action, verified General's SelectionItemPattern, discovered the two shipping CheckBox elements and operated their TogglePatterns. It checked all four on/off combinations against the isolated persisted file, then reset them through the controls. The same test runs on an inactive owned desktop in both full suites. No test checkbox/model substitutes are used for these UIA checks.

Quiet tray lifecycle/encoding checks now change the real Qt General checkboxes rather than writing their internal preferences directly. They exercise actual window minimize/hide/restore events and tray QAction commands, with a verification-only notification-area availability override. They publish no Explorer icon. Generated GPU/PCM fixtures enter the existing scene compositor and real NVENC/AAC/mux/replay workers. Those checks prove app behavior under that fixture; they do not prove physical tray clicks or gameplay capture.

## Test matrix

Status **Passed** means the stated automated method passed. **Not Verified** identifies the physical or perceptual part that the safe method does not establish.

| Requested check | Status | Method / boundary |
|---|---|---|
| Minimize setting exists and is easy to find | Passed | Actual Release UIA and external HWND screenshots; first section in General. |
| Close setting exists and is easy to find | Passed | Actual Release UIA and external HWND screenshots; first section in General. |
| Normal and minimum size, 100/125/150% Qt | Passed | Actual Release at all six sizes; UIA bounds/enabled/offscreen checks, native minimum geometry. |
| Correct saved keys; independent preferences | Passed | Actual shipping TogglePatterns and persisted 00/01/10/11 combinations at every scale; separate restart checks. |
| Largest-first file size | Passed | Actual worker metadata from eight fixture files, production MediaController ordering. |
| Smallest-first file size | Passed | Same metadata and reverse ordering. |
| Size sorting with search and filters | Passed | Both directions combined with microphone search and replay filter. |
| Equal-size sorting | Passed | Equal-size metadata fixture; deterministic full-path ties in both directions. |
| Missing sorted file | Passed | Unavailable-path fixture; production playback reports missing/unavailable safely. |
| Existing clip playback | Passed | Existing Qt Media fixture playback, decoded frame/track/seek/pause checks; silent verification. |
| Minimize-to-tray off | Passed | Actual Qt/native minimize event retains visible minimized window. |
| Minimize-to-tray on | Passed | Actual checkbox and Qt hide transition with private availability override. |
| Close-to-tray on | Passed | Actual checkbox/close event hides without shutdown under private availability override. |
| Close-to-tray off | Passed | Restored process requests real drain/exit; shipping owned profile exits successfully. |
| Normal geometry restoration | Passed | Actual Qt hide/Open events and normalGeometry equality. |
| Maximized restoration | Passed | Actual Qt maximize/minimize/hide/Open events retain maximized state. |
| Recording continues hidden | Passed | Real worker encoded-frame count advances; recording finalizes/decode passes. Generated inputs. |
| Replay continues hidden | Passed | Real worker advances and exports while hidden. Generated inputs. |
| Both enabled while hidden | Passed | Shared encoder advances; clip saves and ordinary recording remain active. |
| Save Clip hotkey registration/conflict | Passed | Production RegisterHotKey on the owned inactive desktop; owned conflicting registration denied. |
| Save Clip hotkey handler while hidden | Passed | Targeted WM_HOTKEY to owned controller produces a finalized clip; no global injection. |
| Actual physical global hotkey dispatch | Not Verified | No global key injection; personal existing bindings/session remain untouched. |
| Save Clip tray command | Passed | Actual tray QAction command produces MP4 through the worker. |
| Tooltip/menu idle/replay/recording/both/error states | Passed | Confirmed engine-mode snapshots and live owned encoding sessions; startup/drain/failure do not claim running. |
| Tray icon stays neutral | Passed | One QSystemTrayIcon keeps its neutral resource/cache key through operational changes. |
| Tray Exit finalizes recording and accepted save | Passed | Real shutdown/drain with Close-to-tray on, bounded three-second export delay; final count checked after exit; no partial MP4. |
| No duplicate tray objects/unexpected termination | Passed | One Qt tray object, owned lifecycle checks, normal process exits and finalized files. |
| Explorer tray rendering, clicks and duplicate shell icons | Not Verified | Quiet tests suppress shell publication. No secondary Explorer shell is started, and no personal shell is controlled. |
| Notification-area loss handling | Passed | Private availability fault; app resurfaces without shutdown. Physical Explorer restart not performed. |
| Capture and Scenes previews | Passed | Owned generated GPU inputs and actual Qt/native preview presentation checks. |
| Scene switching | Passed | Existing scene integration and decoded-pixel/audio-routing fixtures. |
| Recording/replay MP4 validity | Passed | Independent Media Foundation decoding of every fixture stream. |
| Audio tracks and continuity | Passed | Named AAC streams, timestamps and generated PCM continuity/checkpoint regressions. |
| Audible microphone/game audio, perceptual sync | Not Verified | Quiet tests do not listen/play stimuli or operate active Geometry Dash. Prior Stage 3 manual acceptance is retained. |
| ReDot taskbar color state/native icons | Passed | Existing engine-mode/native WM_GETICON tests; neutral/replay green/recording red priority. |
| Physical taskbar rendering this pass | Not Verified | No personal foreground or shell manipulation; prior user Stage 3 tests passed. |
| Existing scenes/settings persistence | Passed | Scene/model restore and full Qt process restart checks, including both tray preferences. |
| Acrylic/shared appearance policy | Passed | Existing focus/native-policy/alpha/material checks; deployed passive screenshots inspected. Material/backdrop/chrome source unchanged. |
| Active Acrylic transmission/two-monitor focus visually | Not Verified | Actual focus transitions would require personal interaction; API acceptance and inactive screenshots are not transmission evidence. |

## Builds and complete suites

MSVC x64 / Qt 6.8.3 Debug and Release compiled successfully. Glass Lab remains off. Pre-existing compiler warnings remain; the final builds have no compilation errors. Full suites passed at `build/quiet-tests/run-20261010-143131-Release` and `build/quiet-tests/run-20261010-143416-Debug`: **33 MP4 decode passes per build**, existing scene/editor checks at all three Qt scales, audio/logging/fault/PCM regressions, and the Stage 4/UIA checks. No final test reports a failure. Build/test logs are under `build/stage4-accessibility`.

Tested Release SHA-256: `4a294fbf7cd9b8d438812afeba5aa782d890af01e69ced3dfda07682ae70fdbd`. `build/stage4-accessibility/verification-summary.json` collects the final pass counts; `preservation.json` records unchanged protected source files and earlier executable hashes.

The new UIA checks were repeated in the full suites at all three Qt scales. Independent passive Release checks and their reports are under `build/stage4-accessibility/verified-100`, `verified-125`, and `verified-150`; each reports zero failures and zero observed owned foreground activation.

The first UIA trial at `build/stage4-accessibility/after-100` is preserved. It expected an explicit `minimizeTray=0` key in a legacy profile where the unchanged off checkbox had correctly caused no save. The tester now honors the existing absent-key/off default and still verifies both explicit keys after toggling. That trial also used Windows DPI to size the window; the final test queries the actual production Qt/native minimum and reaches the true minimum. No production persistence change was needed.

## Changed files and preservation

- `src/frontend_qt.cpp`: existing checkbox labels and placement only.
- `tests/stage4_checks.inl`: operate actual General checkboxes for owned tray lifecycle checks.
- `tests/stage4_settings_uia.h`, `tests/qt_quiet_launcher.cpp`: bounded shipping-UIA inspection and passive screenshots, owned HWNDs only.
- `tests/run_quiet_tests.ps1`: shipping Settings checks at three scales in both complete suites.
- `tools/review_appearance.ps1`: current candidate path; no automatic personal launch.
- `README.md`, `docs/redot-stage4.md`, this report: exact navigation/build/evidence.

The source checkpoint and both earlier Stage 3/4 Releases are preserved. Recording, replay, audio continuity, compositor/editor/model, branding/taskbar, DWM/material policy, and VS Code configuration have not been changed. No recordings, user preferences or personal scene files are overwritten. All generated artifacts stay under this workspace's build directory. Final optimization/reliability work has not started.

## Exact launch and navigation

After finishing the existing personal recording/replay/export and closing that instance normally, launch the tested candidate:

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\build\redot-stage4-accessibility\Release\ReDot.exe'
```

Keep the deployed DLLs/QML/plugins/licenses alongside it. **Settings** in the main navigation → **General** (first category) → **Window behavior** (first section) → **Minimize to tray** or **Close to tray**. Both are independent and save without Apply. Changing them does not restart recording/replay.

Only physical shell/global-key and listening/gameplay checks remain unverified by this safe pass. Stage 4 awaits user acceptance; there is no request to repeat the automated checks above.
