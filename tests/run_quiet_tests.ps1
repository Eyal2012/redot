param([string]$BuildDirectory='build\production',[ValidateSet('Debug','Release')][string]$Configuration='Release',[switch]$SkipBuild,[string]$Executable)
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
Set-Location $project
$build=Join-Path $project $BuildDirectory
$root=Join-Path $project ('build\quiet-tests\run-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+$Configuration)
New-Item -ItemType Directory -Force $root | Out-Null
Start-Transcript -Path (Join-Path $root 'transcript.txt') | Out-Null
try {
 if(-not $SkipBuild){
  & cmake -S . -B $build -G 'Visual Studio 17 2022' -A x64 -DREDOT_BUILD_REPLAY_STRESS=ON
  if($LASTEXITCODE -ne 0){throw 'Configure failed'}
  & cmake --build $build --config $Configuration --parallel
  if($LASTEXITCODE -ne 0){throw 'Build failed'}
 }
 $bin=Join-Path $build $Configuration
 function Run([string]$name,[string[]]$Arguments=@()){
  & (Join-Path $bin ($name+'.exe')) @Arguments
  if($LASTEXITCODE -ne 0){throw "$name failed: $LASTEXITCODE"}
 }
 $previous=@{};foreach($key in @('TEMP','TMP','USERPROFILE','LOCALAPPDATA')){$previous[$key]=[Environment]::GetEnvironmentVariable($key)}
 $env:TEMP=$root;$env:TMP=$root;$env:USERPROFILE=Join-Path $root 'profile';$env:LOCALAPPDATA=Join-Path $root 'local'
 New-Item -ItemType Directory -Force $env:USERPROFILE,$env:LOCALAPPDATA | Out-Null
 Run ReDotBrandTest @((Join-Path $root 'branding'))
 Run ReDotClipSoundTest
 Run ReDotReplayBufferStress
 Run ReDotLifecycleTest @((Join-Path $root 'lifecycle'))
 foreach($clip in Get-ChildItem (Join-Path $root 'lifecycle') -Filter *.mp4){Run ReDotReplaySyncVerify @($clip.FullName)}
 # Same lifecycle (repeat start/stop, cleanup gaps, notification faults) with the Matroska recorder.
 Run ReDotLifecycleTest @((Join-Path $root 'lifecycle-mkv'),'mkv')
 foreach($clip in Get-ChildItem (Join-Path $root 'lifecycle-mkv') -Filter *.mkv){Run ReDotReplaySyncVerify @($clip.FullName)}
 foreach($scale in @('1','1.25','1.5')){
  $oldScale=$env:QT_SCALE_FACTOR
  try{$env:QT_SCALE_FACTOR=$scale;Run ReDotScenesTest @((Join-Path $root ('scenes-'+$scale)))}finally{$env:QT_SCALE_FACTOR=$oldScale}
 }
 Run ReDotSceneIntegration @((Join-Path $root 'scene-integration'))
 foreach($clip in Get-ChildItem (Join-Path $root 'scene-integration') -Recurse -Filter *.mp4){Run ReDotReplaySyncVerify @($clip.FullName)}
 # MKV ordinary recording + simultaneous MP4 replay export, scene stems, track layouts and scene switching.
 Run ReDotSceneIntegration @((Join-Path $root 'scene-integration-mkv'),'mkv')
 foreach($clip in Get-ChildItem (Join-Path $root 'scene-integration-mkv') -Recurse | Where-Object {$_.Extension -in '.mp4','.mkv'}){Run ReDotReplaySyncVerify @($clip.FullName)}
 Run ReDotBorderPolicyTest
 # Compositor frame-pacing regression: desktop-wide application discovery used to run on the compositor thread every 2 s
 # (~60 ms with icons), which skipped ~66 of 3600 slots at 60 FPS. A 20 s generated 60 FPS run must now compose every slot.
 $pacingRoot=Join-Path $root 'compositor-pacing'
 Run ReDotScenePerformance @($pacingRoot,'capture','20','1920','1080','60')
 $pacing=@{};foreach($line in Get-Content (Join-Path $pacingRoot 'metrics.txt')){$k,$v=$line -split '=',2;$pacing[$k]=$v}
 if([int]$pacing['compositor_late_slots'] -gt 3 -or [int]$pacing['composed_frames'] -lt 1190){throw "Compositor pacing regression: late slots $($pacing['compositor_late_slots']), composed $($pacing['composed_frames']) of 1200"}
 Write-Output "PASS compositor pacing: late slots $($pacing['compositor_late_slots']), composed $($pacing['composed_frames'])/1200 at 60 FPS"
 # Controlled termination of an owned recording child, recovery, failure preservation and hostile partial files.
 Run ReDotMkvCrashTest @((Join-Path $root 'mkv-crash'))
 Run ReDotAudioContinuityTest @((Join-Path $root 'audio-continuity'))
 & py -3 tools/analyze_capture_checkpoints.py (Join-Path $root 'audio-continuity/four-checkpoints') --fixture
 if($LASTEXITCODE -ne 0){throw 'Lossless audio checkpoint comparison failed'}
 Run ReDotAudioMeterTest
 Run ReDotLoggingStress @((Join-Path $root 'logging'))
 Run ReDotLoggingStress @((Join-Path $root 'logging-blocked'),'--blocked-folder')
 Run ReDotQuietMediaFixture @((Join-Path $root 'media'))
 Run ReDotM6ServicesTest @((Join-Path $root 'media\both'),(Join-Path $root 'services'))
 foreach($clip in Get-ChildItem (Join-Path $root 'media') -Recurse -Filter *.mp4){Run ReDotReplaySyncVerify @($clip.FullName)}
 # Mix + Game + Microphone MKV recordings (and the MP4 replay exports made beside them).
 Run ReDotQuietMediaFixture @((Join-Path $root 'media-mkv'),'mkv')
 foreach($clip in Get-ChildItem (Join-Path $root 'media-mkv') -Recurse | Where-Object {$_.Extension -in '.mp4','.mkv'}){Run ReDotReplaySyncVerify @($clip.FullName)}
 if(-not $Executable){$Executable=Join-Path $bin 'ReDot.exe'}

  & py -3 tests/prepare_qt_profile.py (Join-Path $root 'ui-generated') (Join-Path $root 'media')
  if($LASTEXITCODE -ne 0){throw 'Qt profile preparation failed'}
  Run ReDotQtQuietLauncher @((Join-Path $bin 'ReDotQuietApp.exe'),(Join-Path $root 'ui-generated'))
  $stage4Expected=[int](Get-Content -LiteralPath (Join-Path $root 'ui-generated/stage4-exit-expected.txt'))
  $stage4Files=@(Get-ChildItem -LiteralPath (Join-Path $root 'ui-generated/output') | Where-Object {$_.Extension -in '.mp4','.mkv'})
  if($stage4Files.Count -ne $stage4Expected -or @($stage4Files|Where-Object {$_.Name -like '*.partial.*'}).Count){throw 'Stage 4 tray Exit did not finalize the recording and accepted replay export'}
  Write-Output 'PASS Stage 4 explicit tray Exit: recording and accepted export finalized, no partial MP4'
  Run ReDotQtQuietLauncher @((Join-Path $bin 'ReDotQuietApp.exe'),(Join-Path $root 'ui-generated'),'--restore')
  # The same Qt flow with MKV selected in the owned settings profile (recordFormat=0).
  # The Media window (listing, thumbnails, playback, seeking, track selection, sorting, filtering) runs against an
  # MKV + MP4 library. MP4 is chosen from the selector while the MKV recording is live (applies to the next one).
  & py -3 tests/prepare_qt_profile.py (Join-Path $root 'ui-generated-mkv') (Join-Path $root 'media-mkv') --format mkv
  if($LASTEXITCODE -ne 0){throw 'MKV Qt profile preparation failed'}
  $env:REDOT_QUIET_FORMAT_SWITCH='1'
  try{Run ReDotQtQuietLauncher @((Join-Path $bin 'ReDotQuietApp.exe'),(Join-Path $root 'ui-generated-mkv'))}finally{Remove-Item Env:REDOT_QUIET_FORMAT_SWITCH -ErrorAction SilentlyContinue}
  $mkvExpected=[int](Get-Content -LiteralPath (Join-Path $root 'ui-generated-mkv/stage4-exit-expected.txt'))
  $mkvFiles=@(Get-ChildItem -LiteralPath (Join-Path $root 'ui-generated-mkv/output') | Where-Object {$_.Extension -in '.mp4','.mkv'})
  if($mkvFiles.Count -ne $mkvExpected -or @($mkvFiles|Where-Object {$_.Name -like '*.partial.*'}).Count -or @($mkvFiles|Where-Object {$_.Extension -eq '.mkv' -and $_.Name -like 'ReDot_2*'}).Count -lt 1){throw 'MKV Qt flow did not finalize recording and replay export'}
  Write-Output 'PASS MKV Qt flow: recording finalized as .mkv (selector switched to MP4 mid-recording), replay export .mp4, no partial files'
  $env:REDOT_EXPECT_RECORD_FORMAT='1'
  try{Run ReDotQtQuietLauncher @((Join-Path $bin 'ReDotQuietApp.exe'),(Join-Path $root 'ui-generated-mkv'),'--restore')}finally{Remove-Item Env:REDOT_EXPECT_RECORD_FORMAT -ErrorAction SilentlyContinue}
  Write-Output 'PASS explicit MP4 selection persisted across a process launch after being chosen during an MKV recording'
  foreach($clip in Get-ChildItem (Join-Path $root 'ui-generated-mkv/output') | Where-Object {$_.Extension -in '.mp4','.mkv'}){Run ReDotReplaySyncVerify @($clip.FullName)}
  & py -3 tests/prepare_qt_profile.py (Join-Path $root 'ui-shipping') (Join-Path $root 'media')
  if($LASTEXITCODE -ne 0){throw 'Shipping smoke profile preparation failed'}
  Run ReDotQtQuietLauncher @($Executable,(Join-Path $root 'ui-shipping'),'--smoke')
  # Recording-format settings migration: the shipping app loads each owned profile, then saves on close.
  foreach($case in @(@('absent','0'),@('invalid','0'),@('mkv','0'),@('mp4','1'))){
   $profile=Join-Path $root ('ui-format-'+$case[0])
   & py -3 tests/prepare_qt_profile.py $profile (Join-Path $root 'no-fixtures') --format $case[0]
   if($LASTEXITCODE -ne 0){throw "Format profile $($case[0]) preparation failed"}
   Run ReDotQtQuietLauncher @($Executable,$profile,'--smoke')
   $saved=Get-Content -LiteralPath (Join-Path $profile 'local/PandaClip/settings-v1.txt') -Raw
   if($saved -notmatch ('"recordFormat" "'+$case[1]+'"')){throw "Recording format '$($case[0])' did not load/save as $($case[1]):`n$saved"}
   if($saved -notmatch '"width" "1280"' -or $saved -notmatch '"folder"'){throw "Recording format '$($case[0])' load lost other settings:`n$saved"}
   Write-Output "PASS settings migration: recordFormat $($case[0]) -> saved $($case[1]); other settings preserved"
  }
  # App-level startup recovery: a genuine hard-killed recording (orphan) placed in an owned profile's output folder is
  # repaired by the shipping app at launch; the original bytes are preserved and nothing else in the folder changes.
  Run ReDotMkvCrashTest @('--orphan-only',(Join-Path $root 'ui-recovery-orphan'))
  & py -3 tests/prepare_qt_profile.py (Join-Path $root 'ui-recovery') (Join-Path $root 'no-fixtures') --format mkv
  if($LASTEXITCODE -ne 0){throw 'Recovery profile preparation failed'}
  $orphan=@(Get-ChildItem (Join-Path $root 'ui-recovery-orphan') -Filter *.partial.mkv)
  if($orphan.Count -ne 1){throw 'Expected exactly one orphan'}
  $orphanBytes=$orphan[0].Length
  Move-Item -LiteralPath $orphan[0].FullName -Destination (Join-Path $root 'ui-recovery/output')
  Run ReDotQtQuietLauncher @($Executable,(Join-Path $root 'ui-recovery'),'--smoke')
  $recoveredClips=@(Get-ChildItem (Join-Path $root 'ui-recovery/output') -Filter *_recovered.mkv)
  $kept=@(Get-ChildItem (Join-Path $root 'ui-recovery/output') -Filter *.partial.mkv.recovered)
  $stillPartial=@(Get-ChildItem (Join-Path $root 'ui-recovery/output') -Filter *.partial.mkv)
  if($recoveredClips.Count -ne 1 -or $kept.Count -ne 1 -or $kept[0].Length -ne $orphanBytes -or $stillPartial.Count){throw "Startup recovery did not repair the orphan (recovered=$($recoveredClips.Count) kept=$($kept.Count) partial=$($stillPartial.Count))"}
  Run ReDotReplaySyncVerify @($recoveredClips[0].FullName)
  Write-Output 'PASS shipping app recovered an orphaned MKV at startup; original preserved; recovered clip decodes'
  & py -3 tests/prepare_qt_profile.py (Join-Path $root 'ui-shipping-controls') (Join-Path $root 'no-fixtures')
  if($LASTEXITCODE -ne 0){throw 'Shipping controls profile preparation failed'}
  Run ReDotQtQuietLauncher @($Executable,(Join-Path $root 'ui-shipping-controls'),'--shipping-ui')
  foreach($scale in @('1','1.25','1.5')){
   $stage4Profile=Join-Path $root ('ui-stage4-settings-'+$scale)
   & py -3 tests/prepare_qt_profile.py $stage4Profile (Join-Path $root 'media')
   if($LASTEXITCODE -ne 0){throw 'Stage 4 Settings profile preparation failed'}
   $oldScale=$env:QT_SCALE_FACTOR
   try{$env:QT_SCALE_FACTOR=$scale;Run ReDotQtQuietLauncher @($Executable,$stage4Profile,'--stage4-settings')}finally{$env:QT_SCALE_FACTOR=$oldScale}
  }
  & py -3 tests/prepare_qt_profile.py (Join-Path $root 'ui-write-faults') (Join-Path $root 'no-fixtures')
  if($LASTEXITCODE -ne 0){throw 'Frontend fault profile preparation failed'}
  Run ReDotQtQuietLauncher @((Join-Path $bin 'ReDotQuietApp.exe'),(Join-Path $root 'ui-write-faults'),'--fault-only')
  foreach($clip in Get-ChildItem (Join-Path $root 'ui-write-faults\output') -Filter *.mp4){Run ReDotReplaySyncVerify @($clip.FullName)}

 foreach($clip in Get-ChildItem (Join-Path $root 'ui-generated\output') -Filter *.mp4){Run ReDotReplaySyncVerify @($clip.FullName)}
 Run ReDotAacDelayProbe @((Join-Path $root 'aac-probe.mp4'))
 Run ReDotReplaySyncVerify @((Join-Path $root 'aac-probe.mp4'))
 foreach($clip in Get-ChildItem (Join-Path $root 'ui\output') -Filter *.mp4 -ErrorAction SilentlyContinue){Run ReDotReplaySyncVerify @($clip.FullName)}
 & py -3 tests\sync_compare_regression.py
 if($LASTEXITCODE -ne 0){throw 'Sync comparison regressions failed'}
 & py -3 tests\playback_checkpoint_regression.py
 if($LASTEXITCODE -ne 0){throw 'Playback PCM checkpoint regressions failed'}
 & py -3 tests/frame_time_summary_test.py
 if($LASTEXITCODE -ne 0){throw 'Frame-time summary arithmetic failed'}
 Write-Output "PASS quiet verification. Artifacts: $root"
 Write-Output 'SKIPPED: audible WASAPI loopback, real microphone speech/disconnection, speaker-to-flash synchronization, physical pointer dragging, physical Windows display-scale changes, active Geometry Dash. Scene model/editor Qt scaling was checked separately at 1.0/1.25/1.5. Interactive runners require -Interactive.'
} finally {if($previous){foreach($key in $previous.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key])}};Stop-Transcript | Out-Null}
