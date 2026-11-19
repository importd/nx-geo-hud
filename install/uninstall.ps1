param(
    [string]$UserDir = $env:UGII_USER_DIR
)

$ErrorActionPreference = 'Stop'

if (-not $UserDir) { throw 'UGII_USER_DIR 未设置；请用 -UserDir 指定' }

$startup = Join-Path $UserDir 'startup'
foreach ($name in @('NxGeo.NxPlugin.dll', 'nxgeo-overlay.exe')) {
    $path = Join-Path $startup $name
    if (Test-Path $path) {
        Remove-Item $path -Force
        Write-Host "removed $path"
    }
}
