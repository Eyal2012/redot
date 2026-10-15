param([string]$BuildDirectory='build', [ValidateSet('Debug','Release')][string]$Configuration='Release', [switch]$Interactive)
# Desktop/audio integration is opt-in. Never run during ordinary build/quiet verification.
if (-not $Interactive) {
  Write-Output 'SKIPPED interactive check: may activate owned windows, use desktop input, or play audio. Use tests\run_quiet_tests.ps1 by default; add -Interactive only when you are ready.'
  return
}

$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$root=Join-Path $project ('build\milestone6\smoke-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
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

$app=Start-Process (Join-Path $project "$BuildDirectory\$Configuration\ReDot.exe") -PassThru
try {
Start-Sleep 3
$window=[AudioBorderUi]::Window($app.Id,'PandaClipMainWindow')
if($window -eq [IntPtr]::Zero){throw 'App did not open'}
[AudioBorderUi]::Screenshot($window,(Join-Path $root 'capture.png'))
Click $window 2002
Start-Sleep 1
[AudioBorderUi]::Screenshot($window,(Join-Path $root 'settings.png'))
Write-Output "Artifacts: $root"
Write-Output ([AudioBorderUi]::Text([AudioBorderUi]::Child($window,2020)))
} finally {
[AudioBorderUi]::PostMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
Start-Sleep 2
if(!$app.HasExited){Stop-Process -Id $app.Id -Force}
foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key])}
}
