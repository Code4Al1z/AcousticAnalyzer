<#
.SYNOPSIS
  Zips the Windows Release build into dist/ and writes SHA256SUMS.txt.

.EXAMPLE
  cmake -B build -G "Visual Studio 17 2022"
  cmake --build build --config Release
  .\scripts\package_windows.ps1 -Version 0.9.0-beta
#>
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
$root      = Resolve-Path (Join-Path $PSScriptRoot "..")
$artefacts = Join-Path $root "$BuildDir\AcousticAnalyzer_artefacts\Release"
$dist      = Join-Path $root "dist"
$vst3      = Join-Path $artefacts "VST3\AcousticAnalyzer.vst3"
$standalone = Join-Path $artefacts "Standalone\AcousticAnalyzer.exe"

if (-not (Test-Path $vst3))       { throw "VST3 not found: $vst3 (did you build with --config Release?)" }
if (-not (Test-Path $standalone)) { throw "Standalone not found: $standalone" }

New-Item -ItemType Directory -Force -Path $dist | Out-Null

# The VST3 is a folder bundle on Windows, so the folder itself goes into the zip.
$stage = Join-Path $dist "stage-windows"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item $vst3 $stage -Recurse
Copy-Item $standalone $stage
Copy-Item (Join-Path $root "README.md") $stage
if (Test-Path (Join-Path $root "LICENSE")) { Copy-Item (Join-Path $root "LICENSE") $stage }

$zip = Join-Path $dist "AcousticAnalyzer-$Version-Windows.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
Remove-Item $stage -Recurse -Force

Write-Host "Created $zip"
& (Join-Path $PSScriptRoot "make_checksums.ps1") -Dir $dist
