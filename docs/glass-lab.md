# PandaClip Glass Lab

A standalone material approval prototype. It does not load PandaClip preferences, open capture/audio/encoders, register hotkeys, or connect to a running PandaClip session. Integration requires visual approval.

**Current material pass:** [Clear / Soft Frost / Solid, comparison screenshots and verification](glass-lab-clear.md). The repair history below describes the earlier build.

## Launch

From the PandaClip workspace:

```powershell
& '.\build\glass-lab-clear\Release\PandaClipGlassLab.exe'
```

Keep the entire Release folder together when copying it to another Windows PC: Qt DLLs, QML/plugins, app-local MSVC runtime, notices and corresponding Qt source archives. Windows 11 22621+ provides Desktop Acrylic. Older Windows or unavailable transparency uses solid materials without installation or registry changes. Release deployment includes the CRT; Debug is not a redistributable package.

## Review

Move the window over two different applications. Check the toolbar, recording demonstration/gear, popover, title-bar controls and resize edges. Sliders change tint, opacity, edge lighting and corner radius immediately. Reset restores the starting material. Preview solid fallback is application-local. Windows settings opens Accessibility > Visual effects; the prototype observes policy changes but never modifies global preferences.

Save preset writes only `GlassLabData/approved-material-v1.json` beside this executable. Logs also stay in GlassLabData. No automatic preference writes happen during rendering or adjustments. This is a visual demonstration: Start Recording toggles the sample control, and does not record anything.

For a quick look at the demonstration sheet, launch with `--popover`. `--solid` starts in the solid preview. `--passive` is a nonactivating, native-input-disabled tool window for external inspection; use the normal launch command for your review and actual interaction. Animate/Pause sample controls the bounded optical study. Windows' animation preference determines its starting state; explicit Animate is a local demonstration action. `--animate-sample --lens-demo` starts that demonstration with 8 px displacement and no local softening, without saving or changing Windows preferences.

## Rendering and references

- **Desktop blur:** the repair uses a prototype-local policy following the existing `DesktopBackdrop` helper's supported DWM contract, requesting `DWMSBT_TRANSIENTWINDOW` (Desktop Acrylic). It does not change the shared helper or PandaClip. Qt's root surface has alpha; full client coverage is requested only for Acrylic and zero glass margins for solid mode. No desktop texture is acquired for presentation; text stays sharp. Successful API calls do not establish visible desktop transmission.
- **Controls:** Qt Quick Direct3D 11 fragment shaders draw antialiased rounded surfaces and thin directional edges. The highlights are analytical decoration, not reflected desktop pixels. No whole-window opacity, wallpaper, thumbnail or screenshot backdrop.
- **Optical sample:** one bounded Qt `ShaderEffectSource` (at most 720 × 170 pixels) contains the sample's actual colored lines and labels. A small lens samples this live texture with up to 8 pixels of edge displacement and a five-tap local softening filter. Text above the lens is not sampled. Refraction and softening controls apply only here: Qt shaders cannot sample DWM's desktop backdrop. This is not Apple's renderer and does not implement desktop refraction, adaptive desktop luminance or morphing glass.
- **Policy:** Windows transparency, high contrast, DWM support, Energy Saver, Remote Desktop, native foreground state and the prototype's solid switch determine Acrylic/fallback. Inactive windows deliberately use the matching solid material rather than Windows' unrelated gray inactive Acrylic fallback. Activation requests Acrylic automatically; deactivation returns to solid. Policy notifications are coalesced. High contrast disables lens displacement/softening and sample motion. Custom controls retain their style independently of Acrylic. This prototype does not read or override PandaClip's preferences.

Studied Apple's official [Meet Liquid Glass demonstration and transcript](https://developer.apple.com/videos/play/wwdc2025/219/) and [Materials guidelines](https://developer.apple.com/design/human-interface-guidelines/materials): floating navigation, lightweight optical separation, restrained edges and legibility. These inform the composition; generated mockups are not used as rendering evidence. Windows implementation follows [Microsoft's documented backdrop types](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type).

## Build and focused verification

```powershell
cmake -S . -B build/glass-lab-repair -G 'Visual Studio 17 2022' -A x64 -DPANDACLIP_BUILD_GLASS_LAB=ON -DPANDACLIP_GLASS_LAB_CHECKS=ON
cmake --build build/glass-lab-repair --config Release --target PandaClipGlassLab PandaClipGlassLabCheck PandaClipGlassLabLauncher --parallel 4
```

Focused checks use only owned windows and private desktops; no recording suite is run. Physical dragging/snap, a friend's clean PC and final visual approval require a human check.

## Rendering repair — October 8, 2026

Current deployment is **glass-lab-repair**, not the older glass-lab executable. The user's existing process and original approved preset were left intact. Only prototype sources, this report, and the README's prototype pointer were changed. No PandaClip runtime sources, backend, preferences or VS Code configuration were changed; no recording suite was run.

### Findings and changes

