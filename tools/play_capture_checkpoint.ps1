param([Parameter(Mandatory)][string]$Folder,[ValidateSet('RawDesktop','ProcessedDesktop','RawMicrophone','FinalMix')][string]$Track='RawDesktop')
$ErrorActionPreference='Stop'
$names=@{RawDesktop='01-Raw Desktop.wav';ProcessedDesktop='02-Processed Desktop.wav';RawMicrophone='03-Raw Microphone.wav';FinalMix='04-Final Mix.wav'}
$summary=Join-Path $Folder 'summary.txt'
if(-not(Test-Path -LiteralPath $summary)){throw 'Trace is not finalized. Stop the recording and close only the isolated review app normally.'}
if((Get-Content -LiteralPath $summary) -notcontains 'finalized=1'){Write-Warning 'Diagnostic capture is partial/failed. Read summary.txt; do not treat missing samples as source evidence.'}
$file=Join-Path $Folder $names[$Track]
if(-not(Test-Path -LiteralPath $file)){throw "Track not captured: $file. Enable both sources for all four checkpoints."}
# Playback is only initiated by this explicit user command.
Start-Process -FilePath $file
