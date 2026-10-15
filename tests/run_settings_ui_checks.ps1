param([string]$Executable='build\interface-refresh\Release\ReDot.exe',[switch]$ObserveOnly,[switch]$Trace, [switch]$Interactive)
# Desktop/audio integration is opt-in. Never run during ordinary build/quiet verification.
if (-not $Interactive) {
  Write-Output 'SKIPPED interactive check: may activate owned windows, use desktop input, or play audio. Use tests\run_quiet_tests.ps1 by default; add -Interactive only when you are ready.'
  return
}

$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$root=Join-Path $project ('build\settings-ui\check-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $root | Out-Null
$previous=@{};foreach($key in @('TEMP','TMP','USERPROFILE','LOCALAPPDATA','PANDACLIP_UI_TRACE')){$previous[$key]=[Environment]::GetEnvironmentVariable($key)}
$env:TEMP=$root;$env:TMP=$root;$env:USERPROFILE=Join-Path $root 'profile';$env:LOCALAPPDATA=Join-Path $root 'local'
New-Item -ItemType Directory -Path $env:USERPROFILE,$env:LOCALAPPDATA | Out-Null
if($Trace){$env:PANDACLIP_UI_TRACE='1'}
Add-Type -TypeDefinition @'
using System;using System.Text;using System.Runtime.InteropServices;using System.Diagnostics;using System.Collections.Generic;
public static class SettingsUi {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p,IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h,EnumProc p,IntPtr l);
 [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,StringBuilder b,int n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,StringBuilder l);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr result);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool GetScrollInfo(IntPtr h,int bar,ref Scroll s);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int height,uint f);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int mode);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("kernel32.dll")] public static extern IntPtr OpenThread(uint access,bool inherit,uint id);
 [DllImport("kernel32.dll")] public static extern bool GetThreadTimes(IntPtr h,out long c,out long e,out long k,out long u);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
 [DllImport("user32.dll")]public static extern bool GetScrollBarInfo(IntPtr h,int obj,ref Bar b);
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out Point p);
 [DllImport("user32.dll")]public static extern IntPtr WindowFromPoint(Point p);
 [DllImport("user32.dll")]public static extern void mouse_event(uint f,uint x,uint y,uint d,UIntPtr extra);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h,uint flag);
 [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint id,ref Gui g);
 public struct Gui{public uint size,flags;public IntPtr active,focus,capture,menu,move,caret;public Rect caretRect;}
 public static IntPtr Focus(IntPtr window){uint pid;uint id=GetWindowThreadProcessId(window,out pid);var g=new Gui{size=(uint)Marshal.SizeOf(typeof(Gui))};GetGUIThreadInfo(id,ref g);return g.focus;}
 public static void Prime(IntPtr window){SetForegroundWindow(window);SetWindowPos(window,(IntPtr)(-1),0,0,0,0,3);IntPtr page=Page(window);Rect r;GetWindowRect(page,out r);SetCursorPos(r.Left+2,r.Top+2);System.Threading.Thread.Sleep(120);if(GetAncestor(WindowFromPoint(new Point{X=r.Left+2,Y=r.Top+2}),2)==window){mouse_event(2,0,0,0,UIntPtr.Zero);mouse_event(4,0,0,0,UIntPtr.Zero);System.Threading.Thread.Sleep(120);}}
 public static bool NativeWheel(IntPtr window,IntPtr control){SetForegroundWindow(window);SetWindowPos(window,(IntPtr)(-1),0,0,0,0,3);Point old;GetCursorPos(out old);Rect r;GetWindowRect(control,out r);int x=(r.Left+r.Right)/2,y=r.Top+10;try{SetCursorPos(x,y);System.Threading.Thread.Sleep(120);if(GetAncestor(WindowFromPoint(new Point{X=x,Y=y}),2)!=window)return false;mouse_event(0x800,0,0,unchecked((uint)(-120)),UIntPtr.Zero);System.Threading.Thread.Sleep(200);return true;}finally{SetCursorPos(old.X,old.Y);SetWindowPos(window,(IntPtr)(-2),0,0,0,0,3);}}
 public static IntPtr Label(IntPtr window,string text){IntPtr result=IntPtr.Zero;EnumChildWindows(window,(h,l)=>{if(Text(h)==text){result=h;return false;}return true;},IntPtr.Zero);return result;}
 public struct Point{public int X,Y;}
 public struct Rect{public int Left,Top,Right,Bottom;}
 [StructLayout(LayoutKind.Sequential)]public struct Bar{public uint size;public Rect rect;public int line,top,bottom,reserved;[MarshalAs(UnmanagedType.ByValArray,SizeConst=6)]public uint[] state;}
 public static IntPtr Page(IntPtr window){IntPtr h=GetParent(Child(window,2020));if(Info(h).page==0)h=GetParent(h);return h;}
 public static string DragReport;
 public static bool Drag(IntPtr window,IntPtr page){SetForegroundWindow(window);SetWindowPos(window,(IntPtr)(-1),0,0,0,0,3);Point old;GetCursorPos(out old);var b=new Bar{size=(uint)Marshal.SizeOf(typeof(Bar)),state=new uint[6]};GetScrollBarInfo(page,-5,ref b);int x=(b.rect.Left+b.rect.Right)/2,y=b.rect.Top+(b.top+b.bottom)/2;int before=Info(page).pos;bool down=false;try{SetCursorPos(x,y);System.Threading.Thread.Sleep(100);IntPtr hit=WindowFromPoint(new Point{X=x,Y=y});var cls=new StringBuilder(128);GetClassName(hit,cls,128);DragReport="page="+page+" hit="+hit+" class="+cls+" bar="+b.rect.Left+","+b.rect.Top+","+b.rect.Right+","+b.rect.Bottom+" thumb="+b.top+","+b.bottom+" before="+before;
if(hit!=page)return false;mouse_event(2,0,0,0,UIntPtr.Zero);down=true;System.Threading.Thread.Sleep(100);for(int i=1;i<=5;i++){SetCursorPos(x,y+i*20);System.Threading.Thread.Sleep(50);}mouse_event(4,0,0,0,UIntPtr.Zero);down=false;System.Threading.Thread.Sleep(100);DragReport+=" after="+Info(page).pos;return Info(page).pos>before;}finally{if(down)mouse_event(4,0,0,0,UIntPtr.Zero);SetCursorPos(old.X,old.Y);SetWindowPos(window,(IntPtr)(-2),0,0,0,0,3);}}

 [StructLayout(LayoutKind.Sequential)]public struct Scroll{public uint size,mask;public int min,max;public uint page;public int pos,track;}
 public static IntPtr Window(int pid,string cls){IntPtr result=IntPtr.Zero;EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);var c=new StringBuilder(256);GetClassName(h,c,256);if(p==pid&&c.ToString().StartsWith(cls,StringComparison.Ordinal)&&(cls!="WindowsForms"||Text(h)=="PandaClip Timed Flash and Beep Test")){result=h;return false;}return true;},IntPtr.Zero);return result;}
 public static IntPtr Child(IntPtr h,int id){IntPtr result=IntPtr.Zero;EnumChildWindows(h,(c,l)=>{if(GetDlgCtrlID(c)==id){result=c;return false;}return true;},IntPtr.Zero);return result;}
 public static string Text(IntPtr h){var b=new StringBuilder(4096);SendMessage(h,13,(IntPtr)b.Capacity,b);return b.ToString();}
 public static string Item(IntPtr h,int index){var b=new StringBuilder(1024);SendMessage(h,0x148,(IntPtr)index,b);return b.ToString();}
 public static Scroll Info(IntPtr h){var s=new Scroll{size=(uint)Marshal.SizeOf(typeof(Scroll)),mask=23};GetScrollInfo(h,1,ref s);return s;}
 public static void Thumb(IntPtr h,int position){SendMessage(h,0x115,(IntPtr)((position<<16)|4),IntPtr.Zero);}
 public static void Wheel(IntPtr h,int delta){Rect r;GetWindowRect(h,out r);long point=(((r.Top+10)&65535)<<16)|((r.Left+10)&65535);SendMessage(h,0x20a,(IntPtr)(delta<<16),(IntPtr)point);}
 public static double Cpu(IntPtr h){uint p;uint id=GetWindowThreadProcessId(h,out p);IntPtr t=OpenThread(0x40,false,id);long c,e,k,u;GetThreadTimes(t,out c,out e,out k,out u);CloseHandle(t);return (k+u)/10000.0;}
 public static double[] Measure(IntPtr h,IntPtr page){var values=new List<double>();for(int i=0;i<80;i++){Thumb(page,(i%2==0)?0:450);var timer=Stopwatch.StartNew();Wheel(page,(i%2==0)?-120:120);values.Add(timer.Elapsed.TotalMilliseconds);}values.Sort();return new[]{values[40],values[76],values[79]};}
 public static double[] Ping(IntPtr h){var values=new List<double>();for(int i=0;i<80;i++){UIntPtr result;var timer=Stopwatch.StartNew();if(SendMessageTimeout(h,0,IntPtr.Zero,IntPtr.Zero,2,2000,out result)==IntPtr.Zero)throw new Exception("UI timeout");values.Add(timer.Elapsed.TotalMilliseconds);System.Threading.Thread.Sleep(10);}values.Sort();return new[]{values[40],values[76],values[79]};}
}
'@
function Child([int]$id){[SettingsUi]::Child($window,$id)}
function Click([int]$id){[SettingsUi]::SendMessage((Child $id),0xf5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
function Choice([int]$id,[int]$index){$h=Child $id;[SettingsUi]::SendMessage($h,0x14e,[IntPtr]$index,[IntPtr]::Zero)|Out-Null;[SettingsUi]::SendMessage($window,0x111,[IntPtr](65536+$id),$h)|Out-Null}
function Wait([int]$id,[string]$pattern){for($i=0;$i -lt 40;$i++){Start-Sleep -Milliseconds 250;$s=[SettingsUi]::Text((Child $id));if($s -match $pattern){return $s}};throw "Timeout $id : $s"}
$failures=[Collections.Generic.List[string]]::new();$measurements=[Collections.Generic.List[object]]::new()
function Check([bool]$ok,[string]$name){if($ok){Write-Output "PASS $name"}else{$failures.Add($name);Write-Output "FAIL $name"}}
$app=$null;$source=$null;$window=[IntPtr]::Zero
try {
 $go=Join-Path $root 'go';$stop=Join-Path $root 'stop';$wav=Join-Path $root 'beep.wav'
 $args="-NoProfile -ExecutionPolicy Bypass -File `"$PSScriptRoot\replay_sync_stimulus.ps1`" -Interactive -GoFile `"$go`" -StopFile `"$stop`" -BeepPath `"$wav`" -DurationSeconds 240"
 $source=Start-Process powershell.exe -ArgumentList $args -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $root 'source.out') -RedirectStandardError (Join-Path $root 'source.err')
 $settings=Join-Path $env:LOCALAPPDATA 'PandaClip\settings-v1.txt';New-Item -ItemType Directory -Path (Split-Path $settings) | Out-Null
 [IO.File]::WriteAllText($settings,"PandaClip settings 1`n`"width`" `"1280`"`n`"height`" `"720`"`n`"fps`" `"30`"`n`"gameMode`" `"0`"`n`"gameEnabled`" `"0`"`n`"micEnabled`" `"0`"`n",[Text.UTF8Encoding]::new($false))
 $app=Start-Process (Join-Path $project $Executable) -PassThru
 Start-Sleep 3;$window=[SettingsUi]::Window($app.Id,'PandaClipMainWindow');if($window -eq [IntPtr]::Zero){throw 'App did not open'}
 [SettingsUi]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
 [SettingsUi]::SetWindowPos($window,[IntPtr]::Zero,180,120,1000,850,4)|Out-Null
 Write-Output "Artifacts: $root | EXE: $Executable | DPI: $([SettingsUi]::GetDpiForWindow($window))"
 for($i=0;$i -lt 15;$i++){$sourceWindow=[SettingsUi]::Window($source.Id,'WindowsForms');if($sourceWindow -ne [IntPtr]::Zero){[SettingsUi]::ShowWindow($sourceWindow,5)|Out-Null;break};Start-Sleep 1}
 Click 1004
 $combo=Child 1001;$count=[SettingsUi]::SendMessage($combo,0x146,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32();$index=-1
 for($i=0;$i -lt $count;$i++){if([SettingsUi]::Item($combo,$i) -eq 'PandaClip Timed Flash and Beep Test'){$index=$i;break}}
 if($index -lt 0){throw 'Owned source not found'};Choice 1001 $index;[IO.File]::WriteAllText($go,'go')
 foreach($mode in @('off','recording','replay')) {
  if($mode -eq 'recording'){Click 2000;Click 1010;Wait 1012 'Recording active'|Write-Output}
  if($mode -eq 'replay'){Click 2000;Click 1023;Start-Sleep 3}
  Click 2002;Choice 2070 7;Start-Sleep -Milliseconds 1500;$page=[SettingsUi]::Page($window);$cpu=[SettingsUi]::Cpu($window);$fileTime=(Get-Item $settings).LastWriteTimeUtc
  $scroll=[SettingsUi]::Measure($window,$page);$cpu=[SettingsUi]::Cpu($window)-$cpu;$ping=[SettingsUi]::Ping($window)
  $measurements.Add([pscustomobject]@{mode=$mode;wheelP50ms=$scroll[0];wheelP95ms=$scroll[1];wheelMaxms=$scroll[2];uiCpuMs=$cpu;pingP50ms=$ping[0];pingP95ms=$ping[1];pingMaxms=$ping[2]})
  Write-Output ($measurements[$measurements.Count-1]|ConvertTo-Json -Compress)
  [SettingsUi]::Thumb($page,0);$before=[SettingsUi]::Info($page).pos;[SettingsUi]::Wheel($page,-120);Check ([SettingsUi]::Info($page).pos -gt $before) "$mode wheel over viewport"
  [SettingsUi]::Thumb($page,0);$before=[SettingsUi]::Info($page).pos;[SettingsUi]::Wheel((Child 2020),-120);Check ([SettingsUi]::Info($page).pos -gt $before) "$mode wheel over edit"
  [SettingsUi]::Thumb($page,0);$before=[SettingsUi]::Info($page).pos;$choice=[SettingsUi]::SendMessage((Child 2030),0x147,[IntPtr]::Zero,[IntPtr]::Zero);[SettingsUi]::Wheel((Child 2030),-120);Check ([SettingsUi]::Info($page).pos -gt $before) "$mode wheel over dropdown";Check ([SettingsUi]::SendMessage((Child 2030),0x147,[IntPtr]::Zero,[IntPtr]::Zero) -eq $choice) "$mode wheel preserves dropdown choice"
  [SettingsUi]::Thumb($page,400);Write-Output ("Thumb info: "+([SettingsUi]::Info($page)|ConvertTo-Json -Compress));Check ([SettingsUi]::Info($page).pos -eq 400) "$mode thumb-position command"
  [SettingsUi]::SendMessage($page,0x115,[IntPtr]7,[IntPtr]::Zero)|Out-Null;$info=[SettingsUi]::Info($page);Check ($info.pos -eq $info.max-$info.page+1) "$mode bottom scroll reaches last setting"
  [SettingsUi]::Thumb($page,0);[SettingsUi]::SendMessage($page,0x100,[IntPtr]0x22,[IntPtr]::Zero)|Out-Null;Check ([SettingsUi]::Info($page).pos -gt 0) "$mode PageDown scrolling"
  $down=[SettingsUi]::Info($page).pos;[SettingsUi]::SendMessage($page,0x100,[IntPtr]0x21,[IntPtr]::Zero)|Out-Null;Check ([SettingsUi]::Info($page).pos -lt $down) "$mode PageUp scrolling"
  [SettingsUi]::SendMessage($page,0x100,[IntPtr]0x23,[IntPtr]::Zero)|Out-Null;$info=[SettingsUi]::Info($page);Check ($info.pos -eq $info.max-$info.page+1) "$mode End reaches bottom"
  [SettingsUi]::SendMessage($page,0x100,[IntPtr]0x24,[IntPtr]::Zero)|Out-Null;Check ([SettingsUi]::Info($page).pos -eq 0) "$mode Home reaches top"
  [SettingsUi]::Wheel($page,-60);Check ([SettingsUi]::Info($page).pos -eq 0) "$mode partial wheel delta retained"
  [SettingsUi]::Wheel($page,-60);Check ([SettingsUi]::Info($page).pos -gt 0) "$mode partial wheel deltas accumulate"
  $sliderRect=[SettingsUi+Rect]::new();$viewportRect=[SettingsUi+Rect]::new();[SettingsUi]::GetWindowRect((Child 1019),[ref]$sliderRect)|Out-Null;[SettingsUi]::GetWindowRect($page,[ref]$viewportRect)|Out-Null
  $sliderOffset=[SettingsUi]::Info($page).pos+[int](($sliderRect.Top-$viewportRect.Top)*96/[SettingsUi]::GetDpiForWindow($window))-40;[SettingsUi]::Thumb($page,$sliderOffset)
  $gain=[SettingsUi]::SendMessage((Child 1019),0x400,[IntPtr]::Zero,[IntPtr]::Zero);$before=[SettingsUi]::Info($page).pos;[SettingsUi]::Wheel((Child 1019),-120)
  Check ([SettingsUi]::Info($page).pos -gt $before -and [SettingsUi]::SendMessage((Child 1019),0x400,[IntPtr]::Zero,[IntPtr]::Zero) -eq $gain) "$mode wheel scrolls without changing gain slider"
  [SettingsUi]::Thumb($page,0);[SettingsUi]::Prime($window);Check ([SettingsUi]::Drag($window,$page)) "$mode actual scrollbar thumb drag";Write-Output ([SettingsUi]::DragReport)
  foreach($target in @([SettingsUi]::Label($window,'Output width (pixels)'),(Child 2020),(Child 2030))){[SettingsUi]::Thumb($page,0);$before=[SettingsUi]::Info($page).pos;$ok=[SettingsUi]::NativeWheel($window,$target);Check ($ok -and [SettingsUi]::Info($page).pos -gt $before) "$mode real wheel over label/edit/dropdown"}
  $choiceAfter=[SettingsUi]::SendMessage((Child 2030),0x147,[IntPtr]::Zero,[IntPtr]::Zero);Check ($choiceAfter -eq $choice) "$mode native wheel preserves resolution"
  Check ((Get-Item $settings).LastWriteTimeUtc -eq $fileTime) "$mode scrolling does not rewrite settings"
  if($mode -eq 'recording'){Click 2000;Click 1011;Wait 1012 'Saved MP4'|Write-Output}
  if($mode -eq 'replay'){Click 2000;Click 1024;Wait 1027 'Saved clip:'|Write-Output;Click 1023;Start-Sleep 2}
 }
 $measurements|ConvertTo-Json|Set-Content (Join-Path $root 'measurements.json')
 foreach($size in @(@(875,775),@(1450,1000))){[SettingsUi]::SetWindowPos($window,[IntPtr]::Zero,120,100,$size[0],$size[1],4)|Out-Null;Click 2002;Choice 2070 7;[SettingsUi]::SendMessage($page,0x115,[IntPtr]7,[IntPtr]::Zero)|Out-Null;$r=[SettingsUi+Rect]::new();[SettingsUi]::GetWindowRect((Child 2011),[ref]$r)|Out-Null;$p=[SettingsUi+Rect]::new();[SettingsUi]::GetWindowRect($page,[ref]$p)|Out-Null;Check ($r.Top -ge $p.Top -and $r.Bottom -le $p.Bottom) "last setting reachable at $($size[0])x$($size[1])"}
 [SettingsUi]::SetForegroundWindow($window)|Out-Null
 $focusBefore=[SettingsUi]::Focus($window);$visited=[Collections.Generic.HashSet[int]]::new()
 for($i=0;$i -lt 85;$i++){$focus=[SettingsUi]::Focus($window);if($focus -eq [IntPtr]::Zero){$focus=$page};[SettingsUi]::PostMessage($focus,0x100,[IntPtr]9,[IntPtr]::Zero)|Out-Null;Start-Sleep -Milliseconds 25;$current=[SettingsUi]::Focus($window);[void]$visited.Add([SettingsUi]::GetDlgCtrlID($current))}
 Write-Output ("Tab control IDs: "+($visited -join ","))
 Check ($visited.Contains(2011) -and $visited.Contains(2020)) 'Tab navigation reaches first and last settings'
 Check ([SettingsUi]::Text((Child 2020)) -eq '1280') 'scroll/resize/keyboard preserve editable values'
 $folder=Join-Path $env:USERPROFILE 'Videos\PandaClip';foreach($clip in Get-ChildItem $folder -Filter '*.mp4'){& (Join-Path $project 'build\milestone6\Release\ReDotReplaySyncVerify.exe') $clip.FullName | Set-Content ($clip.FullName+'.verify.txt');Check ($LASTEXITCODE -eq 0) "independent decode $($clip.Name)"}
 Write-Output "Failed checks: $($failures.Count)";if($failures.Count -and !$ObserveOnly){throw ($failures -join '; ')}
} finally {
 if($app -and !$app.HasExited){[SettingsUi]::SendMessage($window,0x111,[IntPtr]2094,[IntPtr]::Zero)|Out-Null;if(!$app.WaitForExit(15000)){Stop-Process -Id $app.Id -Force}}
 if($source){[IO.File]::WriteAllText((Join-Path $root 'stop'),'stop');if(!$source.WaitForExit(5000)){Stop-Process -Id $source.Id -Force}}
 foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key])}
}