- Read-only inspection of the running older prototype found one HWND, **zero native child windows**, `WS_THICKFRAME` and Qt's `WS_EX_LAYERED` treatment. The QML root had a one-pixel dark outline; GPU surfaces use thin analytic edges, not a thick white rectangular outline. Qt's official 6.8.3 source uses a premultiplied DirectComposition swapchain for alpha-capable D3D11 windows, with platform layered/legacy-blur handling. Those native styles were inspected, not blindly removed.
- Native frame painting on/off comparisons reproduced a system perimeter edge. A prototype-only subclass now suppresses `WM_NCPAINT` for the full-client custom frame, acknowledges background erase without a GDI fill, and requests `DWMWA_COLOR_NONE`. It preserves sizing/system styles and hit tests. The root's decorative outline was also removed. Nothing is covered by a new panel. The corrected deployed screenshots have no thick white perimeter or crossing rectangle. Comparing the historical white-rim capture with the delivered capture found **24,236 vs 0 white pixels in the outer eight-pixel band** (`native-edge-report.json`); this confirms the visible result, not the exact origin of the unreproduced inner artifact.
- **The specific reported upper-left thick rectangle was not reproduced.** No new image attachment was available in this repair message; its precise original producer is therefore unconfirmed. Do not treat the native-edge repair as proof of that artifact's cause. Send an active screenshot if it persists.
- The gray accepted-request render was reproduced in an explicitly inactive verification probe. It was not an opaque QML root or an unintended child HWND: the root uses a transparent clear, an 8-bit alpha request, and window opacity 1. The shipping policy instead selects a neutral solid surface while inactive. This remains distinct from proving active Acrylic transmission.
- Effective status now states the fallback reason or **Acrylic configured / visible transmission unverified**. Details show Windows transparency, native active/inactive state, forced-solid selection, composition, backdrop support, request type/result, coverage result and Qt alpha/opacity. A successful **None/solid** request is labeled as such; it cannot be mistaken for an accepted Acrylic request. Activation and owned-HWND setting/theme notifications coalesce policy refresh. Windows settings is opened only by the user's button.
- New defaults are 4% violet tint, 16% control opacity, 20% edge light and 20 px corners. Existing prototype presets remain loadable; Reset applies the neutral defaults. The optical scene has real moving lines and a moving marker; only its own bounded texture is sampled by the lens. Foreground labels remain sharp. Windows motion/high-contrast defaults are read before QML loads.

### Focused checks and limits

- Release prototype and verification targets built with the existing MSVC/Qt toolchain. **31 checks passed at 100% and 150%**: actual QML input, demo start/stop, navigation, gear/popover switching, Escape/outside dismissal, focus restoration, slider drag/keyboard adjustment, material uniforms, preset Save/Reset, sample Pause/Animate, solid preview, resize/client agreement, caption/resize hit tests, minimize/maximize/restore/close, root alpha/no window opacity, no child HWNDs, request acceptance and explicit policy labeling. Final deployed-version reports are under `build/glass-lab-repair/checks/delivered-ui-100` and `delivered-ui-150`. Targeted QWindow events ran on a private inactive desktop; no physical cursor or global keyboard injection.
- Actual shipping-HWND screenshots were acquired over owned blue/amber backgrounds, with a move and a popover. These verify the **inactive solid rendering and absence of visible borders**, not active desktop transmission. Windows transparency was on, DWM composition available and high contrast off. Windows client-area animations were disabled in a read-only system query. No Windows setting was changed.
- **Active inspection over two backgrounds remains a user check.** Automated foreground activation is prohibited. No new active screenshot or observation was received during this pass. DWM success, alpha checks and the inactive probe do not establish live desktop transmission or desktop refraction.
- A silent **five-second recording of the actual moving optical sample** was acquired externally from a positively identified, input-disabled owned prototype HWND. It contains 150 frames, all 149 adjacent source-frame pairs changed, and the packaged GIF decodes to 150 frames / 5,000 ms. PNG writes use a bounded worker queue (3,655,680 bytes for this 672×170 fixture); it is a verification utility, never a presentation texture or PandaClip recorder. See the recording below. This demonstrates in-app displacement of a valid source, not desktop sampling.
- Repeated internal Quick GPU readback on the inactive desktop stalled in early recording attempts. The launcher bounded those runs and stopped only its identified test process. Incomplete MP4/PNG artifacts remain in `ui-lens-100`, `ui-moving-100` and `ui-moving-basic`; they are **failed tests**, not playable outputs. The repeated-readback fixture was removed; the delivered demonstration uses external owned-window capture instead. Ordinary control checks and the two static displacement comparisons passed. DWM border-color readback also returned E_INVALIDARG here; the check now honestly measures setter acceptance and examines the visible native screenshot separately.
- Final passive guards observed zero owned foreground activations. Pointer endpoints were unchanged in some runs and differed during external user activity in others; no test moves the physical pointer. The tests never controlled or terminated the original Glass Lab process. It was present in earlier inventories and absent in the final inventory; the reason was not observed. Physical drag/snap, active transmission and another PC remain unverified.

