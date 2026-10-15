param([switch]$SkipBuild, [switch]$Interactive)
# Desktop/audio integration is opt-in. Never run during ordinary build/quiet verification.
if (-not $Interactive) {
  Write-Output 'SKIPPED interactive check: may activate owned windows, use desktop input, or play audio. Use tests\run_quiet_tests.ps1 by default; add -Interactive only when you are ready.'
  return
}

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'build'
$appPath = Join-Path $buildRoot 'Release\ReDot.exe'
$verifierPath = Join-Path $buildRoot 'replay-stress\Release\ReDotReplaySyncVerify.exe'
$stimulusPath = Join-Path $PSScriptRoot 'replay_sync_stimulus.ps1'
if (-not $SkipBuild) {
  cmake --build $buildRoot --config Release --parallel
  cmake --build (Join-Path $buildRoot 'replay-stress') --config Release --target ReDotReplaySyncVerify --parallel
}
if (-not (Test-Path $appPath)) { throw "Build PandaClip Release first: $appPath" }
if (-not (Test-Path $verifierPath)) { throw "Build ReDotReplaySyncVerify first: $verifierPath" }

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ReplaySyncUi {
 [UnmanagedFunctionPointer(CallingConvention.Winapi)] public delegate bool EnumProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
 [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder b, int n);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, StringBuilder l);
 public static IntPtr Child(IntPtr p, int id) { IntPtr f=IntPtr.Zero; EnumChildWindows(p,(h,x)=>{if(GetDlgCtrlID(h)==id){f=h;return false;}return true;},IntPtr.Zero); return f; }
 public static string Text(IntPtr h) { var b=new StringBuilder(1024); GetWindowText(h,b,b.Capacity); return b.ToString(); }
 public static int Count(IntPtr h) { return (int)SendMessage(h,0x0146,IntPtr.Zero,IntPtr.Zero); }
 public static string Item(IntPtr h,int i) { int n=(int)SendMessage(h,0x0149,(IntPtr)i,IntPtr.Zero); if(n<0||n>4096)return ""; var b=new StringBuilder(n+1); SendMessage(h,0x0148,(IntPtr)i,b); return b.ToString(); }
}
'@
Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public static class ReplaySyncNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);}'

$runRoot = Join-Path $buildRoot ('replay-sync-test\run-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$profile = Join-Path $runRoot 'profile'; $local = Join-Path $runRoot 'local'
New-Item -ItemType Directory -Path $profile,$local | Out-Null
$goFile=Join-Path $runRoot 'go.signal';$stopFile=Join-Path $runRoot 'stop.signal';$beep=Join-Path $runRoot 'beep.wav'
$oldProfile=$env:USERPROFILE;$oldLocal=$env:LOCALAPPDATA;$env:USERPROFILE=$profile;$env:LOCALAPPDATA=$local
$target=$null;$app=$null
try {
  $launchArgs = "-NoProfile -ExecutionPolicy Bypass -File `"$stimulusPath`" -Interactive -GoFile `"$goFile`" -StopFile `"$stopFile`" -BeepPath `"$beep`""
  $target=Start-Process powershell.exe -ArgumentList $launchArgs -PassThru -RedirectStandardOutput (Join-Path $runRoot 'stimulus.out') -RedirectStandardError (Join-Path $runRoot 'stimulus.err')
  Start-Sleep 2
  $app=Start-Process $appPath -PassThru
  Start-Sleep 4
  $window=(Get-Process -Id $app.Id).MainWindowHandle
  if (-not $window) { throw 'PandaClip window did not open.' }
  $targetCombo=[ReplaySyncUi]::Child($window,1001);$gameMode=[ReplaySyncUi]::Child($window,1014)
  $duration=[ReplaySyncUi]::Child($window,1025);$enable=[ReplaySyncUi]::Child($window,1023)
  $save=[ReplaySyncUi]::Child($window,1024);$status=[ReplaySyncUi]::Child($window,1027)
  $targetIndex=-1
  for($i=0;$i -lt [ReplaySyncUi]::Count($targetCombo);$i++) { if([ReplaySyncUi]::Item($targetCombo,$i) -eq 'PandaClip Timed Flash and Beep Test') { $targetIndex=$i;break } }
  if($targetIndex -lt 0) { throw 'PandaClip did not list the flash/beep stimulus window.' }
  [ReplaySyncUi]::SendMessage($targetCombo,0x014e,[IntPtr]$targetIndex,[IntPtr]::Zero)|Out-Null
  [ReplaySyncUi]::SendMessage($gameMode,0x014e,[IntPtr]1,[IntPtr]::Zero)|Out-Null # Selected application loopback
  [ReplaySyncUi]::SendMessage($duration,0x014e,[IntPtr]2,[IntPtr]::Zero)|Out-Null # 60 seconds
  [ReplaySyncUi]::SendMessage($enable,0x00f5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  $warm=$false
  for($i=0;$i -lt 85;$i++) {
    Start-Sleep 1;$text=[ReplaySyncUi]::Text($status)
    if($text -match 'Replay active: (\d+\.\d+) / 60 s') { if([double]$Matches[1] -ge 60){$warm=$true;break} }
    if($i%10 -eq 9) { Write-Output "replay warm-up: $text" }
  }
  if(-not $warm) { throw "Replay did not reach 60 seconds of history: $([ReplaySyncUi]::Text($status))" }
  Write-Output "stimulus started; replay buffer warmed: $([ReplaySyncUi]::Text($status))"
  [IO.File]::WriteAllText($goFile,'go')
  for($i=0;$i -lt 72;$i++) { Start-Sleep 1;if($i%10 -eq 9){Write-Output "flash/beep run: $($i+1)s"} }
  [ReplaySyncUi]::SendMessage($save,0x00f5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  $saved=''
  for($i=0;$i -lt 60;$i++) { Start-Sleep 1;$saved=[ReplaySyncUi]::Text($status);if($saved -match 'Saved clip:'){break};if($saved -match 'failed|warming up|memory limit'){throw "Clip save failed: $saved"} }
  if($saved -notmatch 'Saved clip: (.*?) \(') { throw "Clip did not finalize successfully: $saved" }
  $clip=Join-Path $profile ('Videos\PandaClip\' + $Matches[1])
  Write-Output "saved test clip: $clip"
  & $verifierPath --require-events $clip
  if($LASTEXITCODE -ne 0) { throw "Replay AV sync verifier failed with exit code $LASTEXITCODE" }
  Write-Output "Artifacts and stimulus logs: $runRoot"
} finally {
  [IO.File]::WriteAllText($stopFile,'stop')
  if($app -and !$app.HasExited) { [ReplaySyncNative]::PostMessage((Get-Process -Id $app.Id).MainWindowHandle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Start-Sleep 2;if(!$app.HasExited){Stop-Process -Id $app.Id -Force} }
  if($target -and !$target.HasExited) { Stop-Process -Id $target.Id -Force }
  $env:USERPROFILE=$oldProfile;$env:LOCALAPPDATA=$oldLocal
}
