param([ValidateRange(1,9)][int]$FromCase=1, [switch]$Interactive)
# Desktop/audio integration is opt-in. Never run during ordinary build/quiet verification.
if (-not $Interactive) {
  Write-Output 'SKIPPED interactive check: may activate owned windows, use desktop input, or play audio. Use tests\run_quiet_tests.ps1 by default; add -Interactive only when you are ready.'
  return
}

$ErrorActionPreference = 'Stop'
$runner = Join-Path $PSScriptRoot 'run_reliability_test.ps1'
$cases = @(
  @{ Seconds=25; Mode='Both'; HidePreview=$true; GameSync=100 },
  @{ Seconds=25; Mode='Both'; HidePreview=$true; GameSync=-100; ExportDelayMilliseconds=3000 },
  @{ Seconds=75; Mode='Both'; HidePreview=$true; EnableMic=$true; AudioFaultSeconds=8; ExerciseInterruptions=$true; ExerciseReopen=$true },
  @{ Seconds=28; Mode='Both'; HidePreview=$true; ExerciseGpu=$true },
  @{ Seconds=48; Mode='Replay'; HidePreview=$true; ExhaustRecovery=$true },
  @{ Seconds=20; Mode='Both'; HidePreview=$true; RecordFaultFrame=600 },
  @{ Seconds=15; Mode='Replay'; HidePreview=$true; ExportFaultPacket=10 },
  @{ Seconds=15; Mode='Replay'; HidePreview=$true; ExportFinalizeFault=$true },
  @{ Seconds=15; Mode='Replay'; HidePreview=$true; FolderFault=$true }
)
foreach ($case in $cases[($FromCase-1)..($cases.Count-1)]) {
  Write-Output ('Controlled check: ' + ($case | ConvertTo-Json -Compress))
  $arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$runner,'-Interactive')
  foreach ($key in $case.Keys) {
    $arguments += "-$key"
    if ($case[$key] -isnot [bool]) { $arguments += [string]$case[$key] }
  }
  & powershell.exe @arguments
  if ($LASTEXITCODE -ne 0) { throw 'Controlled reliability check failed; inspect that run before continuing.' }
}
