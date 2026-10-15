# Focus-transition repair — 2026-10-09

## Exact delivery

`build/focus-policy/Release/PandaClip.exe`

SHA256: `62F008DF3A0F874FEC6A73556DA8D5552ED716E60967E10A1AA9509320AF9E2B`

```powershell
& 'C:\Users\eyald\OneDrive\שולחן העבודה\gdvid\PandaClip\build\focus-policy\Release\PandaClip.exe'
```

The workspace was verified. Checkpoint: `build/checkpoints/focus-policy-20261009-172534/source.zip`. Two pre-existing `focus-dwm` processes and the user's new review process were left running and untouched. No recording/replay engine, recording settings, QML material/control tokens, layout, VS Code configuration or native caption/resize code was changed.

## Remaining cause in the previous fix

The previous delivery had already removed the Qt inactive-solid branch and maintained a consistent custom nonclient paint policy. However, `DesktopBackdrop` still routed native `WM_ACTIVATE`/`WM_ACTIVATEAPP` and Qt activation events through `refreshPolicy()`. That method invalidated the applied HWND and unconditionally wrote the DWM backdrop attributes and full-client margins again. Focus on a second monitor takes the same deactivation path as other unrelated-window focus changes; no monitor-specific style branch is involved.

This unnecessary native reapplication was measured, rather than inferred from an internal rendering grab. A pre-edit copy of the helper, instrumented only to count native applications, made 12 additional applications across 12 real thread-window activation changes on a private inactive desktop. The candidate made zero. Initial counts were 2 → 14 versus 1 → 1. An unchanged Windows settings notification produced another application in the baseline, versus none in the candidate. See `checks/baseline/focus-policy.txt` and `checks/candidate/focus-policy.txt` beneath `build/focus-policy`.

Windows itself also has an inactive background-Acrylic fallback policy, documented in [Microsoft's Acrylic material guidance](https://learn.microsoft.com/en-us/windows/apps/design/style/acrylic). Re-requesting the backdrop after deactivation can re-evaluate native policy; the prior nonclient paint handling alone was not enough to establish stability. Removing the reapplications resolved the reported transition on the user's machine, as confirmed below. This does not promise identical native effects on every Windows build.

## Changes

- Focus events now update diagnostic focus information only. They do not re-query system policy, reapply backdrop attributes/margins, change root alpha, or select another material.
- Real settings notifications still re-query effective Windows transparency/composition/high-contrast settings. Native writes occur only when the HWND or effective enable state changes, or desktop composition resets. Identical requests are cached.
- Explicit Reduce Transparency changes still disable/re-enable the effect. System accessibility fallbacks remain. No preference is changed silently.
- Logs expose focus-notification and native-application counts. Diagnostics distinguish a requested native effect from visual verification. The previous overly broad “focus-independent” badge wording is now “Native Acrylic requested.”
- Existing custom nonclient paint policy and TRUE deactivation return remain, without input focus forcing. Window dragging, native sizing, snapping routes, caption controls and popup ownership/dismissal are unchanged.

No Windows App SDK integration, undocumented composition API, monitor capture, UI screenshot background, frozen picture or new rendering experiment was introduced. The existing native compositor path is working on the user's tested machine, so a replacement renderer was not required for this repair.

## Verification

| Check | Result |
| --- | --- |
| Real PandaClip monitor 1 → YouTube monitor 2 → PandaClip | **User confirmed “its fixed”** after launching the new isolated review. The request also asked to repeat with Recording gear and Settings. This is user-reported physical verification, not an automated visual comparison performed by the agent. |
| Same-condition before/after native policy probe | 12/12 thread activation checks succeeded on an inactive owned desktop. Baseline reapplications: 12; candidate: 0. No SwitchDesktop, SetForegroundWindow, global mouse/key injection or sounds. These are not physical two-monitor visual checks. |
| Explicit transparency changes | Candidate native counts: 1 before changes, 2 after disable, 3 after re-enable. Repeated disable and unchanged policy refresh caused no extra applications. |
| Debug/Release | Both built successfully with MSVC/CMake. Exact deployed Release and Debug launched with an SDK-free PATH. |
| UI/recording/replay regression | 416 PASS checks, zero failures: `checks/regression/checks.txt`. Generated GPU/PCM fixtures exercise recording/replay/finalization, audio/mute/meters, settings, media playback, gears, native choices, retry and window controls. Actual GD or audible audio was not retested. |
| MP4 decoding | All 14 artifacts decoded; eight prepared fixtures and six new recording/replay artifacts. `checks/regression/decode.json`. |
| Actual deployed Release controls at scale overrides | Zero failures for 100%, 125%, 150% Qt scale factors, private owned desktop: `checks/shipping-100`, `shipping-125`, `shipping-150`. Five gears, settings links, dismissal and source controls tested through actual shipping UIA. These do not establish active/inactive compositor equality at each physical Windows scaling setting. |
| Preservation | 21 byte comparisons passed for engine/settings-store, QML materials/control resources and VS Code files: `preservation.json`. |
| Passive snapshot attempt | Skipped because another window overlapped the review app. No focus/position adjustment was made. `user-review-snapshot.txt`. No new active/inactive screenshot pair is claimed. |

Build logs are `build-release.log`, `build-debug.log` and `build-verification.log`. The baseline helper and probe are verification targets only; they are not linked into PandaClip.

## Remaining checks and scope

The user confirmed the physical focus-transition repair at their current setup. Independent active/inactive screenshots, physical Windows display scaling at all of 100%/125%/150%, and physical drag/snap at those settings remain manual. Changing Windows display scaling globally was intentionally avoided. Controlled scale overrides checked shipping controls, not system-wide compositor policy.

For another isolated review, run `tools/review_appearance.ps1`. It launches this exact Release with separate settings/output and initial audio/replay disabled, without automatic input. Use the newly opened window rather than an older running build. The script never closes existing sessions.

Windows accessibility, energy-saving, remote-session and compositor restrictions can still suppress blur. Native API acceptance is not universal visual proof. The repair preserves the current design and introduces no Scenes, Sources, new theme or engine feature. Blur is native Acrylic, not custom desktop refraction.
