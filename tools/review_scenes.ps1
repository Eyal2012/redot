param([switch]$Demo)
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$exe=Join-Path $project 'build/redot-stage3/Release/ReDot.exe'
if(-not(Test-Path -LiteralPath $exe)){throw 'Build redot-stage3 Release first.'}
$root=Join-Path $project ('build/scenes-review/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,6))
$local=Join-Path $root 'local/PandaClip';$output=Join-Path $root 'output'
New-Item -ItemType Directory -Path $local,$output | Out-Null
$values=[ordered]@{folder=$output.Replace('\','/');gameMode='0';gameEnabled='0';micEnabled='0';closeTray='0'}
for($i=0;$i -lt 6;$i++){$values['binding'+$i]='0'}
$original=Join-Path $env:LOCALAPPDATA 'PandaClip/settings-v1.txt'
if(Test-Path -LiteralPath $original){foreach($line in Get-Content -LiteralPath $original){if($line -match '^"(transparency|animations)" "(.*)"$'){$values[$Matches[1]]=$Matches[2]}}}
$settings="PandaClip settings 1`n"+(($values.GetEnumerator()|ForEach-Object{'"'+$_.Key+'" "'+$_.Value+'"'}) -join "`n")+"`n"
[IO.File]::WriteAllText((Join-Path $local 'settings-v1.txt'),$settings,[Text.UTF8Encoding]::new($false))
if($Demo){& py -3 (Join-Path $project 'tests/prepare_scene_profile.py') $root;if($LASTEXITCODE -ne 0){throw 'Scene fixture preparation failed.'}}
$previous=$env:LOCALAPPDATA
try{
 $env:LOCALAPPDATA=Join-Path $root 'local'
 $process=Start-Process -FilePath $exe -WorkingDirectory $project -PassThru
 Write-Output "Scenes review PID=$($process.Id); separate scene/preferences/output/logs: $root"
 Write-Output 'Click Scenes. Add Game/Window from the running-app picker, or Text/Image. Drag to move; handles resize/rotate. Shift allows free resizing or snaps rotation; Alt+handle crops; Ctrl bypasses alignment snapping. Right-click for alignment, Crop mode, or Advanced Transform.'
 Write-Output 'Capture now selects a scene; recording and replay use its composed output. Configure audio in Scenes before recording. No recording, replay, microphone check, global shortcut or sound starts automatically.'
 Write-Output "To reopen this profile: `$env:LOCALAPPDATA='$root\local'; & '$exe' (restore your environment afterward)."
}finally{$env:LOCALAPPDATA=$previous}
