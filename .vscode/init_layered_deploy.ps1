#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Creates a Docker-native layered deployment environment for local testing.

.DESCRIPTION
    Initializes the .deploy/ directory by assembling all layers via Docker:

    Layer 0 (DK originals)  — extracted from keeperfx-dk-originals:local image
    Layer 1 (SDL2 DLLs)     — extracted from keeperfx-build-mingw32:latest image
    Layer 2 (KFX gfx data)  — built by Docker and extracted to .deploy/data/ + .deploy/ldata/
    Layer 3 (Repo assets)   — Windows junctions → live edits in VS Code instant in .deploy/
    Layer 4 (Executable)    — populated by compile task on each build

    The DK originals image is built once per machine by init_dk_layer.ps1.
    If not present, this script will run it automatically.

    No clean master directory needed. No per-worktree prompts.

.EXAMPLE
    .\.vscode\init_layered_deploy.ps1
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$WorkspaceFolder = (Split-Path -Parent $PSScriptRoot),

    [Parameter(Mandatory=$false)]
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:C = @{
    Reset  = "`e[0m"; Green  = "`e[32m"; Yellow = "`e[33m"
    Blue   = "`e[34m"; Red   = "`e[31m"; Cyan   = "`e[36m"; Gray = "`e[90m"
}
function Write-C([string]$Msg, [string]$Color = 'Reset') {
    Write-Host "$($script:C[$Color])$Msg$($script:C.Reset)"
}

$DEPLOY      = Join-Path $WorkspaceFolder ".deploy"
$DK_IMAGE    = "keeperfx-dk-originals:local"
$MINGW_IMAGE = "ghcr.io/cerwym/keeperfx-build-mingw32:latest"

# Read DOCKER_ORG from env if set (matches compose.yml)
if ($env:DOCKER_ORG) { $MINGW_IMAGE = "ghcr.io/$($env:DOCKER_ORG)/keeperfx-build-mingw32:latest" }

Write-C "=== KeeperFX Layered Deploy Init ===" 'Cyan'
Write-Host ""

# Handle existing deploy directory
if (Test-Path $DEPLOY) {
    if (-not $Force) {
        Write-C "Deployment already exists at: $DEPLOY" 'Yellow'
        $r = Read-Host "Delete and recreate? (y/N)"
        if ($r -ne 'y') { Write-C "Aborted." 'Yellow'; exit 0 }
    }
    Write-C "Removing existing deployment..." 'Yellow'
    & "$PSScriptRoot\reset_layered_deploy.ps1" -Force
}

New-Item -ItemType Directory -Path $DEPLOY -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $DEPLOY "data")  -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $DEPLOY "sound") -Force | Out-Null

# ── Layer 0: DK originals ─────────────────────────────────────────────────────
Write-C "`nLayer 0: DK original files" 'Green'

$imageExists = docker image inspect $DK_IMAGE 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-C "  Image '$DK_IMAGE' not found. Running one-time setup..." 'Yellow'
    & "$PSScriptRoot\init_dk_layer.ps1" -WorkspaceFolder $WorkspaceFolder
    if ($LASTEXITCODE -ne 0) { Write-C "ERROR: DK layer setup failed." 'Red'; exit 1 }
}

$cid = docker create $DK_IMAGE 2>&1
docker cp "${cid}:/dk/data/."  (Join-Path $DEPLOY "data")  | Out-Null
docker cp "${cid}:/dk/sound/." (Join-Path $DEPLOY "sound") | Out-Null
docker rm $cid | Out-Null
Write-C "  ✓ 16 original DK files extracted" 'Green'

# ── Layer 1: SDL2 DLLs ────────────────────────────────────────────────────────
Write-C "`nLayer 1: SDL2 DLLs" 'Green'

Write-C "  Pulling $MINGW_IMAGE ..." 'Gray'
docker pull $MINGW_IMAGE | Out-Null

