param([int]$Seconds = 35, [switch]$Interactive)
# Desktop/audio integration is opt-in. Never run during ordinary build/quiet verification.
if (-not $Interactive) {
  Write-Output 'SKIPPED interactive check: may activate owned windows, use desktop input, or play audio. Use tests\run_quiet_tests.ps1 by default; add -Interactive only when you are ready.'
  return
}

$ErrorActionPreference = 'Stop'
$runner = Join-Path $PSScriptRoot 'run_reliability_test.ps1'
foreach ($mode in @('Replay','Recording','Both')) {
  foreach ($hidden in @($false,$true)) {
    Write-Output "Release matrix: mode=$mode, hidden preview=$hidden"
    $arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$runner,'-Interactive','-Seconds',"$Seconds",'-Mode',$mode)
    if ($hidden) { $arguments += '-HidePreview' }
    & powershell.exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "Release matrix cell failed: $mode / preview hidden $hidden" }
  }
}
