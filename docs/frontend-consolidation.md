# Production frontend consolidation — October 9, 2026

## Build to use

`build/production/Release/PandaClip.exe` is the current production application. Historical executables/evidence and Glass Lab were preserved. Launch from PowerShell:

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\build\production\Release\PandaClip.exe'
```

Keep its deployed DLLs, QML/plugins and licenses. VS Code task labels and F5 behavior are unchanged; paths now point to this production directory. Only PandaClip is built by the ordinary Debug/Release tasks. Qt is mandatory, and the Glass Lab option defaults off. Verification executables in this directory are test tools, not alternate user frontends.

## Inventory and checkpoint

The workspace was verified before editing. No Git repository was present. A source checkpoint was created first: `build/checkpoints/frontend-20261009-021302/source.zip` (16,471,770 bytes), with `manifest.json` and `RESTORE.txt`. It preserves source, QML, tests, tools, lab, documentation and VS Code files before consolidation. SDK/dependency originals, recordings, preferences and earlier evidence were not changed/deleted. Stale generated Qt deployment files were moved to recoverable `build/checkpoints/retired-qt-*` folders.

| Actual production responsibility | Files retained |
| --- | --- |
| Capture, lifecycle, controller state | `src/main.cpp`, `src/workspace_ui.inl` |
| NVENC / synchronized WASAPI / compressed replay / muxing | `src/recording.*`, `src/audio_capture.*`, `src/replay.*` |
| Explicit backend adapter, hidden HWND state | `src/frontend_adapter.inl`, `src/frontend_bridge.h` |
| Capture dashboard, Settings viewport, Diagnostics, five popovers | `src/frontend_qt.cpp`, `src/window_chrome.h` |
| Separate Media library/player | `qml/Media.qml`, Qt Multimedia `MediaController` |
| Every window's compositor policy | `src/desktop_backdrop.h` |
| Canonical typography/palette/material/control tokens | `qml/design-system.json`, `src/design_system.h`, `qml/GlassTokens.qml` |
| One GPU material and Qt/QML control family | `qml/GlassPanel.qml`, `qml/desktop.frag`, `qml/WorkspaceGlass.qml`, `qml/controls.qss`, `qml/GlassButton.qml`, `qml/GlassChoice.qml`, `qml/GlassSlider.qml` |

The hidden controller controls are still state adapters; extracting them into a pure backend model is future internal work. Their obsolete painter, layout/scroller, hover animations, Mica header and visible message-loop frontend are removed. They are never offered as a UI fallback. Native capture notifications still target that controller HWND. The capture/recording/audio/replay implementation files and `settings_store.h` are byte-identical to the checkpoint; see `build/consolidation/preservation.json`.

## Retired production paths and dependencies

Reference/target audits preceded removal:

- Separate `DesktopPanel.qml` material: merged into `GlassPanel` for both Widgets' GPU layers and Media.
- Former sampled-video `glass.frag`, `ShaderEffectSource` and `MultiEffect`/QtQuick.Effects material: removed. Playback has a readable shared panel without video sampling; outer Media never uses a thumbnail background.
- Duplicated Media inline pill/choice/slider implementations: replaced by reusable `GlassButton`, `GlassChoice`, `GlassSlider` with shared tokens and Reduce Motion binding.
- Competing hardcoded palette/caption/dock styles: use the shared JSON/QSS roles. Local size/layout overrides remain where the dashboard requires them.
- `PANDACLIP_QT_FRONTEND` build choice and Qt-off Win32 frontend: removed. Old native gallery, owner-painted thumbnail cards, fullscreen player/layout and MFPlay production class/link are retired. The historical MFPlay fixture is isolated in `tests/legacy_clip_player.h`, used only by the optional service test.
- Old Win32 UI verification target/runner branch: excluded from the current build/runner. Historical source/evidence remain, rather than becoming another production launch option.
- Unused local-glass feature/property and shader-sampling controls: removed. Production has no adjustable desktop refraction or custom monitor rendering flags.
- Production QML imports **QtQuick.Controls.Basic** explicitly. The import scanner's `deployment-before.txt` / `deployment-required.txt` prove that eleven optional style/Effects DLLs and their six style directories plus Effects are no longer required. Deployment cleanup moves stale generated files to a checkpoint. `Qt6QuickEffects.dll` is absent from the Release deployment. Basic, Templates, Controls implementation, Shapes (a Basic dependency), Layouts and Multimedia are retained.

Glass Lab sources/targets remain optional and separate, off for this build. Its `CreateForMonitor` source and custom blur/refraction shaders are neither compiled nor linked into PandaClip. Recording's own WGC/D3D11 capture is preserved. Source/resource audits found no production background sampler or desktop acquisition path. `build/consolidation/production-dependencies.txt` confirms no MFPlay import; dynamic Qt is retained with notices/LGPL text and unmodified corresponding sources.

## Material and native rendering repairs

Every production page/popover uses one neutral dark-purple palette, thin lavender outlines, restrained highlights and common foreground controls. The dashboard arrangement and native D3D11 preview remain. Qt draws the custom caption client; DWM supplies native active-window Acrylic, not a UI capture texture or whole-window opacity.

Initial actual compositor screenshots exposed classic native frame painting: restoring Windows sizing styles allowed non-client paint/activation to draw a thick white rectangle despite zero caption margins. The shared chrome now handles `WM_NCPAINT` and calls `DefWindowProc(WM_NCACTIVATE, ..., -1)` without frame repaint. DWM's native border is suppressed because Qt draws its own thin edge. Resize/caption/maximize hit testing, sizing/system styles and monitor bounds remain.

The first passive active-desktop screenshots also showed Windows' **inactive** Acrylic backing as grey. Qt Quick's activation state was insufficient evidence of native foreground activity. Material eligibility now uses the actual foreground HWND and refreshes on native activation/policy changes. Inactive windows deliberately paint the matching dark solid fallback. Windows transparency was enabled (`EnableTransparency=1`), high contrast off, compositor active, Acrylic request accepted; those facts do **not** verify transmission. Current passive tests never activate the app, so active-window Acrylic transmission remains unverified. No global settings were changed. Activate the app manually to inspect blur over two backgrounds.

This follows [Microsoft's custom-frame guidance](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe) and [documented DWM attributes](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute). It is compositor blur/tint, not desktop refraction.

## Checks actually performed

| Check | Actual result / evidence |
| --- | --- |
| Builds | Debug and Release PandaClip succeeded; SDK-free Debug launch smoke passed (`debug-smoke`). MSVC 2022, SDK 10.0.26100, Qt 6.8.3. Shader build now uses quoted explicit dependencies rather than Qt 6.8's malformed space-containing depfile; SDK files remain unmodified. Vulkan header detection is informational; the app uses D3D11. |
| Exact deployed shipping controls | `shipping-controls-final`: all five gears open/dismiss via bounded UI Automation on an inactive owned desktop; missing-source action returns normally. SDK paths are removed from the child environment. Not a WGC/gameplay claim. |
| Full focused frontend regression | `ui-final-basic`: **376 passing assertions, zero failures**, DPI 96. Capture/preview presentation, repeated start/stop and first-frame timeout/retry, recording/replay/shared exports, settings while encoding, live mute atomics/raw meter delivery, popover reachability/wheel/Escape/outside dismissal, presets/settings persistence, silent Media playback/seek/track change/fullscreen and native caption/client geometry. Generated GPU input for video; no gameplay or listening claim. |
| Short encoding / exported files | Direct-NVENC GPU/PCM fixture creates video-only/game-only/mic-only/both ordinary/replay files. Frontend test adds short ordinary/replay/shared/static-retry sessions. All **14 finalized files** in final output decode independently: eight copied fixtures + six new frontend outputs. H.264 1280×720/30 FPS, expected AAC track counts, 48 kHz, monotonic timestamps, exact video rate and finalization. `decode-final.json` records each stream. Silence/low-level sample activity does not prove audible speech. |
| Scaling / small dashboard | `scaled-150`: dashboard/gears/reachability/caption tests pass at DPI 144. Shipping compositor captures at 100% and forced 150%, normal and minimum logical dimensions: Capture 860×620, Media 720×540. Native physical clients match 1290×930 and 1080×810 respectively at 150%. |
| Actual deployed external appearance | `shipping-basic` and `shipping-basic-150`: bounded compositor screenshots of the exact Release HWNDs over two owned colored backing windows, after move/resize and opening Media. No white frames after repair, no outer media-image background, readable coherent fallback. Not internal QML screenshots. |
| Input/session isolation | Passive HWNDs are nonactivating and mouse-transparent before first show. Both reports recorded foreground activation **0**. Private desktop checks use targeted events, never SwitchDesktop/global input. Personal PandaClip/GD processes, pointer, focus, preferences and replay history were not controlled. |
| Persistence / dependency checks | Settings runner parses; VS Code targets retain F5/task behavior. Core pipeline/settings-store hashes unchanged. No Glass Lab/MFPlay production sources/deps. Optional-style cleanup followed import-scanner evidence; SDK files/licenses preserved. |

Failed/earlier artifacts are preserved honestly: `ui-release` had one obsolete test expecting white-video refraction after that feature was removed; replacement checks assert the shared playback material does not sample video. `shipping-materials` showed native white/grey artifacts; `shipping-final` showed the remaining Media activation/frame problem. Neither is current acceptance evidence. `ui-final`, `ui-accepted` and the final Basic run pass; use **shipping-basic** screenshots for the final material.

The lightweight UI tick in the final focused run measured mean 1.673 ms, max 4.142 ms (364 polls). This whole frontend test includes generated video/testing overhead and is not game performance or a comparative benchmark. No new ten-minute sync/performance suite was run for this frontend task.

## Actual Release screenshots

These are external compositor screenshots of inactive deployed windows using the honest solid fallback:

![Capture normal](../build/consolidation/shipping-basic/capture-blue.png)
![Capture minimum size](../build/consolidation/shipping-basic/capture-red-resized.png)
![Media normal](../build/consolidation/shipping-basic/media-blue.png)
![Media minimum size](../build/consolidation/shipping-basic/media-red-resized.png)

Settings, Diagnostics and popovers were inspected as actual Qt widgets on the owned inactive test desktop; their `ui-final-basic/screenshots` images are widget/scene QA renders, **not** desktop transmission evidence.

## Remaining limitations / acceptance

- Click PandaClip to inspect active Acrylic over two different real backgrounds. Passive fallback screenshots and accepted API calls do not establish desktop transmission. Windows may further suppress blur under compositor/accessibility/power policy. Solid/reduced-motion fallbacks remain available.
- Physical dragging/snap, user mouse/keyboard/popover use, real hotkey delivery and native borderless outline removal remain manual; quiet targeted tests do not prove physical interactions.
- Actual moving Geometry Dash performance, audible selected-app audio/mic, listening to every saved track, mic disconnection and perceptual A/V synchronization are not validated by this frontend pass. Recording/replay tests were quiet, short and used generated video/PCM fixtures, with actual WASAPI activity reported separately.
- A hidden HWND settings adapter remains internal. Production does not show its former UI. Advanced docks remain optional, with consistent solid materials when detached; they are not an alternate frontend.
- Gallery trimming/editor, automatic highlights, custom desktop refraction/monitor UI capture and other deferred features remain excluded.
