param(
 [int]$Seconds=620,
 [ValidateSet('Both','Recording','Replay')][string]$Mode='Both',
 [switch]$HidePreview,
 [ValidateRange(-500,500)][int]$GameSync=0,
 [int]$RecordFaultFrame=0, [int]$ExportFaultPacket=0, [int]$ExportDelayMilliseconds=0,
 [switch]$ExportFinalizeFault, [int]$AudioFaultSeconds=0,
 [switch]$ExerciseInterruptions, [switch]$ExerciseReopen, [switch]$ExerciseGpu, [switch]$ExhaustRecovery, [switch]$FolderFault, [switch]$EnableMic, [switch]$SkipDecode
, [switch]$Interactive)
# Desktop/audio integration is opt-in. Never run during ordinary build/quiet verification.
if (-not $Interactive) {
  Write-Output 'SKIPPED interactive check: may activate owned windows, use desktop input, or play audio. Use tests\run_quiet_tests.ps1 by default; add -Interactive only when you are ready.'
  return
}

$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$previousTemp=$env:TEMP;$previousTmp=$env:TMP
$compileTemp=Join-Path $projectRoot 'build\milestone5\temp'
New-Item -ItemType Directory -Force -Path $compileTemp | Out-Null
$env:TEMP=$compileTemp;$env:TMP=$compileTemp
$appPath=Join-Path $projectRoot 'build\Release\ReDot.exe'
$verifierPath=Join-Path $projectRoot 'build\replay-stress\Release\ReDotReplaySyncVerify.exe'
$stimulusPath=Join-Path $PSScriptRoot 'replay_sync_stimulus.ps1'
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
 public static string Text(IntPtr h) { var b=new StringBuilder(1024); SendMessage(h,0x000d,(IntPtr)b.Capacity,b); return b.ToString(); }
 public static int Count(IntPtr h) { return (int)SendMessage(h,0x0146,IntPtr.Zero,IntPtr.Zero); }
 public static string Item(IntPtr h,int i) { int n=(int)SendMessage(h,0x0149,(IntPtr)i,IntPtr.Zero); if(n<0||n>4096)return ""; var b=new StringBuilder(n+1); SendMessage(h,0x0148,(IntPtr)i,b); return b.ToString(); }
}
'@
Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public static class ReplaySyncNative{[DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);}'


Add-Type -TypeDefinition 'using System;using System.Text;using System.Runtime.InteropServices;public static class ReliabilityNative{public delegate bool Callback(IntPtr h,IntPtr l);[DllImport("user32.dll")]static extern bool EnumWindows(Callback c,IntPtr l);[DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);[DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,StringBuilder b,int n);[DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetWindowText(IntPtr h,StringBuilder b,int n);[DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int n);public static IntPtr FindOwned(string text,int pid,bool cls){IntPtr result=IntPtr.Zero;EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);var b=new StringBuilder(256);if(cls)GetClassName(h,b,256);else GetWindowText(h,b,256);if(p==pid&&b.ToString()==text){result=h;return false;}return true;},IntPtr.Zero);return result;}public static IntPtr ByTitle(string t,int pid){return FindOwned(t,pid,false);}public static IntPtr ByClass(string c,int pid){return FindOwned(c,pid,true);}}'

