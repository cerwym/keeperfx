param(
    [Parameter(Mandatory=$true)]
    [string]$WorkspaceFolder
)

$ErrorActionPreference = "Stop"

$workspace = (Resolve-Path $WorkspaceFolder).Path
$deployDir = Join-Path $workspace ".deploy"
$deployData = Join-Path $deployDir "data"
$composeFile = Join-Path $workspace "docker/compose.yml"

if (-not (Test-Path $deployDir)) {
    throw ".deploy does not exist. Run 'Init .deploy/' first."
}
if (-not (Test-Path $composeFile)) {
    throw "Compose file not found: $composeFile"
}

Write-Host "Rebuilding gfx package in linux container..." -ForegroundColor Cyan
docker compose -f "$composeFile" run --rm linux bash -lc "make pkg-gfx"
if ($LASTEXITCODE -ne 0) {
    throw "make pkg-gfx failed."
}

$pkgData = Join-Path $workspace "pkg/data"
$pkgLdata = Join-Path $workspace "pkg/ldata"
$pkgFxdata = Join-Path $workspace "pkg/fxdata"
$pkgCampgns = Join-Path $workspace "pkg/campgns"
$pkgLevels = Join-Path $workspace "pkg/levels"
$deployLdata = Join-Path $deployDir "ldata"
$deployFxdata = Join-Path $deployDir "fxdata"
$deployCampgns = Join-Path $deployDir "campgns"
$deployLevels = Join-Path $deployDir "levels"

if (-not (Test-Path $pkgData) -and -not (Test-Path $pkgLdata) -and -not (Test-Path $pkgFxdata) -and -not (Test-Path $pkgCampgns) -and -not (Test-Path $pkgLevels)) {
    throw "No gfx package output found under pkg/data, pkg/ldata, pkg/fxdata, pkg/campgns, or pkg/levels."
}

New-Item -ItemType Directory -Force -Path $deployData | Out-Null
if (Test-Path $pkgData) {
    Copy-Item "$pkgData\*" $deployData -Recurse -Force
}
if (Test-Path $pkgLdata) {
    New-Item -ItemType Directory -Force -Path $deployLdata | Out-Null
    Copy-Item "$pkgLdata\*" $deployLdata -Recurse -Force
}
if (Test-Path $pkgFxdata) {
    New-Item -ItemType Directory -Force -Path $deployFxdata | Out-Null
    Copy-Item "$pkgFxdata\*" $deployFxdata -Recurse -Force
}
if (Test-Path $pkgCampgns) {
    New-Item -ItemType Directory -Force -Path $deployCampgns | Out-Null
    Copy-Item "$pkgCampgns\*" $deployCampgns -Recurse -Force
}
if (Test-Path $pkgLevels) {
    New-Item -ItemType Directory -Force -Path $deployLevels | Out-Null
    Copy-Item "$pkgLevels\*" $deployLevels -Recurse -Force
}

Write-Host "GFX layer refreshed in .deploy/data, .deploy/ldata, .deploy/fxdata, .deploy/campgns and .deploy/levels." -ForegroundColor Green
