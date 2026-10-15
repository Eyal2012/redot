# Canonical appearance and popup stability — 2026-10-09

## Delivery and recovery

The tested Release is `build/appearance-final/Release/PandaClip.exe` (954880 bytes), SHA256 `ABC86A9EACB85AE9BEF26048838CD59472BEAFCC79F85C4CA7F5F9A787D2B27F`.

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\build\appearance-final\Release\PandaClip.exe'
```

Debug and Release were built with MSVC/CMake using the existing Qt 6.8.3 toolchain; Glass Lab is disabled. Logs are `build/appearance-checks/build-delivery-debug.log` and `build-delivery-release.log`. Existing production and interim review processes remain running and were not replaced or terminated. VS Code tasks/launch settings remain unchanged; rebuilding through them still uses `build/production`.

The recoverable pre-edit checkpoint is `build/checkpoints/appearance-stability-20261009/source.zip`. Extract its source entries into a separate folder inside this project to compare or selectively restore; do not blindly replace settings, output or evidence. No recordings/preferences/evidence were deleted. Engine/VS Code byte comparison results are retained in `build/appearance-checks/preservation.json`.

## Confirmed causes and corrections

- Activation previously selected different panel fills as well as a different window backdrop. The user confirmed active controls looked fine but tabbing away produced another appearance on the interim build. Panel/control material parameters are now invariant; only the root backing switches between compositor tint and opaque dark fallback. Solid accessibility mode retains the same design. Final user confirmation is pending.
- Quick Settings were native popup windows with their own backdrop/material surface. They are now one bounded nonnative child overlay. Nested QQuickWidget backing produced a rectangular opaque area beyond the rounded panel; that backing was removed. Its panel is painted directly with shared tokens. A diagnostics `winId()` call could create an unintended child HWND; it now reads `effectiveWinId()`.
- Some native Qt dropdown list windows lacked the logical root HWND owner. Actual targeted popup checks failed before the ownership fix. The shared backdrop helper gives those owned lists the correct HWND parent. Root-owner activation family tracking excludes unrelated same-process windows. No repeated activation call is used.
- Immediate deactivation dismissal raced native dropdown construction and canceled it. Dismissal now waits briefly for popup creation to settle. Escape, outside dismissal, bounded Tab navigation and guarded focus restoration remain.
- Native preview HWNDs cannot be covered by ordinary Qt child painting. Only the preview's visible region is clipped under an open overlay and restored on close. Capture/encoding continue; no preview readback or background capture was added.
- The outer Qt rim, inset caption rim and DWM contour overlapped. DWM now supplies the single rounded outer edge and subtle shared border, with square maximized bounds. Qt's redundant window/caption rims are removed. Redundant GPU control borders were also removed where Qt controls already paint their own edges.
- Shader color properties are premultiplied; rim RGB/alpha handling was corrected to avoid dark/faint edging. This changes the existing foreground shader, not its background source.

`qml/design-system.json`, shared Qt/QML controls and shared captions remain canonical across Capture, Media, Settings, Diagnostics and popovers. Production exposes no alternate theme/preset/material renderer controls. Legacy preference keys are ignored, then omitted by the normal settings whitelist. Recording/audio/replay settings and accessibility preferences remain. Windows client animation settings are honored in addition to PandaClip Reduce Motion.

## Performed verification

| Check | Result / evidence |
| --- | --- |
| Final Release full owned-desktop UI/engine checks | 399 PASS lines, zero FAIL; `delivery-qa-100/checks.txt` |
| MP4 regression artifacts | All 14 files decoded, zero verifier failures; `delivery-qa-100/decode.json` |
| Recording/replay regression | Generated GPU/PCM recording, shared replay worker, export/finalization, live mute, meter delivery, playback, error/retry paths passed; fixture results are not game/audio listening proof |
| Five gears, native choices, Escape/outside dismissal, focus and scroll | Passed private-desktop targeted checks, including native choice root ownership |
| Final dashboard scaling | Zero failures at 96/120/144 DPI-equivalent Qt scale; `delivery-qa-100`, `delivery-dashboard-125`, `delivery-dashboard-150` |
| Exact shipping external screenshots | Capture/Media normal/small, all gears before/open/closed, maximize/restore at 100/125/150% Qt scale; `delivery-shipping-100`, `-125`, `-150` |
| Passive foreground preservation | Each final shipping run observed owned foreground activation = 0; `passive-input.txt` |
| Debug launch | Final deployed Debug smoke passed; `delivery-debug-smoke` |
| Settings/Diagnostics | All seven categories, wheel over editable controls, small viewport reachability, diagnostics and caption geometry checked on owned desktop |
| Removed preference values | Legacy theme/preset/material values did not restore an alternate style and were stripped on Apply |

The lightweight UI polling measurement in the fixture session was mean 1653 microseconds, max 4119 microseconds, 376 observations. This measures that callback only; it is not a general responsiveness benchmark or gameplay performance result.

Scaling checks used process-local `QT_SCALE_FACTOR`, not a change to the user's actual Windows monitor scaling. Native/client dimensions and work-area bounds were checked. Targeted move-loop checks canceled only the owned process's loop; physical dragging, snapping and mixed-monitor DPI remain manual.

Actual external images:

- Before: `build/appearance-checks/before/capture-blue.png`, `media-blue.png`.
- Final: `build/appearance-checks/delivery-shipping-100/capture-blue.png`, `gear-0-open.png`, `gear-0-closed.png`, `media-blue.png`, `capture-maximized.png`.
- Each scaling directory contains every gear and resized Capture/Media images. These are deployed HWND/compositor evidence, not internal QML renders. Inspection found no rectangular overlay backing or doubled white framing. They are inactive screenshots and do not prove active Acrylic transmission or active/inactive perceptual stability.

Earlier failed results were retained: native choice ownership failures, a verification assertion that incorrectly searched QObject ownership instead of the visual QML tree, and an executable-lock build attempt. The corrected visual-tree check reports fixed panel materials. Final builds/tests listed above supersede those failures.

### Passive test guard incident

An early physical-desktop harness run reused a recording-button test intended for a missing source. Window discovery selected Geometry Dash and the harness's own PandaClip process recorded 1.933 seconds of video-only capture. It did not inject input, activate a window, control or stop the user's app. The file and logs remain under `build/appearance-checks/passive-100`. This was unintended, is not gameplay validation, and is not used as visual evidence. The harness guard was corrected: subsequent physical-desktop appearance runs exclude capture/record actions. Engine regression runs use isolated generated fixtures. No user-running process was terminated.

## Remaining manual confirmation

Run `tools/review_appearance.ps1` for an isolated final-build profile. Click that window yourself, open Recording/source choices and Escape, then tab away and return. The desktop backdrop may become solid while inactive; controls and panels must retain their palette/material. The earlier user review was on the interim executable, before the final inactive-material correction. Active Acrylic transmission and final active-to-inactive visual stability have **not** been independently verified.

Also check physical dragging/snapping, real 100/125/150% Windows display scaling and a gear over moving live preview. The rectangular native-preview exclusion may reveal a small backing area at the popup's curved corners; automated fixtures do not establish its appearance over active GD. Audible microphone speech, listening to individual tracks, actual moving game performance and perceptual audio/video sync were not revalidated by this appearance task.

Desktop blur remains supported DWM Acrylic; no custom desktop refraction, screenshot wallpaper or monitor capture draws the UI. An accepted API request is not proof of visible transmission. Solid fallback and system accessibility remain automatic and honest.

Implementation references: [DWM window attributes](https://learn.microsoft.com/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute), [Qt ShaderEffect color semantics](https://doc.qt.io/qt-6/qml-qtquick-shadereffect.html), [Qt QWidget window and popup behavior](https://doc.qt.io/qt-6.8/qwidget.html).
