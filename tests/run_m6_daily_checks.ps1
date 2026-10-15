param([string]$Executable='build\milestone6\Release\ReDot.exe', [switch]$Interactive)
# Desktop/audio integration is opt-in. Never run during ordinary build/quiet verification.
if (-not $Interactive) {
  Write-Output 'SKIPPED interactive check: may activate owned windows, use desktop input, or play audio. Use tests\run_quiet_tests.ps1 by default; add -Interactive only when you are ready.'
  return
}

$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$root=Join-Path $project ('build\milestone6\daily-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $root | Out-Null
$previous=@{};foreach($key in @('TEMP','TMP','USERPROFILE','LOCALAPPDATA')){$previous[$key]=[Environment]::GetEnvironmentVariable($key)}
$env:TEMP=$root;$env:TMP=$root;$env:USERPROFILE=Join-Path $root 'profile';$env:LOCALAPPDATA=Join-Path $root 'local'
New-Item -ItemType Directory -Path $env:USERPROFILE,$env:LOCALAPPDATA | Out-Null
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;using System.Text;using System.Runtime.InteropServices;using System.Drawing;
public static class AudioBorderUi {
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk,byte scan,uint flags,UIntPtr extra);
 public static void DesktopScreenshot(IntPtr h,string path){var old=SetThreadDpiAwarenessContext((IntPtr)(-4));Rect r;GetWindowRect(h,out r);using(var b=new Bitmap(r.Right-r.Left,r.Bottom-r.Top))using(var g=Graphics.FromImage(b)){g.CopyFromScreen(r.Left,r.Top,0,0,b.Size);b.Save(path,System.Drawing.Imaging.ImageFormat.Png);}SetThreadDpiAwarenessContext(old);}
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
 $app=Start-Process (Join-Path $project $Executable) -PassThru
 Start-Sleep 3;$window=[AudioBorderUi]::Window($app.Id,'PandaClipMainWindow')
 Write-Output "Artifacts: $root"
 if($window -eq [IntPtr]::Zero){throw 'App missing'}
 $combo=[AudioBorderUi]::Child($window,1001);$index=-1;for($i=0;$i -lt [AudioBorderUi]::Count($combo);$i++){if([AudioBorderUi]::Item($combo,$i) -eq 'PandaClip Timed Flash and Beep Test'){$index=$i;break}}
 if($index -lt 0){throw 'Owned source missing'};SetChoice $window 1001 $index
 
 $folder=Join-Path $env:USERPROFILE 'Videos\PandaClip';New-Item -ItemType Directory -Path $folder -Force|Out-Null
 Click $window 2002;SetText 2020 '1280';SetText 2021 '720';SetText 2022 '30';SetText 1025 '15';SetText 1028 '128';Apply
 SetChoice $window 1014 1;SetCheck $window 1015 $true
 $mic=[AudioBorderUi]::Child($window,1016);for($i=1;$i -lt [AudioBorderUi]::Count($mic);$i++){if([AudioBorderUi]::Item($mic,$i) -match '^Microphone \(HyperX Cloud III Wireless\)$'){SetChoice $window 1016 $i;break}}
 # Six hotkey captures; duplicate/unbound tested in the format matrix. This action is global but only registered to the owned test instance.
 [AudioBorderUi]::SendMessage([AudioBorderUi]::Child($window,2203),0x401,[IntPtr]((6 -shl 8)+0x79),[IntPtr]::Zero)|Out-Null;Click $window 2080
 if((Text 2303) -notmatch 'Active globally'){throw 'Test shortcut conflict; cannot safely inject a key'}
 $before=[AudioBorderUi]::SendMessage([AudioBorderUi]::Child($window,1018),0xf0,[IntPtr]::Zero,[IntPtr]::Zero)
 [AudioBorderUi]::keybd_event(0x11,0,0,[UIntPtr]::Zero);[AudioBorderUi]::keybd_event(0x12,0,0,[UIntPtr]::Zero)
 [AudioBorderUi]::keybd_event(0x79,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 200
 [AudioBorderUi]::keybd_event(0x79,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 200
 [AudioBorderUi]::keybd_event(0x79,0,2,[UIntPtr]::Zero);[AudioBorderUi]::keybd_event(0x12,0,2,[UIntPtr]::Zero);[AudioBorderUi]::keybd_event(0x11,0,2,[UIntPtr]::Zero)
 $after=[AudioBorderUi]::SendMessage([AudioBorderUi]::Child($window,1018),0xf0,[IntPtr]::Zero,[IntPtr]::Zero)
 if($before -eq $after){throw 'Held/repeated hotkey toggled twice or not at all'};SetCheck $window 1018 $false
 Write-Output 'PASS registered Ctrl+Alt+F10 acted once for repeated key-down without release'
 Click $window 2000;Click $window 1002;Start-Sleep 3;[IO.File]::WriteAllText($go,'go');Click $window 1010;WaitStatus 1012 'Recording active'|Write-Output
 Click $window 1023;Start-Sleep 4
 # Pending format must not affect the active worker or snapshot configuration.
 Click $window 2002;SetText 2020 '1600';SetText 2021 '900';SetText 2022 '60';Apply
 if((Text 2013) -notmatch 'next encoder session'){throw 'Pending settings not reported'}
 Click $window 2000;Click $window 1024;WaitStatus 1027 'Saved clip:'|Write-Output
 $clip=Get-ChildItem $folder -Filter '*Clip_*.mp4'|Sort-Object LastWriteTime|Select-Object -Last 1
 $verify=& (Join-Path $project 'build\milestone6\Release\ReDotReplaySyncVerify.exe') $clip.FullName;$verify|Set-Content ($clip.FullName+'.verify.txt')
 if($LASTEXITCODE -ne 0 -or ($verify -join "`n") -notmatch '1280x720 @ 30/1'){throw 'Active encoder changed before safe boundary'}
 Click $window 1011;WaitStatus 1012 'Saved MP4'|Write-Output;Start-Sleep 4
 Click $window 1024;WaitStatus 1027 'Saved clip:'|Write-Output
 $clip=Get-ChildItem $folder -Filter '*Clip_*.mp4'|Sort-Object LastWriteTime|Select-Object -Last 1
 $verify=& (Join-Path $project 'build\milestone6\Release\ReDotReplaySyncVerify.exe') $clip.FullName;$verify|Set-Content ($clip.FullName+'.verify.txt')
 if($LASTEXITCODE -ne 0 -or ($verify -join "`n") -notmatch '1600x900 @ 60/1'){throw 'Next session did not use new settings'}
 Write-Output 'PASS pending settings preserve old replay format; restart uses new format'
 Click $window 2001;WaitStatus 2062 'finalized MP4'|Write-Output
 $list=[AudioBorderUi]::Child($window,2053);$point=[IntPtr]((60 -shl 16)+100)
 foreach($m in @(0x201,0x202,0x203,0x202)){[AudioBorderUi]::PostMessage($list,$m,[IntPtr]::Zero,$point)|Out-Null}
 WaitStatus 2063 'Playback [0-9]'|Write-Output
 $labels=@(for($i=0;$i -lt [AudioBorderUi]::Count([AudioBorderUi]::Child($window,2059));$i++){[AudioBorderUi]::Item([AudioBorderUi]::Child($window,2059),$i)})
 if(($labels -join ',') -ne 'Mix,Game,Microphone'){throw "Wrong labels $labels"};Write-Output "PASS playback labels: $labels"
 Click $window 2058;[AudioBorderUi]::SetForegroundWindow($window)|Out-Null;Start-Sleep 1
 [AudioBorderUi]::Screenshot($window,(Join-Path $root 'fullscreen.png'));Click $window 2058
 [AudioBorderUi]::SetWindowPos($window,[IntPtr]::Zero,20,20,850,775,4)|Out-Null;Click $window 2002
 [AudioBorderUi]::Screenshot($window,(Join-Path $root 'settings-small.png'))
 # Physical mouse/focus remains with test; send the focus notification to the last settings control, which must scroll into view.
 $last=[AudioBorderUi]::Child($window,1036);[AudioBorderUi]::SendMessage($window,0x111,[IntPtr]((0x100 -shl 16)+1036),$last)|Out-Null
 [AudioBorderUi]::Screenshot($window,(Join-Path $root 'settings-scrolled.png'))
 SetCheck $window 2038 $true;Apply;Click $window 2000
 [AudioBorderUi]::PostMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Start-Sleep 1
 if([AudioBorderUi]::IsWindowVisible($window) -or $app.HasExited){throw 'Close-to-tray failed'}
 [AudioBorderUi]::SendMessage($window,0x111,[IntPtr]2091,[IntPtr]::Zero)|Out-Null;WaitStatus 1027 'Saved clip:'|Write-Output
 [AudioBorderUi]::SendMessage($window,0x111,[IntPtr]2090,[IntPtr]::Zero)|Out-Null
 if(![AudioBorderUi]::IsWindowVisible($window)){throw 'Tray open failed'}
 Write-Output 'PASS close-to-tray preserved replay; tray Save/Open commands'
 # Corrupt and incomplete files: background scan must report damage and exclude partial exports.
 [IO.File]::WriteAllText((Join-Path $folder 'damaged.mp4'),'not an MP4');[IO.File]::WriteAllText((Join-Path $folder 'ignored.partial.mp4'),'partial')
 Click $window 2001;Click $window 2050;Start-Sleep 2
 [AudioBorderUi]::Screenshot($window,(Join-Path $root 'gallery-damaged.png'))
 $rows=[AudioBorderUi]::SendMessage([AudioBorderUi]::Child($window,2053),0x1004,[IntPtr]::Zero,[IntPtr]::Zero)
 $expected=@(Get-ChildItem $folder -Filter '*.mp4'|Where-Object {$_.Name -notmatch 'partial'}).Count
 if($rows -ne $expected){throw 'Incomplete gallery entry leak'}
 SetText 2051 'damaged';Start-Sleep 1;if([AudioBorderUi]::SendMessage([AudioBorderUi]::Child($window,2053),0x1004,[IntPtr]::Zero,[IntPtr]::Zero) -ne 1){throw 'Search failed'}
 Write-Output 'PASS gallery updates, excludes partial files, searchable damaged entry'
 # Exit while replay is active and a save is accepted; do not kill it on the success path.
 Click $window 2000;Click $window 1024
 [AudioBorderUi]::SendMessage($window,0x111,[IntPtr]2094,[IntPtr]::Zero)|Out-Null
 if(!$app.WaitForExit(15000)){throw 'Actual exit failed to finish'}
 foreach($file in Get-ChildItem $folder -Filter '*.mp4'|Where-Object {$_.Name -notmatch 'damaged|partial'}){$r=& (Join-Path $project 'build\milestone6\Release\ReDotReplaySyncVerify.exe') $file.FullName;$r|Set-Content ($file.FullName+'.verify.txt');if($LASTEXITCODE -ne 0){throw 'Finalized file failed decoding'}}
 Write-Output 'PASS active replay/accepted export clean shutdown and independent decoding'
} finally {
 if($app -and !$app.HasExited){[AudioBorderUi]::SendMessage($window,0x111,[IntPtr]2094,[IntPtr]::Zero)|Out-Null;if(!$app.WaitForExit(15000)){Stop-Process -Id $app.Id -Force}}
 if($source -and !$source.HasExited){[IO.File]::WriteAllText((Join-Path $root 'stop.signal'),'stop');if(!$source.WaitForExit(5000)){Stop-Process -Id $source.Id -Force}}
 foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key])}
}
