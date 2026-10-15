# Glass Lab — Clear / Soft Frost material pass

Standalone prototype; integration into PandaClip is pending approval. The main capture/recording/audio/replay code, preferences and VS Code configuration were not changed. The existing `glass-lab-repair` process was left running; deployment and verification use a separate `glass-lab-clear` folder.

## Launch

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\build\glass-lab-clear\Release\PandaClipGlassLab.exe'
```

Choose **Clear**, **Soft Frost**, or **Solid** at the top of Material. The thin lavender underline marks the selected recipe. Adjust values live, use Reset for Clear defaults, and Save preset to retain the recipe, forced-fallback state and tuned values. The file is `GlassLabData/approved-material-v1.json` beside this executable, never PandaClip's settings. Older prototype files remain readable; no old preset was overwritten.

Optional launch flags `--clear`, `--soft-frost`, and `--solid` select a recipe for that run without saving it. Normal launch honors saved values. `--popover` opens the demonstration sheet. Keep the complete Release folder, with DLLs, QML/plugins, CRT and Qt notices/source archives, when sharing it.

## Material changes

The dashboard arrangement and existing controls are retained. New controls are the recipe selector, fine grain and the shade overlay. The tuning viewport remains scrollable; wheel input over sliders scrolls the viewport without changing their values. Keyboard focus reveals lower controls. Desktop shade is disabled when Acrylic is not configured, rather than presenting a knob with no current effect.

| Parameter | Clear | Soft Frost | Solid |
|---|---:|---:|---:|
| Control fill opacity | 6% | 13% | 20% |
| Lavender tint mix | 2.5% | 5.5% | 2.5% |
| Edge-light coefficient | 14% | 18% | 14% |
| Custom grain coefficient | 0% | 3.5% | 0% |
| Dark desktop shade overlay, when configured | 2.5% | 6.5% | Not applied |
| Bounded sample diffusion | 0.15 px | 1.4 px | 0.15 px |
| Sample refraction | 3 px | 3 px | 3 px |
| Corner radius | 18 px | 18 px | 18 px |
| Forced solid backdrop | No | No | Yes |

These are renderer coefficients, not measured desktop transmission percentages. Soft Frost's grain is deliberately barely visible: its coefficient scales a small ±0.009 RGB perturbation in the sample, and less on translucent controls. Clear has zero added grain. Windows Acrylic's own grain remains system-controlled.

GPU material fills are darker/neutral and much less opaque than the previous 16% default. The previous mandatory 17% minimum panel fill was removed. Edge lobes are narrower, hover adds only 1.2% opacity per unit, recording-button accent adds 2% instead of 8%, and the broad bright bevel appearance is reduced. Reflections are restrained analytic decoration. Shadows occupy a bounded six-pixel outset; they are not native child windows and cannot intercept input. Text/icons do not inherit material opacity, and the top-level window stays at opacity 1.

## Desktop blur versus sample optics

The existing supported Windows DWM **Desktop Acrylic** path is preserved. It obtains the scene physically behind the window; there is no wallpaper, thumbnail, desktop screenshot texture or whole-window opacity. The prototype requests Acrylic only when policy permits it and the window is active. Inactive, unsupported, reduced-transparency, high-contrast, Energy Saver or remote conditions use the matching neutral solid fallback.

**The current DWM API selects a system material; it does not expose a blur-radius, native noise or native Acrylic tint-opacity parameter.** Both translucent recipes use the same Windows blur. Clear reduces this prototype's added shade/fill/tint; Soft Frost increases those modestly. Adjustable diffusion/refraction affect only the bounded in-app optical study, whose live Qt texture is a valid source. They do not change desktop blur or refract desktop pixels. See Microsoft's [system backdrop types](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type) and [Acrylic policy documentation](https://learn.microsoft.com/en-us/windows/apps/design/style/acrylic).

The footer reports the effective fallback reason or Acrylic configured / transmission unverified, Windows transparency, active/inactive state, forced-solid choice, support, request type/HRESULT, coverage and Qt alpha/opacity. Accepted requests never mean visually verified desktop transmission. The Windows settings action is user-initiated and no global setting is changed.

## Build and focused verification

```powershell
cmake -S . -B build/glass-lab-clear -G 'Visual Studio 17 2022' -A x64 -DPANDACLIP_BUILD_GLASS_LAB=ON -DPANDACLIP_GLASS_LAB_CHECKS=ON
cmake --build build/glass-lab-clear --config Release --target PandaClipGlassLab PandaClipGlassLabCheck PandaClipGlassLabLauncher --parallel 4
```

Only Glass Lab targets were built, with MSVC 19.44, Windows SDK 10.0.26100.0 and existing Qt 6.8.3. Release deployment includes the existing dynamically linked Qt distribution and app-local MSVC CRT. No PandaClip recording suites were run.

### Verified on October 9, 2026

- **43 focused checks passed at both 100% and 150%.** Reports: `build/glass-lab-clear/checks/final-ui-100/profile/checks.json` and `final-ui-150/profile/checks.json`. Checked three recipes, live sliders, Save/Reset, Solid metadata/fallback persistence, reload, wheel over sliders, unchanged slider values during scrolling, scrollbar-thumb dragging, keyboard reveal at 810×660, demo/navigation, separate gear, popover switch/Escape/outside dismissal/focus restoration, native resize/caption hit tests, minimize/maximize/restore/close, alpha/opacity, no child HWNDs and honest policy labels. Events target the owned Qt window on an inactive private desktop; no SwitchDesktop, global keyboard/mouse injection or physical cursor movement.
- **Grain and diffusion have real GPU effects.** Static sample crops at 100% changed 14,962 pixels for grain 0→1 and 4,320 for diffusion 0→2 px. At 150% they changed 33,659 and 10,855 respectively. These diagnostic extremes prove the controls affect the sample; they do not describe default grain visibility or desktop frost. Refraction was checked independently against the same valid source.
- **Actual deployed native/compositor screenshots** compare Clear and Soft Frost at identical 1040×720 size/position over identical blue and amber owned backgrounds, plus Solid and the popover. Native presentation reports alpha 8, clear alpha 0, opacity 1, no child HWNDs and matching client/Qt dimensions. Screen inspection checks framing/clipping and the visible control material, not an internal QML render.
- The automated windows are deliberately inactive/nonactivating and thus display the solid outer backdrop. **Active desktop transmission and the active appearance over bright underlying content remain unverified.** No API acceptance, alpha test, in-app lens or passive screenshot substitutes for that check. Windows transparency was enabled during passive capture; the footer correctly identifies inactive fallback.
- Early focused runs exposed that default wheel routing over sliders did not scroll. Those failed reports are retained in `focused-100` and `focused-150`; an explicit, bounded viewport WheelHandler fixed it. It performs no layout, disk access, device enumeration or backend change. Screenshot inspection also found scrollbar/value-label overlap; a reserved gutter and quieter scrollbar corrected it. Final runs pass.
- The foreground-event guards observed zero owned activations. Existing processes/sessions were neither selected nor controlled. Physical title dragging/snap and a clean friend's PC remain manual checks.

## Actual comparison screenshots

These are actual deployed window screenshots of the **inactive solid backdrop**, with different foreground recipes. They do not establish active desktop blur.

| Clear | Soft Frost |
|---|---|
| ![Clear](../build/glass-lab-clear/checks/final-clear/window-blue.png) | ![Soft Frost](../build/glass-lab-clear/checks/final-soft-frost/window-blue.png) |

[Clear over the amber background](../build/glass-lab-clear/checks/final-clear/window-amber.png) · [Soft Frost over the identical amber background](../build/glass-lab-clear/checks/final-soft-frost/window-amber.png) · [Solid](../build/glass-lab-clear/checks/final-solid/window-blue.png) · [Popover](../build/glass-lab-clear/checks/final-popover/window-blue.png).

## One short active check

Launch normally, **click the title bar**, keep a browser behind the stationary prototype, and switch Clear → Soft Frost → Solid. Verify the footer says Window: active for the two glass recipes and compare recognizable blurred shapes, text legibility and the solid difference. This is the remaining desktop check; report if the active window stays gray/opaque. Integration into PandaClip remains pending approval of this running material.
