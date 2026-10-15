param([ValidateSet('wasapi','waveout')][string]$Output='wasapi')
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$relative=if($Output -eq 'waveout'){'build/player-output/Release/PandaClip.exe'}else{'build/redot-stage3/Release/ReDot.exe'}
$exe=Join-Path $project $relative
if(-not(Test-Path -LiteralPath $exe)){throw 'Build redot-stage3 Release first.'}
if($Output -eq 'waveout'){Write-Output 'Archived, unsuccessful audio-output trial. Not the current production renderer.'}
$root=Join-Path $project ('build/playback-review/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+$Output)
$folder=Join-Path $root 'output'
$local=Join-Path $root 'local/PandaClip'
New-Item -ItemType Directory -Path $folder,$local | Out-Null
# Read/copy existing clips. Never edit their originals or personal preferences.
$videos=Join-Path ([Environment]::GetFolderPath('MyVideos')) 'PandaClip'
$clips=@('PandaClip_Clip_20261009_184420_972_000000.mp4','PandaClip_20261009_174215.mp4')
foreach($name in $clips){
 $path=Join-Path $videos $name
 if(Test-Path -LiteralPath $path){Copy-Item -LiteralPath $path -Destination $folder; if(Test-Path -LiteralPath ($path+'.tracks')){Copy-Item -LiteralPath ($path+'.tracks') -Destination $folder}}
}
$settings='PandaClip settings 1'+"`n"+'"folder" "'+$folder.Replace('\','/')+'"'+"`n"+'"gameEnabled" "0"'+"`n"+'"micEnabled" "0"'+"`n"+'"closeTray" "0"'+"`n"
[IO.File]::WriteAllText((Join-Path $local 'settings-v1.txt'),$settings,[Text.UTF8Encoding]::new($false))
$keys=@('LOCALAPPDATA','PANDACLIP_PLAYBACK_OUTPUT','PANDACLIP_PLAYBACK_TRACE','PANDACLIP_PLAYBACK_SILENT_SESSION')
$previous=@{}
try{
 foreach($key in $keys){$previous[$key]=[Environment]::GetEnvironmentVariable($key)}
 $env:LOCALAPPDATA=Join-Path $root 'local'
 $env:PANDACLIP_PLAYBACK_OUTPUT=$Output
 $env:PANDACLIP_PLAYBACK_TRACE=Join-Path $root 'sink'
 Remove-Item Env:PANDACLIP_PLAYBACK_SILENT_SESSION -ErrorAction SilentlyContinue
 # Interactive review is explicit: the user launches, clicks and plays clips.
 # No automatic playback, global input, personal session control or shortcuts.
 $process=Start-Process -FilePath $exe -WorkingDirectory $project -PassThru
 Write-Output "Review PID=$($process.Id) output=$Output artifacts=$root"
 Write-Output 'Click Media and play Microphone then Mix. Test continuous playback, seeks and pause/resume. Close this review app normally to finalize its trace.'
}finally{foreach($key in $keys){[Environment]::SetEnvironmentVariable($key,$previous[$key])}}
