param(
 [Parameter(Mandatory)][string]$PresentMon,
 [Parameter(Mandatory)][ValidateSet('off','idle','replay','record','both')][string]$Condition,
 [ValidateRange(1,20)][int]$Run=1,
 [ValidateRange(60,600)][int]$Seconds=180,
 [ValidateRange(0,120)][int]$DelaySeconds=10,
 [int]$ReDotProcessId=0
)
# User-invoked, read-only measurement. Does not launch/control ReDot or GD,
# move a cursor, take focus, inject a key, play a cue, or change ETW sessions.
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$presentMonPath=(Resolve-Path -LiteralPath $PresentMon).Path
$redot=$null
if($ReDotProcessId){
 $redot=Get-Process -Id $ReDotProcessId
 if($redot.ProcessName -ne 'ReDot'){throw 'ReDotProcessId must identify your chosen ReDot.exe process.'}
}
if($Condition -eq 'off' -and @(Get-Process ReDot -ErrorAction SilentlyContinue).Count){throw 'Close ReDot yourself for the off condition; this helper will not close it.'}
if($Condition -ne 'off' -and -not $redot){throw 'Supply the chosen ReDot PID for this condition.'}
if(-not @(Get-Process GeometryDash -ErrorAction SilentlyContinue).Count){throw 'Start Geometry Dash yourself first; this helper will not launch it.'}
$root=Join-Path $project ('build/gd-benchmark/'+$Condition+'-run'+$Run+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $root | Out-Null
$frames=Join-Path $root ($Condition+'-run'+$Run+'.csv')
$session='ReDotReview-'+[Guid]::NewGuid().ToString('N')
$method=@{condition=$Condition;run=$Run;seconds=$Seconds;delay_seconds=$DelaySeconds;redot_pid=$ReDotProcessId;redot_exe=if($redot){$redot.Path}else{''};redot_hash=if($redot){(Get-FileHash -LiteralPath $redot.Path).Hash}else{''};presentmon_exe=$presentMonPath;session=$session;logical_processors=[Environment]::ProcessorCount;notes='Manual fixed gameplay conditions required; PresentMon measures presentation intervals, not WGC or perception. No automatic game/app interaction.'}
$method | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'method.json')
$arguments='--process_name GeometryDash.exe --v1_metrics --output_file "'+$frames+'" --session_name '+$session+' --delay '+$DelaySeconds+' --timed '+$Seconds+' --terminate_after_timed --no_track_input --no_console_stats'
$monitor=Start-Process -FilePath $presentMonPath -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'presentmon-stdout.txt') -RedirectStandardError (Join-Path $root 'presentmon-stderr.txt')
$resources=[Collections.Generic.List[object]]::new();$gpu=[Collections.Generic.List[object]]::new();$errors=[Collections.Generic.List[string]]::new();$watch=[Diagnostics.Stopwatch]::StartNew();$deadline=$Seconds+$DelaySeconds+30
Write-Output "Measurement starts after $DelaySeconds seconds; play the chosen fixed section yourself. Output: $root"
while(-not $monitor.HasExited -and $watch.Elapsed.TotalSeconds -lt $deadline){
 if($redot){
  try{
   $redot.Refresh();if($redot.HasExited){$errors.Add('Chosen ReDot exited; no replacement PID was selected.');$redot=$null;continue}
   $resources.Add([pscustomobject]@{seconds=$watch.Elapsed.TotalSeconds;utc=[DateTime]::UtcNow.ToString('o');pid=$redot.Id;cpu_seconds=$redot.TotalProcessorTime.TotalSeconds;private_bytes=$redot.PrivateMemorySize64;working_set_bytes=$redot.WorkingSet64;handles=$redot.HandleCount;threads=$redot.Threads.Count})
   $values=(Get-Counter -Counter ('\GPU Engine(pid_'+$redot.Id+'_*)\Utilization Percentage'),('\GPU Process Memory(pid_'+$redot.Id+'_*)\Dedicated Usage'),('\GPU Process Memory(pid_'+$redot.Id+'_*)\Shared Usage') -SampleInterval 1 -MaxSamples 1 -ErrorAction SilentlyContinue).CounterSamples
   foreach($v in $values){if($v.Status -eq 0 -and $v.InstanceName.StartsWith('pid_'+$redot.Id+'_')){$gpu.Add([pscustomobject]@{utc=$v.Timestamp.ToString('o');instance=$v.InstanceName;counter=$v.Path.Substring($v.Path.LastIndexOf('\')+1);value=$v.CookedValue})}}
  }catch{$errors.Add($_.Exception.Message);Start-Sleep -Milliseconds 1000}
 }else{Start-Sleep -Milliseconds 1000}
 $monitor.Refresh()
}
$resources | Export-Csv (Join-Path $root 'redot-resources.csv') -NoTypeInformation
$gpu | Export-Csv (Join-Path $root 'redot-gpu.csv') -NoTypeInformation
$errors | Set-Content -LiteralPath (Join-Path $root 'counter-errors.txt')
if(-not $monitor.HasExited){
 # Only the child launched above may be stopped; never an existing ETW session
 # or either application. Its unique session belongs solely to this helper.
 $owned=Get-Process -Id $monitor.Id
 if($owned.Path -eq $presentMonPath){Stop-Process -Id $owned.Id}
 throw "Owned PresentMon exceeded its bound. Measurements are incomplete: $root"
}
$monitor.WaitForExit()
if($monitor.ExitCode -ne 0){throw "PresentMon failed ($($monitor.ExitCode)); inspect its stderr. No game/app was altered: $root"}
if(-not(Test-Path -LiteralPath $frames)){throw "No gameplay CSV; measurement not verified: $root"}
Write-Output "Collected: $frames. Run: py -3 tests/summarize_game_frame_times.py 'build/gd-benchmark/*/*-run*.csv'"
