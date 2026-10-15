param([ValidateSet('SelectedApplication','WholeSystem')][string]$GameMode='SelectedApplication')
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$exe=Join-Path $project 'build/redot-stage3/Release/ReDot.exe'
if(-not(Test-Path -LiteralPath $exe)){throw 'Build redot-stage3 Release first.'}
$root=Join-Path $project ('build/audio-capture-review/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,6))
$local=Join-Path $root 'local/PandaClip';$output=Join-Path $root 'output'
New-Item -ItemType Directory -Path $local,$output | Out-Null
# Copy functional device choices only; never write to personal preferences.
$values=[ordered]@{folder=$output.Replace('\','/');gameMode=($(if($GameMode -eq 'WholeSystem'){'2'}else{'1'}));gameEnabled='1';micEnabled='1';gameMute='0';micMute='0';gameGain='100';micGain='100';mixTrack='1';gameTrack='1';micTrack='1';closeTray='0';binding0='0'}
$original=Join-Path $env:LOCALAPPDATA 'PandaClip/settings-v1.txt'
if(Test-Path -LiteralPath $original){
 foreach($line in Get-Content -LiteralPath $original){if($line -match '^"(micId|outputId|gameSync|micSync|transparency|animations)" "(.*)"$'){$values[$Matches[1]]=$Matches[2]}}
}
$settings="PandaClip settings 1`n"+(($values.GetEnumerator()|ForEach-Object{'"'+$_.Key+'" "'+$_.Value+'"'}) -join "`n")+"`n"
[IO.File]::WriteAllText((Join-Path $local 'settings-v1.txt'),$settings,[Text.UTF8Encoding]::new($false))
$keys=@('LOCALAPPDATA','PANDACLIP_AUDIO_CAPTURE_TRACE');$previous=@{}
try{
 foreach($key in $keys){$previous[$key]=[Environment]::GetEnvironmentVariable($key)}
 $env:LOCALAPPDATA=Join-Path $root 'local';$env:PANDACLIP_AUDIO_CAPTURE_TRACE=Join-Path $root 'checkpoints'
 $process=Start-Process -FilePath $exe -WorkingDirectory $project -PassThru
 Write-Output "Audio review PID=$($process.Id); artifacts=$root"
 Write-Output "Audio mode=$GameMode; mic enabled. Confirm the actual device name, gains and mute states before Start Recording."
 Write-Output 'In Scenes, add Game/Window Capture for Geometry Dash and the requested Application Audio (or explicit Desktop Audio) plus Microphone; confirm endpoints. Select that scene in Capture. Record 30-60 seconds of its music/sound effects and spoken microphone, then Stop. No automated playback or recording is started.'
 Write-Output 'Wait for Saved and close only this review window normally. Check checkpoints/session-1/summary.txt: finalized=1, dropped_events=0. Four WAVs are named Raw Desktop, Processed Desktop, Raw Microphone, Final Mix.'
 Write-Output "Play separately: & '$PSScriptRoot\play_capture_checkpoint.ps1' -Folder '$root\checkpoints\session-1' -Track RawDesktop"
 Write-Output 'The raw native tracks retain device timing; packet CSV records exact QPC. The trace covers at most 120 seconds, four sessions. Ordinary recording/replay continues beyond the trace cap.'
}finally{foreach($key in $keys){[Environment]::SetEnvironmentVariable($key,$previous[$key])}}
