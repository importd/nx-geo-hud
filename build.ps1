$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$msbuild = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' 2>$null
    if ($found) { $msbuild = ($found | Select-Object -First 1) }
}
if (-not $msbuild) {
    # fallback: known VS2019 Community location
    $msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe'
}
if (-not (Test-Path $msbuild)) { Write-Host "MSBuild not found; pass -MsBuild <path>"; exit 1 }

# Use a workspace-local temp dir: the system %TEMP% can lose its sandbox ACLs,
# and then cl.exe cannot create its temporary files.
$tempDir = Join-Path $root '.temp'
New-Item -ItemType Directory -Force $tempDir | Out-Null
$env:TEMP = $tempDir
$env:TMP = $tempDir

$projs = @()
foreach ($dir in @('src', 'tests')) {
    $full = Join-Path $root $dir
    if (Test-Path $full) { $projs += Get-ChildItem -Path $full -Recurse -Filter *.vcxproj }
}
if ($projs.Count -eq 0) { Write-Host 'no projects found'; exit 1 }

foreach ($p in $projs) {
    Write-Host "== building $($p.Name)"
    & $msbuild $p.FullName /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=v142 /v:m /nologo /nodeReuse:false
    if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED: $($p.Name)"; exit $LASTEXITCODE }
}

$exe = Join-Path $root 'tests\NxGeo.Tests\x64\Debug\NxGeo.Tests.exe'
if (-not (Test-Path $exe)) { Write-Host "test exe not found: $exe"; exit 1 }
& $exe
exit $LASTEXITCODE