$runRoot=Join-Path $projectRoot ('build\milestone5\run-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+$Mode)
$profile=Join-Path $runRoot 'profile';$local=Join-Path $runRoot 'local'
New-Item -ItemType Directory -Path $profile,$local | Out-Null
$goFile=Join-Path $runRoot 'go.signal';$stopFile=Join-Path $runRoot 'stop.signal';$beep=Join-Path $runRoot 'beep.wav'
$variables=@('USERPROFILE','LOCALAPPDATA','PANDACLIP_SYNC_TRACE','PANDACLIP_TEST_RECORD_FAIL_FRAME','PANDACLIP_TEST_EXPORT_FAIL_PACKET','PANDACLIP_TEST_EXPORT_FAIL_FINALIZE','PANDACLIP_TEST_AUDIO_FAIL_SECONDS','PANDACLIP_TEST_HOOKS','PANDACLIP_TEST_EXPORT_DELAY_MS')
$previous=@{};foreach($name in $variables){$previous[$name]=[Environment]::GetEnvironmentVariable($name)}
$env:USERPROFILE=$profile;$env:LOCALAPPDATA=$local;$env:PANDACLIP_SYNC_TRACE='1'
$env:PANDACLIP_TEST_RECORD_FAIL_FRAME="$RecordFaultFrame";$env:PANDACLIP_TEST_EXPORT_FAIL_PACKET="$ExportFaultPacket";$env:PANDACLIP_TEST_EXPORT_FAIL_FINALIZE="$(if($ExportFinalizeFault){1}else{0})";$env:PANDACLIP_TEST_AUDIO_FAIL_SECONDS="$AudioFaultSeconds";$env:PANDACLIP_TEST_HOOKS='1';$env:PANDACLIP_TEST_EXPORT_DELAY_MS="$ExportDelayMilliseconds"
$target=$null;$app=$null
function Click([IntPtr]$parent,[int]$id){[ReplaySyncUi]::SendMessage([ReplaySyncUi]::Child($parent,$id),0x00f5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
function Save([IntPtr]$window){Click $window 1024;Start-Sleep 1;Write-Output ('save: '+[ReplaySyncUi]::Text([ReplaySyncUi]::Child($window,1027)))}
try {
 $launchArgs="-NoProfile -ExecutionPolicy Bypass -File `"$stimulusPath`" -Interactive -GoFile `"$goFile`" -StopFile `"$stopFile`" -BeepPath `"$beep`" -DurationSeconds $($Seconds+20)"
 $target=Start-Process powershell.exe -ArgumentList $launchArgs -PassThru -RedirectStandardOutput (Join-Path $runRoot 'stimulus.out') -RedirectStandardError (Join-Path $runRoot 'stimulus.err')
 for($i=0;$i -lt 60;$i++){Start-Sleep 1;$targetWindow=[ReliabilityNative]::ByTitle('PandaClip Timed Flash and Beep Test',$target.Id);if($targetWindow -ne [IntPtr]::Zero){break}}
 if($targetWindow -eq [IntPtr]::Zero){throw 'Stimulus window unavailable.'}
 $app=Start-Process $appPath -PassThru;Start-Sleep 4
 $window=(Get-Process -Id $app.Id).MainWindowHandle
 $combo=[ReplaySyncUi]::Child($window,1001);$selected=-1
 for($i=0;$i -lt [ReplaySyncUi]::Count($combo);$i++){if([ReplaySyncUi]::Item($combo,$i) -eq 'PandaClip Timed Flash and Beep Test'){$selected=$i;break}}
 if($selected -lt 0){throw 'Stimulus missing from source selector.'}
 [ReplaySyncUi]::SendMessage($combo,0x014e,[IntPtr]$selected,[IntPtr]::Zero)|Out-Null
 Click $window 1029;Start-Sleep 1
 $diag=[ReliabilityNative]::ByClass('PandaClipDiagnostics',$app.Id)
 if($diag -eq [IntPtr]::Zero){throw 'Diagnostics window was not found; cannot verify settings.'}
 [ReplaySyncUi]::SendMessage([ReplaySyncUi]::Child($diag,1031),0x000c,[IntPtr]::Zero,[Text.StringBuilder]::new("$GameSync"))|Out-Null
 Click $diag 1033
 if($HidePreview){Click $diag 1034}
 $checked=[ReplaySyncUi]::SendMessage([ReplaySyncUi]::Child($diag,1034),0x00f0,[IntPtr]::Zero,[IntPtr]::Zero)
 if(($HidePreview -and $checked -ne [IntPtr]::Zero) -or (!$HidePreview -and $checked -eq [IntPtr]::Zero)){throw 'Preview visibility control did not match the requested test configuration.'}
 if([ReplaySyncUi]::Text([ReplaySyncUi]::Child($diag,1031)) -ne "$GameSync"){throw 'Sync setting UI did not apply.'}
 Write-Output ('verified sync control: '+[ReplaySyncUi]::Text([ReplaySyncUi]::Child($diag,1033)))
 [ReliabilityNative]::ShowWindow($diag,0)|Out-Null
 if($EnableMic){Click $window 1015}
 if($Mode -ne 'Replay'){Click $window 1010;Start-Sleep 3}
 if($Mode -ne 'Recording'){Click $window 1023;Start-Sleep 3}
 [IO.File]::WriteAllText($goFile,'go')
 $clock=[Diagnostics.Stopwatch]::StartNew();$nextReport=30;$firstSave=$false;$interrupted=$false;$reopened=$false;$gpuInterrupted=$false
 Write-Output "Started $Mode; preview hidden=$HidePreview; adjustment=$GameSync ms; artifacts=$runRoot"
 while($clock.Elapsed.TotalSeconds -lt $Seconds){
  Start-Sleep 1
  $t=$clock.Elapsed.TotalSeconds
  if($t -ge $nextReport){
   Click $diag 1030;Start-Sleep -Milliseconds 250
   $process=Get-Process -Id $app.Id
   $state=[ReplaySyncUi]::Text([ReplaySyncUi]::Child($window,1012));$replay=[ReplaySyncUi]::Text([ReplaySyncUi]::Child($window,1027))
   Write-Output ('t={0:N1}s private={1:N1}MiB working={2:N1}MiB; {3}; {4}' -f $t,($process.PrivateMemorySize64/1MB),($process.WorkingSet64/1MB),$state,$replay)
   $nextReport+=30
  }
  if(!$firstSave -and $t -ge 75 -and $Mode -ne 'Recording'){Save $window;$firstSave=$true}
  if($ExerciseInterruptions -and !$interrupted -and $t -ge 12){
   # App notifications only; Windows itself stays awake and unlocked.
   [ReplaySyncNative]::PostMessage($window,0x02b1,[IntPtr]7,[IntPtr]::Zero)|Out-Null;Start-Sleep 3
   [ReplaySyncNative]::PostMessage($window,0x02b1,[IntPtr]8,[IntPtr]::Zero)|Out-Null;Start-Sleep 5
   [ReplaySyncNative]::PostMessage($window,0x0218,[IntPtr]4,[IntPtr]::Zero)|Out-Null;Start-Sleep 3
   [ReplaySyncNative]::PostMessage($window,0x0218,[IntPtr]18,[IntPtr]::Zero)|Out-Null
   $interrupted=$true
  }
  if($ExerciseReopen -and !$reopened -and $t -ge 42){
   [ReplaySyncNative]::PostMessage($targetWindow,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
   Start-Sleep 4
   Copy-Item -LiteralPath ($beep+'.source.csv') -Destination (Join-Path $runRoot 'first-source.csv')
   $target=Start-Process powershell.exe -ArgumentList $launchArgs -PassThru -RedirectStandardOutput (Join-Path $runRoot 'reopened.out') -RedirectStandardError (Join-Path $runRoot 'reopened.err')
   $reopened=$true;Write-Output 'Closed and reopened only the test source.'
  }
  if(($ExerciseGpu -or $ExhaustRecovery) -and !$gpuInterrupted -and $t -ge 12){
   [ReplaySyncNative]::PostMessage($window,0x8018,[IntPtr]$(if($ExhaustRecovery){2}else{1}),[IntPtr]::Zero)|Out-Null
   $gpuInterrupted=$true;Write-Output 'Injected capture/GPU interruption in PandaClip only.'
  }
 }
 $logPath=Join-Path $local 'PandaClip\logs\panda.log'
 $log=[IO.File]::ReadAllText($logPath)
 $beforeCfr=[regex]::Matches($log,'CFR slots=(\d+)')
 if($ExerciseReopen){
  $closed=$log.LastIndexOf('Lifecycle failure: Selected game window closed.')
  $recovered=$log.LastIndexOf(': new capture/encoder session, same selected source.')
  if($closed -lt 0 -or $recovered -le $closed){throw 'Source reopening did not create a verified new capture session after closure.'}
 }
 if($ExerciseInterruptions -and ([regex]::Matches($log,': new capture/encoder session, same selected source.').Count -lt 2)){throw 'Lock and sleep notification recovery did not both restart the selected source.'}
 if($ExerciseGpu -and $log -notmatch ': new capture/encoder session, same selected source.'){throw 'Injected capture/GPU interruption did not recover.'}
 if($ExhaustRecovery){
  $state=[ReplaySyncUi]::Text([ReplaySyncUi]::Child($window,1005))
  if($state -notmatch 'exhausted' -or [regex]::Matches($log,'Lifecycle failure: Recovery attempt [123]: Controlled').Count -ne 3){throw "Recovery did not stop after exactly three failed attempts: $state"}
 } elseif($Mode -ne 'Recording'){
  $live=[ReplaySyncUi]::Text([ReplaySyncUi]::Child($window,1027))
  if($live -notmatch 'Replay active|Replay recovered|Saved clip:' -or [ReplaySyncUi]::Text([ReplaySyncUi]::Child($window,1023)) -ne 'Disable Replay Buffer'){throw "Expected healthy replay after the exercise: $live"}
 }
 if($AudioFaultSeconds -and $log -notmatch 'Game audio restored on the same selected source.'){throw 'Game audio retry did not restore the same source.'}
 if($AudioFaultSeconds -and $EnableMic -and $log -notmatch 'Microphone restored on the same selected source.'){throw 'Microphone retry did not restore the same endpoint.'}
 if($FolderFault){
  if($Mode -ne 'Replay'){throw 'Folder fault operates only on an unsaved replay-only test folder.'}
  $folder=Join-Path $profile 'Videos\PandaClip'
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $folder)|Out-Null
  # A file at our own not-yet-created destination simulates an unavailable folder.
  [IO.File]::WriteAllText($folder,'PandaClip controlled test obstacle')
  Save $window
  $failed=[ReplaySyncUi]::Text([ReplaySyncUi]::Child($window,1027))
  if($failed -notmatch 'folder|error|failed'){throw "Unavailable folder was not reported: $failed"}
  Click $diag 1030
  for($attempt=0;$attempt -lt 20;$attempt++){
   Start-Sleep -Milliseconds 100
   if([IO.File]::ReadAllText($logPath) -match 'Lifecycle failure: Report export failed'){break}
  }
  if($attempt -eq 20){throw 'Diagnostics report failure was not recorded in the failure log.'}
  Write-Output 'Verified diagnostics report failure is surfaced and logged.'
  Remove-Item -LiteralPath $folder
  Write-Output 'Removed only the test obstacle; the next save must recreate the selected folder.'
 }
 if($Mode -ne 'Recording' -and !$ExhaustRecovery){
  if($ExportDelayMilliseconds){
   for($press=0;$press -lt 6;$press++){Click $window 1024}
   Start-Sleep -Milliseconds 250
   Click $diag 1030
   Write-Output ('burst save status: '+[ReplaySyncUi]::Text([ReplaySyncUi]::Child($window,1027)))
   Start-Sleep 5
  }else{Save $window;Save $window;Save $window}
 }
 if($ExportFaultPacket -or $ExportFinalizeFault){
  $status=[ReplaySyncUi]::Text([ReplaySyncUi]::Child($window,1027))
  if($status -notmatch 'Controlled replay (write|finalization) failure'){throw "Export fault was not reported: $status"}
  if(Get-ChildItem (Join-Path $profile 'Videos\PandaClip') -Filter '*.mp4' -ErrorAction SilentlyContinue){throw 'Failed export left a finalized or incomplete MP4.'}
 }
 if($ExportDelayMilliseconds){
  $clips=@(Get-ChildItem (Join-Path $profile 'Videos\PandaClip') -Filter '*_Clip_*.mp4')
  if($clips.Count -ne 2){throw "Six rapid saves must admit exactly two bounded exports; got $($clips.Count)."}
 }
 Click $diag 1030;Start-Sleep 2
 if($ExportFaultPacket -or $ExportFinalizeFault -or $ExportDelayMilliseconds -or $FolderFault){
  $afterCfr=[regex]::Matches([IO.File]::ReadAllText($logPath),'CFR slots=(\d+)')
  if(!$beforeCfr.Count -or !$afterCfr.Count -or [long]$afterCfr[$afterCfr.Count-1].Groups[1].Value -le [long]$beforeCfr[$beforeCfr.Count-1].Groups[1].Value){throw 'Encoding did not measurably continue during clip export/failure.'}
  Write-Output ('Verified continued encoding across export: CFR '+$beforeCfr[$beforeCfr.Count-1].Groups[1].Value+' -> '+$afterCfr[$afterCfr.Count-1].Groups[1].Value)
 }
 if($Mode -ne 'Replay'){
  Click $window 1011
  for($i=0;$i -lt 30;$i++){Start-Sleep 1;$result=[ReplaySyncUi]::Text([ReplaySyncUi]::Child($window,1012));if($result -match 'Saved MP4|Recording failed'){break}}
  Write-Output "record result: $result"
  if(!$RecordFaultFrame -and !$ExhaustRecovery -and $result -notmatch 'Saved MP4|Saved recording'){throw "Recording did not report finalized Saved: $result"}
 }
 if($Mode -ne 'Recording' -and !$ExhaustRecovery){Click $window 1023;Start-Sleep 2}
 Click $diag 1030;Start-Sleep 1
 if(!$SkipDecode){
  foreach($clip in Get-ChildItem (Join-Path $profile 'Videos\PandaClip') -Filter '*.mp4'){
   if($clip.Name -like '*.partial.mp4'){continue}
   Write-Output "verify: $($clip.FullName)"
   & $verifierPath $clip.FullName | Tee-Object -FilePath ($clip.FullName+'.verify.txt')
   if($LASTEXITCODE -ne 0){throw "Decode failed: $($clip.Name)"}
  }
 }
 Write-Output "Finished; artifacts: $runRoot"
} finally {
 [IO.File]::WriteAllText($stopFile,'stop')
 if($app -and !$app.HasExited){[ReplaySyncNative]::PostMessage((Get-Process -Id $app.Id).MainWindowHandle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;Start-Sleep 3;if(!$app.HasExited){Stop-Process -Id $app.Id -Force}}
 if($target -and !$target.HasExited){Stop-Process -Id $target.Id -Force}
 foreach($name in $variables){[Environment]::SetEnvironmentVariable($name,$previous[$name])}
 $env:TEMP=$previousTemp;$env:TMP=$previousTmp
}

