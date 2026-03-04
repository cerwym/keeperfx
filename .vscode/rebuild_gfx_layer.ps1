#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Rebuilds Layer 2 (KFX generated gfx data) and redeploys into .deploy/.

.DESCRIPTION
    Checks the gfx submodule commit hash against the last successful build.
    Rebuilds via 'make pkg-gfx' in Docker only when the submodule has changed
    or no cached output exists. Always redeploys the result into .deploy/.

    Run this after pulling gfx submodule changes, or to force a fresh build.

.EXAMPLE
    .\.vscode\rebuild_gfx_layer.ps1
    .\.vscode\rebuild_gfx_layer.ps1 -Force
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
    Reset = "`e[0m"; Green = "`e[32m"; Yellow = "`e[33m"
    Blue  = "`e[34m"; Red  = "`e[31m"; Cyan  = "`e[36m"; Gray = "`e[90m"
}
function Write-C([string]$Msg, [string]$Color = 'Reset') {
    Write-Host "$($script:C[$Color])$Msg$($script:C.Reset)"
}

$DEPLOY   = Join-Path $WorkspaceFolder ".deploy"
$pkgData  = Join-Path $WorkspaceFolder "pkg\data"
$pkgLdata = Join-Path $WorkspaceFolder "pkg\ldata"
$hashFile = Join-Path $WorkspaceFolder "pkg\.gfx-hash"

Write-C "=== KeeperFX Layer 2: GFX Data ===" 'Cyan'
Write-Host ""

if (-not (Test-Path $DEPLOY)) {
    Write-C "ERROR: .deploy/ not found. Run 'Init .deploy/' first." 'Red'
    exit 1
}

# Ensure gfx submodule is initialized
$subStatus = (git -C $WorkspaceFolder submodule status gfx 2>&1)
if ($subStatus -match '^-') {
    Write-C "Initializing gfx submodule..." 'Yellow'
    git -C $WorkspaceFolder submodule update --init gfx 2>$null
    if ($LASTEXITCODE -ne 0) {
        # git worktree quirk: submodule update --init can fail with "No such ref: HEAD"
        # Fall back to cloning directly from the HTTPS URL
        $gfxUrl = (git -C $WorkspaceFolder config submodule.gfx.url 2>$null)
        if (-not $gfxUrl) { $gfxUrl = "https://github.com/Cerwym/FXGraphics.git" }
        Write-C "  submodule init failed; cloning directly from $gfxUrl" 'Yellow'
        $gfxBranch = (git -C $WorkspaceFolder config submodule.gfx.branch 2>$null)
        if (-not $gfxBranch) { $gfxBranch = "develop" }
        git clone $gfxUrl (Join-Path $WorkspaceFolder "gfx") --branch $gfxBranch --single-branch --depth 1
        if ($LASTEXITCODE -ne 0) {
            Write-C "ERROR: gfx clone failed." 'Red'
            exit 1
        }
    }
    $subStatus = (git -C $WorkspaceFolder submodule status gfx 2>&1)
}

# Get current submodule commit hash
$currentHash = if ($subStatus -match '^\s*[-+]?([0-9a-f]{40})\s') { $Matches[1] } else { $null }
if (-not $currentHash) {
    Write-C "ERROR: Could not determine gfx submodule hash." 'Red'
    exit 1
}

$cachedHash = if (Test-Path $hashFile) { (Get-Content $hashFile -Raw).Trim() } else { "" }

if (-not $Force -and (Test-Path $pkgData) -and ($currentHash -eq $cachedHash)) {
    Write-C "gfx data is up to date (hash $($currentHash.Substring(0,8)))." 'Green'
    Write-C "Use -Force to rebuild anyway." 'Gray'
} else {
    $reason = if ($Force) { "forced" } elseif (-not (Test-Path $pkgData)) { "no cache" } else { "submodule changed" }
    Write-C "Building pkg-gfx ($reason, hash $($currentHash.Substring(0,8)))..." 'Yellow'
    Write-Host ""

    docker compose -f (Join-Path $WorkspaceFolder "docker\compose.yml") `
        run --rm linux bash -c "make pkg-gfx -j\$(nproc)"

    if ($LASTEXITCODE -ne 0) {
        Write-C "ERROR: make pkg-gfx failed." 'Red'
        exit 1
    }

    New-Item -ItemType Directory -Path (Split-Path $hashFile) -Force | Out-Null
    Set-Content $hashFile $currentHash
    Write-C "`n✓ pkg-gfx built (hash $($currentHash.Substring(0,8)))" 'Green'
}

# Deploy into .deploy/
Write-C "`nDeploying to .deploy/..." 'Blue'

if (Test-Path $pkgData) {
    Copy-Item (Join-Path $pkgData "*") (Join-Path $DEPLOY "data") -Force
    Write-C "  ✓ pkg/data/ deployed" 'Green'
} else {
    Write-C "  ⚠  pkg/data/ not found" 'Yellow'
}

if (Test-Path $pkgLdata) {
    New-Item -ItemType Directory -Path (Join-Path $DEPLOY "ldata") -Force | Out-Null
    Copy-Item (Join-Path $pkgLdata "*") (Join-Path $DEPLOY "ldata") -Force
    Write-C "  ✓ pkg/ldata/ deployed" 'Green'
}

Write-Host ""
Write-C "=== Layer 2 complete ===" 'Cyan'