$cid = docker create $MINGW_IMAGE 2>&1
docker cp "${cid}:/usr/i686-w64-mingw32/bin/SDL2.dll"       (Join-Path $DEPLOY "SDL2.dll")       | Out-Null
docker cp "${cid}:/usr/i686-w64-mingw32/bin/SDL2_mixer.dll"  (Join-Path $DEPLOY "SDL2_mixer.dll")  | Out-Null
docker cp "${cid}:/usr/i686-w64-mingw32/bin/SDL2_image.dll"  (Join-Path $DEPLOY "SDL2_image.dll")  | Out-Null
docker cp "${cid}:/usr/i686-w64-mingw32/bin/SDL2_net.dll"    (Join-Path $DEPLOY "SDL2_net.dll")    | Out-Null
docker rm $cid | Out-Null
Write-C "  ✓ SDL2*.dll extracted" 'Green'

# ── Layer 2: KFX generated gfx data ──────────────────────────────────────────
Write-C "`nLayer 2: KFX generated graphics data" 'Green'
Write-C "  Building pkg-gfx via Docker (may take a few minutes first time)..." 'Yellow'

docker compose -f (Join-Path $WorkspaceFolder "docker\compose.yml") `
    run --rm linux bash -c "make pkg-gfx"

# Copy generated data files into .deploy/
$pkgData  = Join-Path $WorkspaceFolder "pkg\data"
$pkgLdata = Join-Path $WorkspaceFolder "pkg\ldata"

if (Test-Path $pkgData) {
    Copy-Item (Join-Path $pkgData "*") (Join-Path $DEPLOY "data") -Force
    Write-C "  ✓ pkg/data/ deployed" 'Green'
} else {
    Write-C "  ⚠  pkg/data/ not found — run make pkg-gfx manually if needed" 'Yellow'
}

if (Test-Path $pkgLdata) {
    New-Item -ItemType Directory -Path (Join-Path $DEPLOY "ldata") -Force | Out-Null
    Copy-Item (Join-Path $pkgLdata "*") (Join-Path $DEPLOY "ldata") -Force
    Write-C "  ✓ pkg/ldata/ deployed" 'Green'
}

# ── Layer 3: Repo assets (Windows junctions — live edits) ─────────────────────
Write-C "`nLayer 3: Repo asset junctions (live editing)" 'Green'

$junctions = @{
    "campgns" = "campgns"
    "levels"  = "levels"
    "lang"    = "lang"
    "fxdata"  = "fxdata"
    "config"  = "config"
}

foreach ($pair in $junctions.GetEnumerator()) {
    $src  = Join-Path $WorkspaceFolder $pair.Value
    $dest = Join-Path $DEPLOY $pair.Key
    if (Test-Path $src) {
        cmd /c mklink /J "$dest" "$src" 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-C "  ✓ $($pair.Key) -> repo/$($pair.Value)" 'Green'
        } else {
            Write-C "  ✗ Failed to create junction for $($pair.Key)" 'Red'
        }
    } else {
        Write-C "  ⚠  $($pair.Value) not found in repo — skipping junction" 'Yellow'
    }
}

# Placeholder keeperfx.cfg at root (game expects it there)
$cfgSrc = Join-Path $WorkspaceFolder "config\keeperfx.cfg"
if (Test-Path $cfgSrc) {
    cmd /c mklink /H (Join-Path $DEPLOY "keeperfx.cfg") "$cfgSrc" 2>&1 | Out-Null
    Write-C "  ✓ keeperfx.cfg (hard link)" 'Green'
}

# ── Done ──────────────────────────────────────────────────────────────────────
Write-Host ""
Write-C "=== Deployment Ready ===" 'Cyan'
Write-C "Location: $DEPLOY" 'Blue'
Write-C "Disk usage: ~25MB (junctions + Docker-extracted files)" 'Green'
Write-Host ""
Write-C "Next: press Ctrl+Shift+B in VS Code to build and deploy keeperfx.exe" 'Yellow'
