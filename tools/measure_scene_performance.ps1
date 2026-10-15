param([string]$BuildDirectory='build/scenes-stage2')
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$exe=[IO.Path]::GetFullPath((Join-Path $project "$BuildDirectory/Release/ReDotScenePerformance.exe"))
if(-not $exe.StartsWith([IO.Path]::GetFullPath((Join-Path $project 'build'))+[IO.Path]::DirectorySeparatorChar)){throw 'Benchmark must use a project build executable.'}
$root=Join-Path $project ('build/scenes-stage2/performance-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $root | Out-Null
foreach($mode in @('direct','scene','layers')){
 $folder=Join-Path $root $mode;New-Item -ItemType Directory -Path $folder | Out-Null
 $arguments='"'+$folder+'" '+$mode
 $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $folder 'stdout.txt') -RedirectStandardError (Join-Path $folder 'stderr.txt')
 $limit=[DateTime]::UtcNow.AddSeconds(50);$samples=[Collections.Generic.List[object]]::new();$counterError=''
 while(-not $process.HasExited -and [DateTime]::UtcNow -lt $limit){
  try{
   $values=(Get-Counter -Counter ('\GPU Engine(pid_'+$process.Id+'_*)\Utilization Percentage'),('\GPU Process Memory(pid_'+$process.Id+'_*)\Dedicated Usage') -SampleInterval 1 -MaxSamples 1 -ErrorAction SilentlyContinue).CounterSamples
   foreach($value in $values){if($value.Status -eq 0 -and $value.InstanceName.StartsWith('pid_'+$process.Id+'_')){$samples.Add([pscustomobject]@{time=$value.Timestamp.ToString('o');instance=$value.InstanceName;counter=$value.Path.Substring($value.Path.LastIndexOf('\')+1);value=$value.CookedValue})}}
  }catch{$counterError=$_.Exception.Message;Start-Sleep -Milliseconds 1000}
  $process.Refresh()
 }
 if(-not $process.HasExited){$owned=Get-Process -Id $process.Id -ErrorAction Stop;if($owned.Path -eq $exe){Stop-Process -Id $owned.Id};throw 'Owned benchmark exceeded 50 seconds; artifacts preserved.'}
 $samples | Export-Csv (Join-Path $folder 'gpu-counters.csv') -NoTypeInformation
 [IO.File]::WriteAllText((Join-Path $folder 'counter-status.txt'),$counterError)
 $process.WaitForExit();if($process.ExitCode -ne 0){throw "Benchmark $mode failed ($($process.ExitCode)); artifacts preserved: $folder"}
 Write-Output "PASS $mode; $folder"
}
Write-Output "Performance evidence: $root. Generated GPU source, no gameplay/WGC/audio/UI; other applications preserved."
