param([switch]$Interactive)
# Desktop/audio integration is opt-in. Never run during ordinary build/quiet verification.
if (-not $Interactive) {
  Write-Output 'SKIPPED interactive check: may activate owned windows, use desktop input, or play audio. Use tests\run_quiet_tests.ps1 by default; add -Interactive only when you are ready.'
  return
}

$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$root=Join-Path $project ('build\milestone6\checks-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
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
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 public static void Screenshot(IntPtr h,string path){var old=SetThreadDpiAwarenessContext((IntPtr)(-4));Rect r;GetWindowRect(h,out r);using(var b=new Bitmap(r.Right-r.Left,r.Bottom-r.Top))using(var g=Graphics.FromImage(b)){var dc=g.GetHdc();PrintWindow(h,dc,2);g.ReleaseHdc(dc);b.Save(path,System.Drawing.Imaging.ImageFormat.Png);}SetThreadDpiAwarenessContext(old);}
}
'@
function Click([IntPtr]$h,[int]$id){[AudioBorderUi]::SendMessage([AudioBorderUi]::Child($h,$id),0xf5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
function SetChoice([IntPtr]$h,[int]$id,[int]$index){$c=[AudioBorderUi]::Child($h,$id);[AudioBorderUi]::SendMessage($c,0x14e,[IntPtr]$index,[IntPtr]::Zero)|Out-Null;[AudioBorderUi]::SendMessage($h,0x111,[IntPtr]((1 -shl 16)+$id),$c)|Out-Null}
function SetCheck([IntPtr]$h,[int]$id,[bool]$on){$c=[AudioBorderUi]::Child($h,$id);$state=[AudioBorderUi]::SendMessage($c,0xf0,[IntPtr]::Zero,[IntPtr]::Zero);if(($state -eq 1) -ne $on){Click $h $id}}


function Text([int]$id){[AudioBorderUi]::Text([AudioBorderUi]::Child($window,$id))}
function SetText([int]$id,[string]$value){[AudioBorderUi]::SendMessage([AudioBorderUi]::Child($window,$id),12,[IntPtr]::Zero,[Text.StringBuilder]::new($value))|Out-Null}
function WaitStatus([int]$id,[string]$pattern,[int]$seconds=20){for($i=0;$i -lt $seconds*2;$i++){Start-Sleep -Milliseconds 500;$t=Text $id;if($t -match $pattern){return $t}};throw "Timeout $id expected $pattern : $t"}
function Apply {Click $window 2010;$t=Text 2013;if($t -notmatch 'Settings applied|Saved for the next'){throw "Apply failed: $t"}}
$app=$null;$source=$null;$window=[IntPtr]::Zero
try {
 $go=Join-Path $root 'go.signal';$stop=Join-Path $root 'stop.signal';$wav=Join-Path $root 'beep.wav'
 $args="-NoProfile -ExecutionPolicy Bypass -File `"$PSScriptRoot\replay_sync_stimulus.ps1`" -Interactive -GoFile `"$go`" -StopFile `"$stop`" -BeepPath `"$wav`" -DurationSeconds 240"
 $source=Start-Process powershell.exe -ArgumentList $args -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $root 'source.out') -RedirectStandardError (Join-Path $root 'source.err')
 for($i=0;$i -lt 20;$i++){Start-Sleep 1;$sourceWindow=[AudioBorderUi]::Window($source.Id,'WindowsForms');if($sourceWindow -ne [IntPtr]::Zero){[AudioBorderUi]::ShowWindow($sourceWindow,5)|Out-Null;break}}
 $app=Start-Process (Join-Path $project 'build\milestone6\Release\ReDot.exe') -PassThru
 Start-Sleep 3;$window=[AudioBorderUi]::Window($app.Id,'PandaClipMainWindow')
 Write-Output "Artifacts: $root"
 if($window -eq [IntPtr]::Zero){throw 'App missing'}
 $combo=[AudioBorderUi]::Child($window,1001);$index=-1;for($i=0;$i -lt [AudioBorderUi]::Count($combo);$i++){if([AudioBorderUi]::Item($combo,$i) -eq 'PandaClip Timed Flash and Beep Test'){$index=$i;break}}
 if($index -lt 0){throw 'Owned source missing'};SetChoice $window 1001 $index
 Click $window 2002
 foreach($bad in @(@(1025,'4'),@(1025,'601'),@(1025,'sixty'),@(1028,'127'),@(1028,'4097'),@(2020,'1279'))){SetText $bad[0] $bad[1];Click $window 2010;if((Text 2013) -notmatch 'Invalid|Use even'){throw 'Invalid value was accepted'};SetText 1025 '60';SetText 1028 '512';SetText 2020 '1920'}
 SetText 1025 '37';SetText 1028 '128';Apply
 Write-Output 'PASS invalid numeric inputs rejected; custom 37 seconds / 128 MiB accepted'
 # Duplicate shortcut detection and clearing via real hotkey control messages.
 foreach($id in @(2201,2202)){[AudioBorderUi]::SendMessage([AudioBorderUi]::Child($window,$id),0x401,[IntPtr]((6 -shl 8)+0x79),[IntPtr]::Zero)|Out-Null}
 Click $window 2080;if((Text 2301) -notmatch 'Conflict' -or (Text 2302) -notmatch 'Conflict'){throw 'Internal hotkey conflict missing'}
 [AudioBorderUi]::SendMessage([AudioBorderUi]::Child($window,2202),0x100,[IntPtr]8,[IntPtr]::Zero)|Out-Null;Click $window 2080
 if((Text 2302) -ne 'Unbound'){throw 'Backspace did not clear binding'}
 Write-Output "PASS hotkeys: $(Text 2301); default: $(Text 2300)"
 SetChoice $window 1014 1;SetCheck $window 1015 $true
 $mic=[AudioBorderUi]::Child($window,1016);for($i=1;$i -lt [AudioBorderUi]::Count($mic);$i++){if([AudioBorderUi]::Item($mic,$i) -match '^Microphone \(HyperX Cloud III Wireless\)$'){SetChoice $window 1016 $i;break}}
 Click $window 2000;Click $window 1002;Start-Sleep 3
 [IO.File]::WriteAllText($go,'go')
 $cases=@(@('720p30-VBR',1280,720,30,0,0,3),@('1080p60-CBR',1920,1080,60,1,1,1),@('1440p60-CQP',2560,1440,60,2,2,2),@('720p120-VBR',1280,720,120,0,3,0),@('custom900p30',1600,900,30,0,0,3))
 foreach($case in $cases){
  Click $window 2002;SetText 2020 $case[1];SetText 2021 $case[2];SetText 2022 $case[3];SetChoice $window 2023 $case[4]
  $layout=[int]$case[5];SetCheck $window 2033 ($layout -in @(0,1));SetCheck $window 2034 ($layout -in @(0,2));SetCheck $window 2035 ($layout -in @(0,2));Apply
  Click $window 2000;Click $window 1010;WaitStatus 1012 'Recording active|Recording [0-9]'|Write-Output
  Start-Sleep 5;Click $window 1011;WaitStatus 1012 'Saved MP4'|Write-Output
  $file=Get-ChildItem (Join-Path $env:USERPROFILE 'Videos\PandaClip') -Filter '*.mp4' | Sort-Object LastWriteTime | Select-Object -Last 1
  $result=& (Join-Path $project 'build\milestone6\Release\ReDotReplaySyncVerify.exe') $file.FullName
  $result | Set-Content -LiteralPath ($file.FullName+'.verify.txt') -Encoding UTF8
  if($LASTEXITCODE -ne 0 -or ($result -join "`n") -notmatch "tracks: video=1 audio=$($case[6])"){throw "Decode/layout failed $($case[0]) : $result"}
  Write-Output "PASS $($case[0]): $($result | Where-Object {$_ -match 'H.264|tracks:|decodeResult'})"
 }
 Click $window 1023;Start-Sleep 5;Click $window 1024;WaitStatus 1027 'Saved clip:'|Write-Output
 Click $window 2001;WaitStatus 2062 'finalized MP4'|Write-Output
 [AudioBorderUi]::Screenshot($window,(Join-Path $root 'gallery.png'))
 $list=[AudioBorderUi]::Child($window,2053);$point=[IntPtr]((60 -shl 16)+100)
 foreach($m in @(0x201,0x202,0x203,0x202)){[AudioBorderUi]::PostMessage($list,$m,[IntPtr]::Zero,$point)|Out-Null}
 WaitStatus 2063 'Playback [0-9]'|Write-Output
 for($i=0;$i -lt [AudioBorderUi]::Count([AudioBorderUi]::Child($window,2059));$i++){Write-Output "Playback track $i : $([AudioBorderUi]::Item([AudioBorderUi]::Child($window,2059),$i))"}
 SetChoice $window 2059 1;Start-Sleep 1;Write-Output "Track switch: $(Text 2063)"
 Click $window 2058;Start-Sleep 1;[AudioBorderUi]::Screenshot($window,(Join-Path $root 'fullscreen.png'));Click $window 2058
 Click $window 2000;Click $window 1024;WaitStatus 1027 'Saved clip:'|Write-Output
 Click $window 1023;Start-Sleep 2
 foreach($file in Get-ChildItem (Join-Path $env:USERPROFILE 'Videos\PandaClip') -Filter '*_Clip_*.mp4'){$result=& (Join-Path $project 'build\milestone6\Release\ReDotReplaySyncVerify.exe') $file.FullName;$result|Set-Content ($file.FullName+'.verify.txt');if($LASTEXITCODE -ne 0){throw 'Replay independent decode failed'}}
 Click $window 2002;[AudioBorderUi]::Screenshot($window,(Join-Path $root 'settings.png'))
 Click $window 2000;[AudioBorderUi]::Screenshot($window,(Join-Path $root 'capture.png'))
 [AudioBorderUi]::PostMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 if(!$app.WaitForExit(15000)){throw 'Clean shutdown exceeded 15 seconds'}
 $app=Start-Process (Join-Path $project 'build\milestone6\Release\ReDot.exe') -PassThru;Start-Sleep 3;$window=[AudioBorderUi]::Window($app.Id,'PandaClipMainWindow')
 if((Text 2020) -ne '1600' -or (Text 1025) -ne '37' -or (Text 1028) -ne '128'){throw 'Restored fields mismatch'}
 if((Text 1005) -notmatch 'Ready'){throw "Unexpected automatic capture: $(Text 1005)"}
 Write-Output 'PASS settings restore, no automatic capture, replay continues during playback, clean exit'
} finally {
 if($app -and !$app.HasExited){[AudioBorderUi]::SendMessage($window,0x111,[IntPtr]2094,[IntPtr]::Zero)|Out-Null;if(!$app.WaitForExit(15000)){Stop-Process -Id $app.Id -Force}}
 if($source -and !$source.HasExited){[IO.File]::WriteAllText((Join-Path $root 'stop.signal'),'stop');if(!$source.WaitForExit(5000)){Stop-Process -Id $source.Id -Force}}
 foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key])}
}
