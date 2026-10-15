param(
 [string]$BuildDirectory='build/redot-moved',
 [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$RunId='stage5',
 [ValidateRange(10,3600)][int]$Seconds=60,
 [ValidateRange(1,10)][int]$Rounds=3,
 [string[]]$Configs=@('idle','generator-60','capture-60','replay-60','record-mkv-60','record-mp4-60','record-mkv-60-mix','both-mkv-60','generator-120','record-mkv-120','record-mp4-120')
)
# Repeatable Stage 5 benchmark matrix. Runs one owned ReDotScenePerformance process at a time (round-robin across
# configurations so slow drift of the machine hits every configuration equally), samples ambient CPU/GPU just before
# each run, and decodes/verifies every ordinary recording with ReDotReplaySyncVerify. Nothing outside build/ is touched.
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$table=@{
 'idle'             =@{mode='idle';fps=60;container='mkv';tracks='all';audio=$false}
 'generator-60'     =@{mode='generator';fps=60;container='mkv';tracks='all';audio=$true}
 'capture-60'       =@{mode='capture';fps=60;container='mkv';tracks='all';audio=$true}
 'replay-60'        =@{mode='replay';fps=60;container='mkv';tracks='all';audio=$true}
 'record-mkv-60'    =@{mode='record';fps=60;container='mkv';tracks='all';audio=$true}
 'record-mp4-60'    =@{mode='record';fps=60;container='mp4';tracks='all';audio=$true}
 'record-mkv-60-mix'=@{mode='record';fps=60;container='mkv';tracks='mix';audio=$true}
 'record-mp4-60-mix'=@{mode='record';fps=60;container='mp4';tracks='mix';audio=$true}
 'record-mkv-60-video'=@{mode='record';fps=60;container='mkv';tracks='all';audio=$false}
 'both-mkv-60'      =@{mode='both';fps=60;container='mkv';tracks='all';audio=$true}
 'both-mp4-60'      =@{mode='both';fps=60;container='mp4';tracks='all';audio=$true}
 'generator-120'    =@{mode='generator';fps=120;container='mkv';tracks='all';audio=$true}
 'record-mkv-120'   =@{mode='record';fps=120;container='mkv';tracks='all';audio=$true}
 'record-mp4-120'   =@{mode='record';fps=120;container='mp4';tracks='all';audio=$true}
 'replay-120'       =@{mode='replay';fps=120;container='mkv';tracks='all';audio=$true}
}
foreach($name in $Configs){if(-not $table.ContainsKey($name)){throw "Unknown configuration $name"}}
$out=Join-Path $project ('build/stage5/'+$RunId+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $out | Out-Null
$verify=Join-Path $project ($BuildDirectory+'/Release/ReDotReplaySyncVerify.exe')
function Ambient{
 try{
  $cpu=(Get-Counter '\Processor(_Total)\% Processor Time' -SampleInterval 1 -MaxSamples 3).CounterSamples|Measure-Object CookedValue -Average
  $gpu=(Get-Counter '\GPU Engine(*)\Utilization Percentage' -SampleInterval 1 -MaxSamples 1).CounterSamples
  $d3=($gpu|Where-Object{$_.InstanceName -like '*engtype_3D'}|Measure-Object CookedValue -Sum).Sum
  $enc=($gpu|Where-Object{$_.InstanceName -like '*engtype_VideoEncode'}|Measure-Object CookedValue -Sum).Sum
  $os=Get-CimInstance Win32_OperatingSystem
  [pscustomobject]@{cpu_percent=[math]::Round($cpu.Average,2);gpu_3d_percent=[math]::Round([double]$d3,2);gpu_videoencode_percent=[math]::Round([double]$enc,2);free_ram_gb=[math]::Round($os.FreePhysicalMemory/1MB,2)}
 }catch{[pscustomobject]@{error=$_.Exception.Message}}
}
$manifest=[Collections.Generic.List[object]]::new()
for($round=1;$round -le $Rounds;$round++){
 foreach($name in $Configs){
  $c=$table[$name];$label=($RunId+'-'+$name+'-r'+$round)
  $ambient=Ambient
  $splat=@{BuildDirectory=$BuildDirectory;Label=$label;Seconds=$Seconds;Modes=@($c.mode);Fps=$c.fps;Container=$c.container;Tracks=$c.tracks;HistorySeconds=60}
  if($c.audio){$splat.AudioFixture=$true}
  & (Join-Path $PSScriptRoot 'measure_final_performance.ps1') @splat | Out-Null
  $folder=(Get-ChildItem (Join-Path $project 'build/final-performance') -Directory|Where-Object{$_.Name -like ($label+'-*')}|Sort-Object Name|Select-Object -Last 1).FullName
  $run=Join-Path $folder $c.mode
  $sync=@()
  foreach($clip in Get-ChildItem $run -File|Where-Object{$_.Name -like 'ReDot_2*' -and $_.Extension -in '.mkv','.mp4'}){
   $text=& $verify $clip.FullName 2>&1|Out-String
   $sync+=[pscustomobject]@{file=$clip.Name;exit=$LASTEXITCODE;offset=([regex]::Match($text,'measured offset range=([^\r\n]+)').Groups[1].Value);decode=([regex]::Match($text,'decodeResult=(\w+)').Groups[1].Value);drift=([regex]::Match($text,'Endpoint offset change=([^ ]+)').Groups[1].Value)}
  }
  $manifest.Add([pscustomobject]@{name=$name;round=$round;seconds=$Seconds;folder=$run;ambient=$ambient;sync=$sync;exe_sha256=(Get-FileHash (Join-Path $project ($BuildDirectory+'/Release/ReDotScenePerformance.exe'))).Hash})
  $manifest|ConvertTo-Json -Depth 6|Set-Content (Join-Path $out 'manifest.json') -Encoding UTF8
  Write-Output ("done $label ambient cpu="+$ambient.cpu_percent+"% gpu3d="+$ambient.gpu_3d_percent+"%")
 }
}
Write-Output "Manifest: $out\manifest.json"
