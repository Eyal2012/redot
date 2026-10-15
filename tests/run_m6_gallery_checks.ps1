param([string]$ClipFolder="", [switch]$Interactive)
# Desktop/audio integration is opt-in. Never run during ordinary build/quiet verification.
if (-not $Interactive) {
  Write-Output 'SKIPPED interactive check: may activate owned windows, use desktop input, or play audio. Use tests\run_quiet_tests.ps1 by default; add -Interactive only when you are ready.'
  return
}

$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$root=Join-Path $project ('build\milestone6\gallery-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
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

if(!$ClipFolder){$latest=Get-ChildItem (Join-Path $project 'build\milestone6') -Directory -Filter 'daily-*' | Sort-Object Name | Select-Object -Last 1;if(!$latest){throw 'Run run_m6_daily_checks.ps1 first, or pass -ClipFolder'};$ClipFolder=Join-Path $latest.FullName 'profile\Videos\PandaClip'}
$clips=$ClipFolder
$settings=Join-Path $env:LOCALAPPDATA 'PandaClip\settings-v1.txt';New-Item -ItemType Directory -Path (Split-Path $settings) -Force|Out-Null
[IO.File]::WriteAllText($settings,('PandaClip settings 1'+"`n"+'"folder" "'+$clips.Replace('\','\\')+'"'+"`n"),[Text.UTF8Encoding]::new($false))
$app=Start-Process (Join-Path $project 'build\milestone6\Release\ReDot.exe') -PassThru
try{
 Start-Sleep 2;$window=[AudioBorderUi]::Window($app.Id,'PandaClipMainWindow');Click $window 2001;WaitStatus 2062 'finalized MP4'|Write-Output
 Write-Output "Artifacts: $root"
 $list=[AudioBorderUi]::Child($window,2053)
 $count=[AudioBorderUi]::SendMessage($list,0x1004,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()
 if($count -lt 1){throw 'No finalized clips in gallery'}
 foreach($sort in 0..3){SetChoice $window 2052 $sort;Start-Sleep -Milliseconds 100;if([AudioBorderUi]::SendMessage($list,0x1004,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne $count){throw 'Sort lost gallery entries'}}
 SetChoice $window 2052 0
 Write-Output 'PASS all four sort selections retain finalized gallery entries'
 SetText 2051 'Clip_';Start-Sleep -Milliseconds 600
 $list=[AudioBorderUi]::Child($window,2053);$point=[IntPtr]((60 -shl 16)+100)
 foreach($m in @(0x201,0x202,0x203,0x202)){[AudioBorderUi]::PostMessage($list,$m,[IntPtr]::Zero,$point)|Out-Null}
 WaitStatus 2063 'Playback [0-9]'|Write-Output;Click $window 2058;[AudioBorderUi]::SetForegroundWindow($window)|Out-Null;Start-Sleep 1
 [AudioBorderUi]::Screenshot($window,(Join-Path $root 'fullscreen.png'));Click $window 2058
 Click $window 2002;[AudioBorderUi]::Screenshot($window,(Join-Path $root 'settings.png'))
 Click $window 2000;[AudioBorderUi]::Screenshot($window,(Join-Path $root 'capture.png'))
}finally{[AudioBorderUi]::SendMessage($window,0x111,[IntPtr]2094,[IntPtr]::Zero)|Out-Null;if(!$app.WaitForExit(15000)){Stop-Process $app.Id -Force};foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key])}}
