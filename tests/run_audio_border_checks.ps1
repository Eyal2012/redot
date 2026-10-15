param([string]$BuildDirectory='build\audio-border',[ValidateSet('Debug','Release')][string]$Configuration='Release', [switch]$Interactive)
# Desktop/audio integration is opt-in. Never run during ordinary build/quiet verification.
if (-not $Interactive) {
  Write-Output 'SKIPPED interactive check: may activate owned windows, use desktop input, or play audio. Use tests\run_quiet_tests.ps1 by default; add -Interactive only when you are ready.'
  return
}

$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$root=Join-Path $project ('build\audio-border\ui-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $root | Out-Null
$previous=@{};foreach($key in @('TEMP','TMP','USERPROFILE','LOCALAPPDATA')){$previous[$key]=[Environment]::GetEnvironmentVariable($key)}
$env:TEMP=$root;$env:TMP=$root;$env:USERPROFILE=Join-Path $root 'profile';$env:LOCALAPPDATA=Join-Path $root 'local'
New-Item -ItemType Directory -Path $env:USERPROFILE,$env:LOCALAPPDATA | Out-Null
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;using System.Text;using System.Runtime.InteropServices;using System.Drawing;
public static class AudioBorderUi {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p,IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h,EnumProc p,IntPtr l);
 [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,StringBuilder b,int n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,StringBuilder l);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint f);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 public struct Rect{public int Left,Top,Right,Bottom;}
 public static IntPtr Window(int pid,string cls){IntPtr result=IntPtr.Zero;EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);var c=new StringBuilder(256);GetClassName(h,c,256);if(p==pid&&c.ToString().StartsWith(cls,StringComparison.Ordinal)&&(cls!="WindowsForms"||Text(h)=="PandaClip Timed Flash and Beep Test")){result=h;return false;}return true;},IntPtr.Zero);return result;}
 public static IntPtr Child(IntPtr h,int id){IntPtr result=IntPtr.Zero;EnumChildWindows(h,(c,l)=>{if(GetDlgCtrlID(c)==id){result=c;return false;}return true;},IntPtr.Zero);return result;}
 public static string Text(IntPtr h){var b=new StringBuilder(4096);SendMessage(h,13,(IntPtr)b.Capacity,b);return b.ToString();}
 public static string Item(IntPtr h,int index){var b=new StringBuilder(1024);SendMessage(h,0x148,(IntPtr)index,b);return b.ToString();}
 public static int Count(IntPtr h){return (int)SendMessage(h,0x146,IntPtr.Zero,IntPtr.Zero);}
 public static void Screenshot(IntPtr h,string path){Rect r;GetWindowRect(h,out r);using(var b=new Bitmap(r.Right-r.Left,r.Bottom-r.Top))using(var g=Graphics.FromImage(b)){var dc=g.GetHdc();PrintWindow(h,dc,2);g.ReleaseHdc(dc);b.Save(path,System.Drawing.Imaging.ImageFormat.Png);}}
}
'@
function Click([IntPtr]$h,[int]$id){[AudioBorderUi]::SendMessage([AudioBorderUi]::Child($h,$id),0xf5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
function SetChoice([IntPtr]$h,[int]$id,[int]$index){$c=[AudioBorderUi]::Child($h,$id);[AudioBorderUi]::SendMessage($c,0x14e,[IntPtr]$index,[IntPtr]::Zero)|Out-Null;[AudioBorderUi]::SendMessage($h,0x111,[IntPtr]((1 -shl 16)+$id),$c)|Out-Null}
function SetCheck([IntPtr]$h,[int]$id,[bool]$on){$c=[AudioBorderUi]::Child($h,$id);$state=[AudioBorderUi]::SendMessage($c,0xf0,[IntPtr]::Zero,[IntPtr]::Zero);if(($state -eq 1) -ne $on){Click $h $id}}
$app=$null;$source=$null;$window=[IntPtr]::Zero
try {
 $go=Join-Path $root 'go.signal';$stop=Join-Path $root 'stop.signal';$wav=Join-Path $root 'beep.wav'
 $args="-NoProfile -ExecutionPolicy Bypass -File `"$PSScriptRoot\replay_sync_stimulus.ps1`" -Interactive -GoFile `"$go`" -StopFile `"$stop`" -BeepPath `"$wav`" -DurationSeconds 240"
 $source=Start-Process powershell.exe -ArgumentList $args -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $root 'source.out') -RedirectStandardError (Join-Path $root 'source.err')
 for($attempt=0;$attempt -lt 30;$attempt++){Start-Sleep 1;$sourceWindow=[AudioBorderUi]::Window($source.Id,'WindowsForms');if($sourceWindow -ne [IntPtr]::Zero){[AudioBorderUi]::ShowWindow($sourceWindow,5)|Out-Null;break}}
 if($sourceWindow -eq [IntPtr]::Zero){throw 'Owned stimulus window did not appear.'}
 $app=Start-Process (Join-Path $project "$BuildDirectory\$Configuration\ReDot.exe") -PassThru
 for($attempt=0;$attempt -lt 20;$attempt++){Start-Sleep 1;$window=[AudioBorderUi]::Window($app.Id,'PandaClipMainWindow');if($window -ne [IntPtr]::Zero){break}}
 if($window -eq [IntPtr]::Zero){throw 'Owned app window not found.'}
 $combo=[AudioBorderUi]::Child($window,1001);$index=-1
 for($i=0;$i -lt [AudioBorderUi]::Count($combo);$i++){Write-Output "Target choice $i : $([AudioBorderUi]::Item($combo,$i))";if([AudioBorderUi]::Item($combo,$i) -eq 'PandaClip Timed Flash and Beep Test'){$index=$i;break}}
 if($index -lt 0){throw 'Owned source missing.'};SetChoice $window 1001 $index
 SetChoice $window 1026 1 # The user's already running app owns the default shortcut.
 $mic=[AudioBorderUi]::Child($window,1016)
 Write-Output "Artifacts: $root"
 for($i=0;$i -lt [AudioBorderUi]::Count($mic);$i++){Write-Output "Mic choice $i : $([AudioBorderUi]::Item($mic,$i))"}
 if([AudioBorderUi]::Item($mic,0) -notmatch 'Default device'){throw 'Default microphone choice missing.'}
 Click $window 1037
 Start-Sleep 3
 $status=[AudioBorderUi]::Text([AudioBorderUi]::Child($window,1038));Write-Output "Pre-record check: $status"
 if($status -notmatch 'CHECK open:'){throw "Mic check did not open: $status"}
 if([AudioBorderUi]::Text([AudioBorderUi]::Child($window,1010)) -ne 'Start Recording'){throw 'Mic check unexpectedly started recording.'}
 [AudioBorderUi]::Screenshot($window,(Join-Path $root 'main-mic-check.png'))
 $virtual=-1;for($i=1;$i -lt [AudioBorderUi]::Count($mic);$i++){if([AudioBorderUi]::Item($mic,$i) -match 'e2eSoft iVCam'){$virtual=$i;break}}
 if($virtual -gt 0){SetChoice $window 1016 $virtual;Start-Sleep 3;Write-Output "Explicit virtual input check: $([AudioBorderUi]::Text([AudioBorderUi]::Child($window,1038)))"}
 # A named endpoint is chosen explicitly; no fallback from failed/default input.
 $named=-1;for($i=1;$i -lt [AudioBorderUi]::Count($mic);$i++){if([AudioBorderUi]::Item($mic,$i) -match 'HyperX Cloud III Wireless'){$named=$i;break}}
 if($named -gt 0){SetChoice $window 1016 $named;Start-Sleep 3;$status=[AudioBorderUi]::Text([AudioBorderUi]::Child($window,1038));Write-Output "Named selection check: $status";if($status -notmatch 'CHECK open: Microphone \(HyperX Cloud III Wireless\)'){throw 'Named selection was not opened.'}}
 Click $window 1002
 for($i=0;$i -lt 30;$i++){Start-Sleep 1;$border=[AudioBorderUi]::Text([AudioBorderUi]::Child($window,1035));if($border -notmatch 'waiting'){break}}
 Write-Output "Actual Windows border result: $border"
 if($border -match 'waiting|will be requested'){throw 'Windows consent needs a human response; actual grant/deny remains unverified.'}
 if($border -notmatch 'permission granted|outline remains'){throw 'Permission result was not honestly surfaced.'}
 [IO.File]::WriteAllText($go,'go')
 $cases=@(@('game-only',1,$false,$false,2),@('mic-only',0,$true,$false,2),@('both',1,$true,$false,3),@('mic-muted',0,$true,$true,2),@('mic-zero-gain',0,$true,$false,2,0),@('video-only',0,$false,$false,0))
 foreach($case in $cases){
  SetChoice $window 1014 ([int]$case[1]);SetCheck $window 1015 ([bool]$case[2]);SetCheck $window 1018 ([bool]$case[3])
  $gain=100;if($case.Count -gt 5){$gain=[int]$case[5]};[AudioBorderUi]::SendMessage([AudioBorderUi]::Child($window,1020),0x405,[IntPtr]1,[IntPtr]$gain)|Out-Null
  # First start must drain an active mic check away from the UI thread.
  Click $window 1010
  for($i=0;$i -lt 15;$i++){Start-Sleep 1;$status=[AudioBorderUi]::Text([AudioBorderUi]::Child($window,1012));if($status -match 'Recording active|Recording 1920'){break}}
  if($status -notmatch 'Recording active|Recording 1920'){throw "Recording failed to start: $status"}
  Start-Sleep 12
  Write-Output "$($case[0]) mic: $([AudioBorderUi]::Text([AudioBorderUi]::Child($window,1038)))"
  Click $window 1011
  for($i=0;$i -lt 30;$i++){Start-Sleep 1;$status=[AudioBorderUi]::Text([AudioBorderUi]::Child($window,1012));if($status -match 'Saved MP4|failed'){break}}
  if($status -notmatch 'Saved MP4'){throw "File did not finalize: $status"}
  $file=Get-ChildItem (Join-Path $env:USERPROFILE 'Videos\PandaClip') -Filter '*.mp4' | Sort-Object LastWriteTime | Select-Object -Last 1
  $result=& (Join-Path $project "$BuildDirectory\$Configuration\ReDotReplaySyncVerify.exe") $file.FullName
  $result | Set-Content -LiteralPath ($file.FullName+'.verify.txt') -Encoding UTF8
  if($LASTEXITCODE -ne 0 -or ($result -join "`n") -notmatch "tracks: video=1 audio=$($case[4])"){throw "Decode/track layout failed: $($case[0])"}
  Write-Output "PASS $($case[0]): $($file.Name)";Write-Output ($result | Where-Object {$_ -match 'Audio activity|tracks:|decodeResult'})
  if($case[0] -in @('mic-muted','mic-zero-gain') -and @($result | Where-Object {$_ -match 'Audio activity' -and $_ -notmatch 'digitalSilence=1'}).Count){throw 'Muted/zero-gain microphone track was not digitally silent.'}
 }
 # Permission policy must be reapplied on subsequent real capture sessions.
 for($i=0;$i -lt 2;$i++){Click $window 1003;Start-Sleep 1;Click $window 1002;Start-Sleep 2}
 Write-Output 'PASS: repeated capture restart; border result remains visible.'
 SetChoice $window 1014 1;SetCheck $window 1015 $true;SetCheck $window 1018 $false
 Click $window 1023;Start-Sleep 5;Click $window 1024;Start-Sleep 3
 $clipStatus=[AudioBorderUi]::Text([AudioBorderUi]::Child($window,1027));Write-Output "Replay result: $clipStatus"
 if($clipStatus -notmatch 'Saved clip:'){throw 'Replay export failed.'}
 Click $window 1023;Start-Sleep 2
 foreach($clip in Get-ChildItem (Join-Path $env:USERPROFILE 'Videos\PandaClip') -Filter '*_Clip_*.mp4'){
  $result=& (Join-Path $project "$BuildDirectory\$Configuration\ReDotReplaySyncVerify.exe") $clip.FullName
  $result | Set-Content -LiteralPath ($clip.FullName+'.verify.txt') -Encoding UTF8
  if($LASTEXITCODE -ne 0 -or ($result -join "`n") -notmatch 'tracks: video=1 audio=3'){throw 'Replay decode/track layout failed.'}
 }
 Write-Output 'PASS: recording source combinations, live selected mic check, repeated capture, replay export. Listening/physical microphone speech and actual denied/granted Windows consent require separate evidence.'
} finally {
 if($app -and !$app.HasExited){[AudioBorderUi]::PostMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Start-Sleep 3;if(!$app.HasExited){Stop-Process -Id $app.Id -Force}}
 if($source -and !$source.HasExited){[IO.File]::WriteAllText((Join-Path $root 'stop.signal'),'stop');Start-Sleep 1;if(!$source.HasExited){Stop-Process -Id $source.Id -Force}}
 foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key])}
}

