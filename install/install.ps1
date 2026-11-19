param(
    [string]$UserDir = $env:UGII_USER_DIR
)

$ErrorActionPreference = 'Stop'

if (-not $UserDir) {
    throw 'UGII_USER_DIR 未设置；请用 -UserDir 指定，例如 -UserDir "$env:USERPROFILE\Documents\nxgeo"'
}

$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root 'src\NxGeo.NxPlugin\x64\Debug\NxGeo.NxPlugin.dll'
$exe = Join-Path $root 'src\NxGeo.Overlay\x64\Debug\nxgeo-overlay.exe'

foreach ($f in @($dll, $exe)) {
    if (-not (Test-Path $f)) { throw "先跑 build.cmd，缺少产物：$f" }
}

$startup = Join-Path $UserDir 'startup'
New-Item -ItemType Directory -Force $startup | Out-Null

Copy-Item $dll $startup -Force
Copy-Item $exe $startup -Force

Write-Host "installed -> $startup"
Write-Host "  NxGeo.NxPlugin.dll   (NX 启动时调用 ufsta 自动加载)"
Write-Host "  nxgeo-overlay.exe    (插件按需拉起)"
Write-Host "重启 NX 生效。卸载：uninstall.ps1 -UserDir $UserDir"
