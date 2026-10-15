param([string]$BuildDirectory='build\redot-stage3', [string]$Publisher='CN=PandaClip Development')
$ErrorActionPreference='Stop'
$project=(Split-Path -Parent $PSScriptRoot)
$build=[IO.Path]::GetFullPath((Join-Path $project $BuildDirectory))
if(!$build.StartsWith($project+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Build directory must be inside the ReDot project.'}
$exe=Join-Path $build 'Release\ReDot.exe'
if(!(Test-Path -LiteralPath $exe)){throw "Missing Release build: $exe"}
$sdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$makeappx=Get-ChildItem -LiteralPath $sdk -Directory | Where-Object {$_.Name -match '^10\.0\.'} | Sort-Object Name -Descending | ForEach-Object {Join-Path $_.FullName 'x64\makeappx.exe'} | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
if(!$makeappx){throw 'Missing MakeAppx.exe: install the Windows 10/11 SDK through Visual Studio Installer.'}
$output=Join-Path $project ('build\packages\package-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
$stage=Join-Path $output 'stage';$assets=Join-Path $stage 'Assets'
New-Item -ItemType Directory -Path $assets | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $stage 'ReDot.exe')
$runtime=Split-Path -Parent $exe
foreach($file in Get-ChildItem -LiteralPath $runtime -File -Filter *.dll){Copy-Item -LiteralPath $file.FullName -Destination $stage}
foreach($folder in @('platforms','qml','imageformats','networkinformation','styles','tls','generic','translations','licenses','docs','multimedia')){
 $source=Join-Path $runtime $folder
 if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination (Join-Path $stage $folder) -Recurse}
}
foreach($name in @('README.md','QT-NOTICE.txt','qt.conf')){
 $source=Join-Path $runtime $name
 if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination (Join-Path $stage $name)}
}
$manifest=[xml](Get-Content -LiteralPath (Join-Path $PSScriptRoot 'AppxManifest.xml') -Raw)
$manifest.Package.Identity.Publisher=$Publisher
$manifest.Save((Join-Path $stage 'AppxManifest.xml'))
# Copy the same neutral logo rendered from the maintained production SVG.
foreach($asset in @(@('StoreLogo.png',50),@('Square44x44Logo.png',44),@('Square150x150Logo.png',150))){
 $source=Join-Path $runtime ('branding\idle-'+$asset[1]+'.png')
 if(!(Test-Path -LiteralPath $source)){throw "Missing generated ReDot brand asset: $source"}
 Copy-Item -LiteralPath $source -Destination (Join-Path $assets $asset[0])
}
$package=Join-Path $output 'ReDot.msix'
& $makeappx pack /d $stage /p $package
if($LASTEXITCODE -ne 0){throw 'MakeAppx package validation/build failed.'}
Write-Output "Built and schema-validated UNSIGNED MSIX: $package"
Write-Output "Publisher must match your signing certificate: $Publisher"
Write-Output 'Sign/trust/install it yourself as described in README. No certificate, registration, or Windows permission was changed by this script.'
