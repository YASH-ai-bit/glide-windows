param([switch]$TestsOnly)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build-native.ps1') -TestsOnly:$TestsOnly
if (-not $TestsOnly) {
    $glideExecutable = Join-Path $PSScriptRoot 'Glide.exe'
    $glideBuilt = Join-Path $PSScriptRoot 'artifacts\native\Glide.exe'
    $glideRunning = @(Get-Process -Name Glide -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $glideExecutable })
    if ($glideRunning.Count) {
        Write-Warning "The running app was left open. New build: $glideBuilt. Close Glide before copying this build to $glideExecutable."
    } else {
        Copy-Item -LiteralPath $glideBuilt -Destination $glideExecutable -Force
        Write-Output "Ready: $glideExecutable"
    }
}
