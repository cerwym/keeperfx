#!/usr/bin/env pwsh
<#
.SYNOPSIS
    One-time setup: caches original Dungeon Keeper files into a local Docker image.

.DESCRIPTION
    Asks for your DK GOG/Steam install path ONCE.
    Builds the local-only 'keeperfx-dk-originals:local' Docker image that stores
    the 16 required original DK files as an immutable image layer.

    This image is machine-wide — all worktrees on this machine share it.
    It is never pushed to any registry.

    After this script completes successfully, you will never be asked for your
    DK install path again (on this machine).

.EXAMPLE
    .\.vscode\init_dk_layer.ps1
    .\.vscode\init_dk_layer.ps1 -DkPath "E:\Games\DungeonKeeper"
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$DkPath,

    [Parameter(Mandatory=$false)]
    [string]$WorkspaceFolder = (Split-Path -Parent $PSScriptRoot)
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:C = @{
    Reset  = "`e[0m"; Green  = "`e[32m"; Yellow = "`e[33m"
    Blue   = "`e[34m"; Red   = "`e[31m"; Cyan   = "`e[36m"
}
function Write-C([string]$Msg, [string]$Color = 'Reset') {
    Write-Host "$($script:C[$Color])$Msg$($script:C.Reset)"
}

$IMAGE = "keeperfx-dk-originals:local"
$DOCKERFILE = Join-Path $WorkspaceFolder "docker\dk-originals\Dockerfile"

# Required files relative to DK install root
$REQUIRED = @(
    "data\bluepal.dat",  "data\bluepall.dat", "data\dogpal.pal",
    "data\hitpall.dat",  "data\lightng.pal",  "data\main.pal",
    "data\mapfadeg.dat", "data\redpal.col",   "data\redpall.dat",
    "data\slab0-0.dat",  "data\slab0-1.dat",  "data\vampal.pal",
    "data\whitepal.col",
    "sound\atmos1.sbk",  "sound\atmos2.sbk",  "sound\bullfrog.sbk"
)

Write-C "=== KeeperFX DK Originals Layer Setup ===" 'Cyan'
Write-Host ""

# Check if image already exists
$existing = docker image inspect $IMAGE 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-C "Image '$IMAGE' already exists on this machine." 'Green'
    Write-C "To rebuild it, run: docker image rm $IMAGE && .\.vscode\init_dk_layer.ps1" 'Yellow'
    exit 0
}

# Get DK install path
if (-not $DkPath) {
    Write-C "KeeperFX needs 16 files from an original Dungeon Keeper installation." 'Yellow'
    Write-C "Compatible sources: Dungeon Keeper (GOG), Dungeon Keeper Gold (GOG/Steam)," 'Yellow'
    Write-C "or any retail CD version. The Deeper Dungeons addon is NOT sufficient." 'Yellow'
    Write-Host ""
    $DkPath = Read-Host "Enter the path to your Dungeon Keeper installation"
    $DkPath = $DkPath.Trim().Trim('"')
}

if (-not (Test-Path $DkPath)) {
    Write-C "ERROR: Path not found: $DkPath" 'Red'
    exit 1
}

# Validate all required files are present
Write-Host ""
Write-C "Validating required files in: $DkPath" 'Blue'
$missing = @()
foreach ($rel in $REQUIRED) {
    $full = Join-Path $DkPath $rel
    if (Test-Path $full) {
        Write-Host "  ✓ $rel" -ForegroundColor DarkGray
    } else {
        Write-Host "  ✗ $rel" -ForegroundColor Red
        $missing += $rel
    }
}

if ($missing.Count -gt 0) {
    Write-Host ""
    Write-C "ERROR: $($missing.Count) required file(s) not found." 'Red'
    Write-C "Make sure you are pointing at the correct DK installation directory." 'Yellow'
    Write-C "(GOG version: the files are inside the CD image — mount/extract it first.)" 'Yellow'
    exit 1
}

Write-Host ""
Write-C "All 16 files found. Building Docker image..." 'Green'
Write-Host ""

# Stage files into a temp directory with lowercase names.
# This avoids Steam file handles and NTFS/BuildKit case-sensitivity issues
# when Docker's Linux daemon resolves the build context.
$STAGING = Join-Path $env:TEMP "kfx-dk-staging-$([System.IO.Path]::GetRandomFileName())"
New-Item -ItemType Directory -Path (Join-Path $STAGING "data")  -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $STAGING "sound") -Force | Out-Null

foreach ($rel in $REQUIRED) {
    $src  = Join-Path $DkPath $rel
    $dest = Join-Path $STAGING ($rel.ToLower())
    Copy-Item $src $dest -Force
}
Write-C "  Staged 16 files to temp directory." 'Gray'

# Build the local Docker image using the staging directory as the 'dk' context
docker build `
    --build-context "dk=$STAGING" `
    -f $DOCKERFILE `
    -t $IMAGE `
    $WorkspaceFolder

$exitCode = $LASTEXITCODE
Remove-Item $STAGING -Recurse -Force -ErrorAction SilentlyContinue

if ($exitCode -ne 0) {
    Write-C "ERROR: Docker build failed." 'Red'
    exit 1
}

Write-Host ""
Write-C "=== DK Layer Ready ===" 'Cyan'
Write-C "Image '$IMAGE' is now available on this machine." 'Green'
Write-C "All worktrees will use it automatically — you won't be asked again." 'Green'
Write-Host ""
Write-C "Next: run the 'Default task' (Ctrl+Shift+B) or:" 'Yellow'
Write-C "  .\.vscode\init_layered_deploy.ps1" 'Yellow'
