param(
 [string]$BuildDirectory='build/redot-final-baseline',
 [string]$Label='baseline',
 [ValidateRange(10,36000)][int]$Seconds=30,
 [string[]]$Modes=@('idle','capture','replay','record','both','layers','switch','saves'),
 [int]$Width=1920,[int]$Height=1080,[int]$Fps=60,[switch]$AudioFixture,
 [ValidateRange(5,600)][int]$HistorySeconds=15,
 [ValidateRange(0,36000)][int]$ExportUntilSeconds=7200,
 [ValidateSet('mkv','mp4')][string]$Container='mkv',
 [ValidateSet('all','mix')][string]$Tracks='all'
)
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$exe=[IO.Path]::GetFullPath((Join-Path $project "$BuildDirectory/Release/ReDotScenePerformance.exe"))
$buildRoot=[IO.Path]::GetFullPath((Join-Path $project 'build'))+[IO.Path]::DirectorySeparatorChar
if(-not $exe.StartsWith($buildRoot) -or $Label -notmatch '^[a-zA-Z0-9_-]+$'){throw 'Use a project build and simple evidence label.'}
$root=Join-Path $project ('build/final-performance/'+$Label+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $root | Out-Null
$hardware=@{cpu=Get-CimInstance Win32_Processor | Select-Object Name,NumberOfLogicalProcessors;gpu=Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion;system=Get-CimInstance Win32_ComputerSystem | Select-Object TotalPhysicalMemory;os=Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version;exe=$exe;sha256=(Get-FileHash -LiteralPath $exe).Hash;seconds=$Seconds;width=$Width;height=$Height;fps=$Fps;logical_processors=[Environment]::ProcessorCount;history_seconds=$HistorySeconds;export_until_seconds=$ExportUntilSeconds;audio_fixture=$AudioFixture.IsPresent;coarse_compositor_test_override=$env:PANDACLIP_TEST_COARSE_COMPOSITOR;workload='Owned moving GPU fixture; no WGC, microphone, gameplay or UI presentation';personal_apps_preserved=$true}
$hardware | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'method.json')
foreach($mode in $Modes){
 if($mode -notin @('idle','capture','replay','record','both','layers','switch','saves','replay-saves','direct','soak','replay-soak','generator')){throw "Unknown mode $mode"}
 $folder=Join-Path $root $mode;New-Item -ItemType Directory -Path $folder | Out-Null
 $arguments='"'+$folder+'" '+$mode+' '+$Seconds+' '+$Width+' '+$Height+' '+$Fps+' '+[int]$AudioFixture.IsPresent+' '+$HistorySeconds+' '+$ExportUntilSeconds+' '+$Container+' '+$Tracks
 $temporary=Join-Path $folder 'temp';New-Item -ItemType Directory -Path $temporary | Out-Null
 $oldTemp=$env:TEMP;$oldTmp=$env:TMP
 try{
  $env:TEMP=$temporary;$env:TMP=$temporary
  $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $folder 'stdout.txt') -RedirectStandardError (Join-Path $folder 'stderr.txt')
  $null=$process.Handle # cache the handle so ExitCode is readable after exit (Windows PowerShell 5.1)
 }finally{$env:TEMP=$oldTemp;$env:TMP=$oldTmp}
 $limit=[DateTime]::UtcNow.AddSeconds($Seconds+90);$samples=[Collections.Generic.List[object]]::new();$counterError=''
 while(-not $process.HasExited -and [DateTime]::UtcNow -lt $limit){
  try{
   $values=(Get-Counter -Counter ('\GPU Engine(pid_'+$process.Id+'_*)\Utilization Percentage'),('\GPU Process Memory(pid_'+$process.Id+'_*)\Dedicated Usage'),('\GPU Process Memory(pid_'+$process.Id+'_*)\Shared Usage') -SampleInterval 1 -MaxSamples 1 -ErrorAction SilentlyContinue).CounterSamples
   $process.Refresh()
   foreach($value in $values){if($value.Status -eq 0 -and $value.InstanceName.StartsWith('pid_'+$process.Id+'_')){$samples.Add([pscustomobject]@{time=$value.Timestamp.ToString('o');instance=$value.InstanceName;counter=$value.Path.Substring($value.Path.LastIndexOf('\')+1);value=$value.CookedValue;threads=$process.Threads.Count})}}
  }catch{$counterError=$_.Exception.Message;Start-Sleep -Milliseconds 1000}
  $process.Refresh()
 }
 if(-not $process.HasExited){$owned=Get-Process -Id $process.Id -ErrorAction Stop;if($owned.Path -eq $exe){Stop-Process -Id $owned.Id};throw 'Owned benchmark exceeded bounded timeout; artifacts preserved.'}
 $samples | Export-Csv (Join-Path $folder 'gpu-counters.csv') -NoTypeInformation
 [IO.File]::WriteAllText((Join-Path $folder 'counter-status.txt'),$counterError)
 $process.WaitForExit();if($process.ExitCode -ne 0){throw "Benchmark $mode failed ($($process.ExitCode)); artifacts preserved: $folder"}
 Write-Output "PASS $mode; $folder"
}
Write-Output "Evidence: $root. Quiet generated GPU pipeline only; fixture idle is not shipping UI idle, capture is not WGC."

# Start-Process does not set the caller native-exit variable. Reflect the checked success.
$global:LASTEXITCODE=0
