# Manual review only. No automated input, sounds, capture or recording.
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $project 'build/redot-stage4-accessibility/Release/ReDot.exe'
if (!(Test-Path -LiteralPath $exe)) { throw "Build not found: $exe" }
$review = Join-Path $project ('build/appearance-checks/manual-' + [Guid]::NewGuid().ToString('N'))
$local = Join-Path $review 'local'
$settings = Join-Path $local 'PandaClip'
$output = Join-Path $review 'output'
New-Item -ItemType Directory -Path $settings,$output | Out-Null
$contents = @"
PandaClip settings 1
"folder" "$($output.Replace('\','/'))"
"gameEnabled" "0"
"micEnabled" "0"
"gameMode" "0"
"closeTray" "0"
"transparency" "1"
"animations" "1"
"@
[IO.File]::WriteAllText((Join-Path $settings 'settings-v1.txt'), $contents, [Text.UTF8Encoding]::new($false))
$savedLocal = $env:LOCALAPPDATA
$savedQuiet = $env:PANDACLIP_TEST_QUIET_UI
$savedPassive = $env:PANDACLIP_TEST_PASSIVE_WINDOW
try {
    $env:LOCALAPPDATA = $local
    $env:PANDACLIP_TEST_QUIET_UI = '1' # initial show without activation; no tray/hotkeys
    $env:PANDACLIP_TEST_PASSIVE_WINDOW = $null # allow the user's own clicks
    Write-Host "Isolated profile: $review. Click ReDot on monitor 1, then your browser on monitor 2. Review this newly opened build, not an older running instance. Compare focus/unfocus on two monitors, then repeat with Recording gear and Settings; the physical result is still pending."
    & $exe
} finally {
    $env:LOCALAPPDATA = $savedLocal
    $env:PANDACLIP_TEST_QUIET_UI = $savedQuiet
    $env:PANDACLIP_TEST_PASSIVE_WINDOW = $savedPassive
}
