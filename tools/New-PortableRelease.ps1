# Builds the signed portable release files that ReDotUpdater.exe consumes:
#   dist\ReDot-v<ver>-Windows-x64.zip      (single top-level folder ReDot-v<ver>\ ... , contains redot-version.json)
#   dist\ReDot-v<ver>-Windows-x64.zip.sig  (64-byte ECDSA P-256 signature over the ZIP's SHA-256)
# Upload BOTH files to the GitHub release tagged v<ver>. Nothing is published by this script.
#
#   .\tools\New-PortableRelease.ps1 -ReleaseDirectory build\<fresh-dir>\Release
param(
    [Parameter(Mandatory)][string]$ReleaseDirectory,    # folder containing the deployed ReDot.exe (+ Qt/FFmpeg DLLs, README.txt, THIRD-PARTY-NOTICES.txt)
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\dist'),
    [string]$PrivateKey = (Join-Path $env:USERPROFILE '.redot-signing\redot-release-private.blob'),
    [string]$SignTool = (Join-Path $PSScriptRoot '..\build\redot-updater\Release\ReDotSignTool.exe')
)
$ErrorActionPreference = 'Stop'
$ReleaseDirectory = (Resolve-Path $ReleaseDirectory).Path
if (-not (Test-Path "$ReleaseDirectory\ReDot.exe")) { throw "ReDot.exe not found in $ReleaseDirectory" }
if (-not (Test-Path "$ReleaseDirectory\redot-version.json")) { throw "redot-version.json missing: build ReDot from this source tree (CMake generates it)." }
$manifest = Get-Content "$ReleaseDirectory\redot-version.json" -Raw | ConvertFrom-Json
if ($manifest.product -ne 'ReDot' -or $manifest.version -notmatch '^\d+\.\d+\.\d+$') { throw 'redot-version.json is not a stable ReDot manifest.' }
$exeVersion = (Get-Item "$ReleaseDirectory\ReDot.exe").VersionInfo.ProductVersion
if ($exeVersion -ne $manifest.version) { throw "ReDot.exe reports $exeVersion but the manifest says $($manifest.version). Rebuild so they agree." }
foreach ($required in 'Qt6Core.dll', 'THIRD-PARTY-NOTICES.txt') {
    if (-not (Test-Path "$ReleaseDirectory\$required")) { Write-Warning "$required is missing from the release folder; the public v1.0.0 package included it." }
}
foreach ($tool in $SignTool, $PrivateKey) { if (-not (Test-Path $tool)) { throw "Missing $tool" } }

$version = $manifest.version
$name = "ReDot-v$version-Windows-x64"
$work = Join-Path ([IO.Path]::GetTempPath()) "redot-release-$([guid]::NewGuid().ToString('N'))"
$root = Join-Path $work "ReDot-v$version"
New-Item -ItemType Directory $root -Force | Out-Null
robocopy $ReleaseDirectory $root /E /XF *.pdb *.ilk *.exp *.lib /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }

New-Item -ItemType Directory $OutputDirectory -Force | Out-Null
$zip = Join-Path (Resolve-Path $OutputDirectory).Path "$name.zip"
if (Test-Path $zip) { throw "$zip already exists; refusing to overwrite an earlier release file." }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($root, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
Remove-Item $work -Recurse -Force

& $SignTool sign $PrivateKey $zip "$zip.sig"
if ($LASTEXITCODE -ne 0) { throw 'Signing failed.' }
"`nZIP: $zip ($([math]::Round((Get-Item $zip).Length/1MB,1)) MB)"
"SIG: $zip.sig"
"Upload both files to the GitHub release tagged v$version."
