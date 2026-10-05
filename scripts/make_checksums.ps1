<#
.SYNOPSIS
  Writes SHA256SUMS.txt for every .zip in a folder (default: dist/).
  Format matches `sha256sum`, so users can verify with either tool.
#>
param([string]$Dir = (Join-Path $PSScriptRoot "..\dist"))

$ErrorActionPreference = "Stop"
$Dir = Resolve-Path $Dir
$lines = Get-ChildItem -Path $Dir -Filter *.zip | Sort-Object Name | ForEach-Object {
    $hash = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLower()
    "$hash  $($_.Name)"
}
if (-not $lines) { throw "No .zip files in $Dir" }

# Plain ASCII/LF so sha256sum -c accepts it
[System.IO.File]::WriteAllText((Join-Path $Dir "SHA256SUMS.txt"), (($lines -join "`n") + "`n"))
Write-Host "Wrote $(Join-Path $Dir 'SHA256SUMS.txt')"
$lines | ForEach-Object { Write-Host $_ }