### Corrected actual window and lens recording

This is a **native-window/compositor screenshot of the inactive fallback**, not a mockup or internal QML render:

![Corrected Glass Lab](../build/glass-lab-repair/checks/delivered-window/acrylic-blue.png)

[Actual popover screenshot](../build/glass-lab-repair/checks/delivered-popover/acrylic-blue.png). Historical `acrylic-*` filenames identify the harness, not a visually verified Acrylic effect.

![Five-second moving in-app lens recording](../build/glass-lab-repair/checks/delivered-lens/moving-in-app-lens.gif)

### Short active review

Run the launch command above, **click the Glass Lab title bar**, leave Preview solid fallback unchecked, and move it over a browser and then a distinctly different window. The status must say **Window: active**. Check whether blurred colors/shapes change behind it; compare forced solid. Send an active screenshot if it stays gray or shows the upper-left rectangle. This is the remaining check, not something the passive screenshots establish.

Integration into PandaClip is still pending visual approval.

## Initial prototype verification (historical, earlier build)

- **Release built and deployed** with MSVC 19.44, Windows SDK 10.0.26100.0 and Qt 6.8.3. Qt DLLs/QML/plugins, app-local VC143 CRT and Qt notices/source archives are present. No Debug build or recording suite was run for this limited prototype pass.
- **25 focused checks passed at 100% and 150%.** Actual Qt Quick controls received targeted QWindow mouse/key events on an owned inactive desktop (never SwitchDesktop or physical cursor/global input). Checked demo start/stop, navigation, gear sheet, sheet switch, Escape, outside dismissal, focus restoration, live slider drag/keyboard input, all material values, Save/Reset, solid toggle, resize/client dimensions, native caption/resize hit testing, maximize/restore/minimize and process close. Repeated a session using its saved prototype preset to check reloading. Reports: `build/glass-lab/checks/ui-approved-100/profile/checks.json` and `ui-approved-150/profile/checks.json`.
- **GPU lens verified independently.** Changing displacement from 0 to 8 px changed 3,068 pixels at 100% and 8,761 at 150% in a bounded lens crop. Neutral/refracted crops are retained with those reports. This is internal GPU fixture readback of the legitimate in-app source, not proof of desktop transmission.
- **Actual deployed native-window screenshots**, externally acquired from the running shipping executable over blue and amber owned background windows, including a move and opening the sheet. Final guards observed no owned foreground activation. Windows Transparency effects were enabled (`EnableTransparency=1`); composition was available and high contrast was off. No registry preferences, personal app controls, global input or sounds were used. Pointer endpoint differences occurred during some passive runs while external activity continued; this launcher never moves the physical cursor.
- **Desktop transmission is NOT visibly verified.** An early API-accepted passive run produced the same opaque gray appearance over both backgrounds. The final intentionally inactive prototype uses the polished dark fallback; background changes do not show through. Active-window Acrylic remains a manual visual check. Do not approve desktop blur from the successful DWM call or the in-app lens images.
- Early rejected runs are retained: the harness initially lacked COM initialization for PNG encoding, and some passive runs reported foreground activation. The final passive path uses a native tool window, input disabling and a native guard before first show; foreground-event monitoring rejects activation. The popover initially cached coordinates before layout and opened at the wrong location; it now computes its anchor when opened. Shader baking uses explicit dependencies because Qt 6.8 qsb depfiles mishandled spaces in this workspace. No main application code was changed to address these prototype issues.

### Actual prototype screenshots

These are real native-window/compositor captures, not generated mockups or internal QML renders. They show the **inactive solid fallback**.

![Glass Lab window](../build/glass-lab/checks/review-final/acrylic-blue.png)

![Glass Lab demonstration popover](../build/glass-lab/checks/review-popover-final/acrylic-blue.png)

[Same window over the amber backing window](../build/glass-lab/checks/review-final/acrylic-amber.png). Filenames preserve the original requested-Acrylic test label; the screenshot's visible status correctly identifies solid appearance.

### Your visual review

1. Run the normal Release command. Click/activate Glass Lab, with a browser or another owned application underneath. Move it over a distinctly different background. Confirm that an active window actually shows the live blurred colors/shapes, then compare the solid preview switch. Report if it still looks gray/opaque; that would remain a compositor integration issue, not an approved glass result.
2. Try physical title dragging, edge resizing, maximize/snap, minimize/restore and closing. Native hit tests and targeted controls passed; physical gestures were not simulated.
3. Check the gear sheet and live values. Compare Refraction 0/8 px in the labeled optical sample. Judge the edges and typography rather than expecting desktop lensing, which is not implemented.
4. Save the material you approve. Share the whole Release folder with a friend to check a clean machine; that clean-PC check has not been performed here.

Integration into PandaClip comes only after approval of this running prototype.
