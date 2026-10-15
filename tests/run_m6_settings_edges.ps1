param([string]$Executable='build\milestone6\Release\ReDot.exe', [switch]$Interactive)
# Desktop/audio integration is opt-in. Never run during ordinary build/quiet verification.
if (-not $Interactive) {
  Write-Output 'SKIPPED interactive check: may activate owned windows, use desktop input, or play audio. Use tests\run_quiet_tests.ps1 by default; add -Interactive only when you are ready.'
  return
}

$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$root=Join-Path $project ('build\milestone6\settings-edges-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $root | Out-Null
$previous=@{};foreach($key in @('TEMP','TMP','USERPROFILE','LOCALAPPDATA')){$previous[$key]=[Environment]::GetEnvironmentVariable($key)}
$env:TEMP=$root;$env:TMP=$root;$env:USERPROFILE=Join-Path $root 'profile';$env:LOCALAPPDATA=Join-Path $root 'local'
New-Item -ItemType Directory -Path $env:USERPROFILE,$env:LOCALAPPDATA | Out-Null
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;using System.Text;using System.Runtime.InteropServices;using System.Drawing;
public static class AudioBorderUi {
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int width,int height,uint flags);
 public static void Resize(IntPtr h,int width,int height){var old=SetThreadDpiAwarenessContext((IntPtr)(-4));SetWindowPos(h,IntPtr.Zero,30,30,width,height,4);SetThreadDpiAwarenessContext(old);}
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

$settings=Join-Path $env:LOCALAPPDATA 'PandaClip\settings-v1.txt';New-Item -ItemType Directory -Path (Split-Path $settings) -Force|Out-Null
function Launch {
 $script:app=Start-Process (Join-Path $project $Executable) -PassThru;Start-Sleep 2
 $script:window=[AudioBorderUi]::Window($app.Id,'PandaClipMainWindow');if($window -eq [IntPtr]::Zero){throw 'App missing'}
}
function ExitOwned {
 [AudioBorderUi]::SendMessage($window,0x111,[IntPtr]2094,[IntPtr]::Zero)|Out-Null;if(!$app.WaitForExit(15000)){throw 'Clean exit timeout'}
}
function ChoiceText([int]$id){$control=[AudioBorderUi]::Child($window,$id);$index=[int][AudioBorderUi]::SendMessage($control,0x147,[IntPtr]::Zero,[IntPtr]::Zero);if($index -ge 0){return [AudioBorderUi]::Item($control,$index)};return ''}
$app=$null;$window=[IntPtr]::Zero
try{
 Write-Output "Artifacts: $root"
 $initial='PandaClip settings 1'+"`n"+'"micId" "PandaClip absent microphone endpoint"'+"`n"+'"outputId" "PandaClip absent playback endpoint"'+"`n"+'"sourceExe" "PandaClip absent executable"'+"`n"+'"sourceTitle" "PandaClip absent source"'+"`n"+'"duration" "37"'+"`n"+'"width" "1600"'+"`n"+'"height" "900"'+"`n"
 [IO.File]::WriteAllText($settings,$initial,[Text.UTF8Encoding]::new($false));Launch
 if((Text 1005) -notmatch 'unavailable|disconnected'){throw 'Missing source selection not reported'}
 Click $window 2002
 if((ChoiceText 2030) -notmatch 'Custom'){throw 'Restored custom dimensions misleading preset'}
 if((ChoiceText 1016) -notmatch 'unavailable|disconnected' -or (ChoiceText 2036) -notmatch 'unavailable|disconnected'){throw 'Missing devices not preserved'}
 Click $window 2037
 for($i=0;$i -lt 30;$i++){if((Text 2013) -match 'lists refreshed|completed with errors'){break};Start-Sleep -Milliseconds 100}
 if((Text 2013) -notmatch 'lists refreshed|completed with errors'){throw 'Async device refresh did not complete'}
 if((ChoiceText 1016) -notmatch 'unavailable|disconnected' -or (ChoiceText 2036) -notmatch 'unavailable|disconnected'){throw 'Refresh silently replaced missing endpoint'}
 Click $window 1037;Start-Sleep 2;Write-Output "Unavailable mic: $(Text 1038)"
 if((Text 1038) -notmatch 'fail|could not|not found|unavailable|HRESULT'){throw 'Missing mic startup lacks useful error'}
 [AudioBorderUi]::Resize($window,875,775);Start-Sleep -Milliseconds 200
 Write-Output "UI DPI: $([AudioBorderUi]::GetDpiForWindow($window)); physical window 875x775"
 [AudioBorderUi]::Screenshot($window,(Join-Path $root 'settings-small.png'))
 $last=[AudioBorderUi]::Child($window,1036);[AudioBorderUi]::SendMessage($window,0x111,[IntPtr]((0x100 -shl 16)+1036),$last)|Out-Null
 [AudioBorderUi]::Screenshot($window,(Join-Path $root 'settings-scroll-bottom.png'))
 # Post commands because confirmation dialog is modal on the owned UI thread.
 foreach($answer in @(7,6)){
  [AudioBorderUi]::PostMessage($window,0x111,[IntPtr]2011,[IntPtr]::Zero)|Out-Null
  $dialog=[IntPtr]::Zero;for($i=0;$i -lt 20;$i++){Start-Sleep -Milliseconds 100;$dialog=[AudioBorderUi]::Window($app.Id,'#32770');if($dialog -ne [IntPtr]::Zero){break}}
  if($dialog -eq [IntPtr]::Zero){throw 'Restore Defaults confirmation missing'}
  [AudioBorderUi]::PostMessage([AudioBorderUi]::Child($dialog,$answer),0xf5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Start-Sleep -Milliseconds 300
  if($answer -eq 7 -and (Text 2020) -ne '1600'){throw 'Cancel restored defaults unexpectedly'}
 }
 if((Text 2020) -ne '1920' -or (Text 1025) -ne '60' -or (Text 1028) -ne '512' -or (ChoiceText 1016) -notmatch '^Default device'){throw 'Restore Defaults mismatch'}
 Write-Output 'PASS missing source and endpoints retained; Refresh has no fallback; Restore Defaults Cancel/Confirm'
 ExitOwned
 # Unknown/corrupt version loads defaults without silently replacing the damaged file.
 [IO.File]::WriteAllText($settings,'PandaClip settings 999 damaged',[Text.UTF8Encoding]::new($false));Launch;Start-Sleep 1
 if((Text 2020) -ne '1920' -or (Text 2013) -notmatch 'unreadable|outdated'){throw 'Corrupt settings did not recover visibly'}
 if([IO.File]::ReadAllText($settings) -ne 'PandaClip settings 999 damaged'){throw 'Corrupt settings overwritten before Apply'}
 Click $window 2002;Apply;Start-Sleep 1
 if([IO.File]::ReadAllText($settings) -notmatch '^PandaClip settings 1'){throw 'Apply did not replace corrupt settings'}
 Write-Output 'PASS unsupported settings version recovers safely; explicit Apply replaces it'
 ExitOwned
}finally{
 if($app -and !$app.HasExited){[AudioBorderUi]::SendMessage($window,0x111,[IntPtr]2094,[IntPtr]::Zero)|Out-Null;if(!$app.WaitForExit(15000)){Stop-Process -Id $app.Id -Force}}
 foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key])}
}

