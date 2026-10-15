# OPT-IN, NOT QUIET: launches ReDot with a throwaway LOCALAPPDATA profile under build and injects ONE key
# combination (Ctrl+Shift+F9, then F9 alone) with keybd_event to prove Windows delivers WM_HOTKEY to a
# non-foreground ReDot. Personal settings/recordings are never touched. Run: -Executable <ReDot.exe>
param([Parameter(Mandatory)][string]$Executable, [string]$Work = "$PSScriptRoot\..\build\hotkey-delivery")
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;
public static class HK{
 [DllImport("user32.dll")]public static extern bool RegisterHotKey(IntPtr h,int id,uint m,uint vk);
 [DllImport("user32.dll")]public static extern bool UnregisterHotKey(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern void keybd_event(byte vk,byte sc,uint f,UIntPtr e);
 public static bool Owned(uint mods,uint vk){bool ok=RegisterHotKey(IntPtr.Zero,9,mods|0x4000,vk);if(ok)UnregisterHotKey(IntPtr.Zero,9);return !ok;}
 public static void Press(byte[] mods,byte vk){foreach(var m in mods)keybd_event(m,0,0,UIntPtr.Zero);keybd_event(vk,0,0,UIntPtr.Zero);keybd_event(vk,0,2,UIntPtr.Zero);for(int i=mods.Length-1;i>=0;i--)keybd_event(mods[i],0,2,UIntPtr.Zero);}
}
'@
$failures = 0
function Check($name, $ok) { if ($ok) { "PASS $name" } else { "FAIL $name"; $script:failures++ } }
function Run-Case($label, $binding0, [uint32]$mods, [uint32]$vk, [byte[]]$press, [byte]$key) {
    $profile = Join-Path $Work $label; Remove-Item $profile -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory "$profile\PandaClip" -Force | Out-Null
    if ($binding0) { # simulates an existing install whose user changed the shortcut
        Set-Content "$profile\PandaClip\settings-v1.txt" -Encoding utf8 "PandaClip settings 1`r`n`"binding0`" `"$binding0`"`r`n"
    }
    $env:LOCALAPPDATA = $profile
    $p = Start-Process $Executable -PassThru
    try {
        Start-Sleep 8
        $log = "$profile\PandaClip\logs\panda.log"
        Check "$label registered in log" ((Get-Content $log -ErrorAction SilentlyContinue) -match 'Shortcut 0 registered' )
        Check "$label combination owned by ReDot (second registrant refused)" ([HK]::Owned($mods, $vk))
        [HK]::Press($press, $key); Start-Sleep 2   # this console keeps focus: ReDot is a background app
        Check "$label press reached ReDot (replay-off notice logged)" ((Get-Content $log -ErrorAction SilentlyContinue) -match 'Shortcut notice: .*replay buffer is off')
    } finally { if (-not $p.HasExited) { Stop-Process $p -Force } }
}
Run-Case 'fresh-default-CtrlShiftF9' $null 6 0x78 @(0x11, 0x10) 0x78
Run-Case 'changed-to-F9'            120   0 0x78 @()            0x78
"Failures: $failures"; exit $failures
