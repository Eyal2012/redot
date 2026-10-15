# Focus-independent Acrylic — 2026-10-09

## Delivery

`build/focus-dwm/Release/PandaClip.exe`

SHA256: `1E63C71108D891A404E6CA7D26912A779C0DFAE887F4D2D809B422FD02662727`

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\build\focus-dwm\Release\PandaClip.exe'
```

The workspace was verified before editing. Recoverable pre-edit source is in `build/checkpoints/focus-acrylic-20261009/source.zip`. The delivery uses a separate build directory; no existing executable, personal settings, recordings or running session was replaced or controlled.

## Confirmed implementation causes and fix

The previous helper set `desktopBackdropMaterial` from `accepted && active`. When an unrelated application gained focus, including one on a second monitor, that explicitly selected the solid Qt root. The previous inactive screenshot displays this branch. This was application behavior, not evidence of an unavoidable Windows restriction.

The custom-caption `WM_NCACTIVATE` handler also forwarded the requested inactive state to default nonclient processing. Removing only the Qt solid branch did not establish stable native blur. The final handler maintains the same nonclient visual paint state while **explicitly returning TRUE for deactivation**. Real `WM_ACTIVATE`/`WM_ACTIVATEAPP` processing remains normal. It does not call SetActiveWindow, SetForegroundWindow or force Qt activation. Microsoft documents [WM_NCACTIVATE](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-ncactivate) as nonclient activation painting; returning TRUE allows a requested activation change, while `lParam=-1` avoids default nonclient repaint. Stable DWM blur from this paint policy was visibly observed here; the documentation does not promise identical Acrylic behavior on every Windows version.

`DesktopBackdrop` now chooses root alpha/material from accepted supported policy, independently of focus. Focus remains diagnostic information. Native and Qt activation notifications still coalesce policy refresh. Windows Transparency effects, high contrast, unavailable composition/backdrop support and explicit Reduce Transparency retain their readable fallback. The misleading “Inactive · solid appearance” status was removed.

The existing supported [DWM system backdrop](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute), margins and shared material/control tokens remain. No new renderer, desktop sampling, whole-window opacity or experimental Glass Lab component is in production. Blur changes naturally with the physical background; this fix preserves the material and controls rather than freezing a picture.

## Checks actually performed

| Check | Result and evidence |
| --- | --- |
| Debug and Release builds | Successful; `build/focus-dwm/build-debug.log` and `build-release-final.log`. Dynamic Qt deployment retained. |
| Exact shipping Release launch | Passed with an SDK-free PATH and isolated profile. Debug smoke also passed. |
| Quiet UI/engine regression | 414 PASS checks, zero failures at 96 DPI: `build/focus-checks/regression-prepared/checks.txt`. Uses generated GPU/PCM fixtures and a private inactive desktop. Includes recording/replay sharing, finalization, meters/mute, Media playback, seven Settings categories during recording/replay, five gears, native choices, errors/retry and caption/minimize/maximize behavior. |
| MP4 decoding | All 14 artifacts decoded successfully: `regression-prepared/decode.json`. Eight are prepared owned fixtures; six were newly produced by the run. This is not a real gameplay or audible microphone test. |
| Deactivation acceptance | Owned HWND `WM_NCACTIVATE(FALSE)` returns TRUE; `WM_ACTIVATE` and `WM_ACTIVATEAPP` notifications leave actual foreground unchanged. Material remains selected. |
| Actual deployed inactive Acrylic | Initial controlled screenshots show blurred owned background shapes/colors behind sharp text and controls. Different blue/red background HWNDs at identical host geometry produce different compositor output. No internal QML grab was used as transmission proof. Later snapshot backing-scene inconsistencies are excluded from this claim, as described below. |
| Screenshot comparison | In a static background ROI `(400,240)-(850,300)`, RGB difference after deactivation notifications is exactly zero. Switching blue/red backing windows yields mean absolute RGB difference `[76,2,15]`. `verified-focus-final/pixel-comparison.json`. This sampled-region check is not proof of the user's physical focus sequence. |
| Gears and Settings | All five shipping gears opened/closed via owned UIA. Settings has an actual external inactive screenshot. The dashboard-with-Settings-open screenshot was skipped by the overlap guard; it is not marked passed. Later screenshots with uncertain backing-scene attribution do not prove transmission from the intended fixture. |
| Resize/maximize/restore | Owned shipping HWND and UIA resize checks passed. Capture client 860×620, Media 720×540, maximized Capture 2560×1390 work area; restored successfully. No physical drag/snap performed. |
| Quiet isolation | Final `passive-input.txt` reports zero observed foreground activation. No cursor movement, global keys, sounds or personal browser control. Physical-desktop checks never started capture/recording. |
| Preservation | Engine/audio/replay/settings-store, CMake and VS Code configuration byte comparisons unchanged: `build/focus-checks/preservation.json`. |

Actual compositor screenshots in `build/focus-checks/verified-focus-final`:

- `capture-blue.png`, `capture-deactivated.png` — same Qt material before/after notifications, already inactive owned test window. The intended backing scene was not reliable in later repeats; these are not controlled-source transmission proof.
- `shipping-compositor.png`, `shipping-compositor-changed.png` — same geometry, different real owned background windows.
- `gear-0-open.png` through `gear-4-open.png`, corresponding closed images.
- `settings-deactivated.png`, `media-blue.png`, `media-red-resized.png`.
- `capture-red-resized.png`, `capture-maximized.png`, `capture-restored.png`.

Previous inactive-solid screenshot: `build/appearance-checks/delivery-shipping-100/capture-blue.png`. It is historical evidence, not an active/inactive physical-focus comparison with the new binary.

## Investigation and test caveats

A supported Windows App SDK DesktopAcrylicController trial was rejected: legacy target composition collided with Qt's existing DComp target or covered the foreground; a modern adapter failed with a stowed HRESULT 0x80070057. Trial code and dependencies were moved under `build/focus-checks/retired-controller`, and intermediate source/evidence retained under build. They are not production dependencies. Launch only the delivery identified above.

The first full regression attempt used an empty profile without the required eight Media fixtures and timed out in that phase. The prepared repeat passed. This prerequisite failure remains in `build/focus-checks/final-regression`.

Early passive snapshots had insufficient backing-scene margins/settling and some unprepared profiles could discover the normal Videos library. They were not used as final controlled-background evidence. The harness now prepares project-local output/preferences, covers a 96-pixel sampling margin, waits for composition, and rejects overlapping unrelated windows. Later repeats (`verified-focus-order` and `verified-covered-scene`) still showed backing-scene inconsistencies after changing/closing backing windows despite those precautions. Those images are not used as controlled-source transmission proof. The accepted initial same-geometry comparison is `verified-focus-final/shipping-compositor.png` versus `shipping-compositor-changed.png`; the physical focus sequence remains pending. A same-window occluded GDI tint repaint did not change the captured scene in one trial; final background comparisons therefore switch two real owned background HWNDs. That failed repaint trial does not establish live-update cadence. No personal recordings or preferences were modified.

## Remaining physical check and limitations

**Not yet verified:** clicking PandaClip on monitor 1, then YouTube on monitor 2, and returning, with menus/settings open. Quiet automated checks intentionally never changed real foreground focus, so they cannot claim this user action passed. Nor were new physical 125%/150% scaling, drag/snap, moving GD, microphone speech or listening checks performed.

Run the isolated review (no initial replay/audio; separate preferences, no automatic input):

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\tools\review_appearance.ps1'
```

Click the review window on monitor 1, click your browser on monitor 2 without minimizing PandaClip, then click back. Repeat with Recording gear, source dropdown and Settings open. The same glass/material must remain. This manual result was requested and remains pending.

Windows power, accessibility, remote-session and compositor policies may still restrict native blur. No unsupported native composition API or captured-background substitute is used. An accepted request is not presented as visual proof on another machine. Native Acrylic is blur/tint, not custom desktop refraction.
