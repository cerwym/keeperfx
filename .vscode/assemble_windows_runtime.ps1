param(
    [Parameter(Mandatory=$true)]
    [string]$WorkspaceFolder,
    [ValidateSet("windows-x86-debug", "windows-x86-release")]
    [string]$Preset = "windows-x86-release"
)

$ErrorActionPreference = "Stop"

$workspace = (Resolve-Path $WorkspaceFolder).Path
$deployDir = Join-Path $workspace ".deploy"
$exePath = Join-Path $workspace "out/build/$Preset/keeperfx.exe"
$outDir = Join-Path $workspace "out/package/$Preset"

if (-not (Test-Path $deployDir)) {
    throw ".deploy not initialized. Run 'Init .deploy/' first."
}
if (-not (Test-Path $exePath)) {
    throw "Executable not found: $exePath. Build first for preset '$Preset'."
}

Write-Host "Creating clean runtime package at $outDir ..." -ForegroundColor Cyan
if (Test-Path $outDir) {
    Remove-Item -Recurse -Force $outDir
}
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Write-Host "Copying layered runtime files from .deploy ..." -ForegroundColor Cyan
Copy-Item "$deployDir\*" $outDir -Recurse -Force

Write-Host "Copying core runtime config/campaign assets ..." -ForegroundColor Cyan
$configKeeperfx = Join-Path $workspace "config/keeperfx.cfg"
$configCreatrs = Join-Path $workspace "config/creatrs"
$configFxdata = Join-Path $workspace "config/fxdata"
$configMods = Join-Path $workspace "config/mods"
$campgnsDir = Join-Path $workspace "campgns"
$levelsDir = Join-Path $workspace "levels"
$docsReadme = Join-Path $workspace "docs/keeperfx_readme.txt"

if (Test-Path $configKeeperfx) {
    Copy-Item $configKeeperfx (Join-Path $outDir "keeperfx.cfg") -Force
}
if (Test-Path $configCreatrs) {
    Copy-Item $configCreatrs (Join-Path $outDir "creatrs") -Recurse -Force
}
if (Test-Path $configFxdata) {
    Copy-Item $configFxdata (Join-Path $outDir "fxdata") -Recurse -Force
}
if (Test-Path $configMods) {
    Copy-Item $configMods (Join-Path $outDir "mods") -Recurse -Force
}
if (Test-Path $campgnsDir) {
    Copy-Item $campgnsDir (Join-Path $outDir "campgns") -Recurse -Force
}
if (Test-Path $levelsDir) {
    Copy-Item $levelsDir (Join-Path $outDir "levels") -Recurse -Force
}
if (Test-Path $docsReadme) {
    Copy-Item $docsReadme (Join-Path $outDir "keeperfx_readme.txt") -Force
}

Write-Host "Copying SDL runtime DLLs ..." -ForegroundColor Cyan
$sdlRuntimeDir = Join-Path $workspace "sdl/for_final_package"
foreach ($dll in @("SDL2.dll", "SDL2_image.dll", "SDL2_mixer.dll", "SDL2_net.dll")) {
    $dllPath = Join-Path $sdlRuntimeDir $dll
    if (Test-Path $dllPath) {
        Copy-Item $dllPath (Join-Path $outDir $dll) -Force
    }
}

Write-Host "Overlaying latest generated package assets (if available) ..." -ForegroundColor Cyan
$pkgData = Join-Path $workspace "pkg/data"
$pkgLdata = Join-Path $workspace "pkg/ldata"
$pkgFxdata = Join-Path $workspace "pkg/fxdata"
$pkgCampgns = Join-Path $workspace "pkg/campgns"
$pkgLevels = Join-Path $workspace "pkg/levels"
$outData = Join-Path $outDir "data"
$outLdata = Join-Path $outDir "ldata"
$outFxdata = Join-Path $outDir "fxdata"
$outCampgns = Join-Path $outDir "campgns"
$outLevels = Join-Path $outDir "levels"

if (Test-Path $pkgData) {
    New-Item -ItemType Directory -Force -Path $outData | Out-Null
    Copy-Item "$pkgData\*" $outData -Recurse -Force
}
if (Test-Path $pkgLdata) {
    New-Item -ItemType Directory -Force -Path $outLdata | Out-Null
    Copy-Item "$pkgLdata\*" $outLdata -Recurse -Force
}
if (Test-Path $pkgFxdata) {
    New-Item -ItemType Directory -Force -Path $outFxdata | Out-Null
    Copy-Item "$pkgFxdata\*" $outFxdata -Recurse -Force
}
if (Test-Path $pkgCampgns) {
    New-Item -ItemType Directory -Force -Path $outCampgns | Out-Null
    Copy-Item "$pkgCampgns\*" $outCampgns -Recurse -Force
}
if (Test-Path $pkgLevels) {
    New-Item -ItemType Directory -Force -Path $outLevels | Out-Null
    Copy-Item "$pkgLevels\*" $outLevels -Recurse -Force
}

# Keep DK/release-provided sound banks for runtime. pkg/sound assets are not
# guaranteed to match the expected bank layout consumed by bflib_sndlib.

Write-Host "Copying executable ..." -ForegroundColor Cyan
Copy-Item $exePath (Join-Path $outDir "keeperfx.exe") -Force

$required = @(
    (Join-Path $outDir "keeperfx.exe"),
    (Join-Path $outDir "keeperfx.cfg"),
    (Join-Path $outDir "SDL2.dll"),
    (Join-Path $outDir "data"),
    (Join-Path $outDir "sound"),
    (Join-Path $outDir "campgns"),
    (Join-Path $outDir "levels"),
    (Join-Path $outDir "fxdata")
)
$missing = @()
foreach ($entry in $required) {
    if (-not (Test-Path $entry)) {
        $missing += $entry
    }
}
if ($missing.Count -gt 0) {
    throw "Runtime package missing required outputs: $($missing -join ', ')"
}

Write-Host "Runtime package ready: $outDir" -ForegroundColor Green
