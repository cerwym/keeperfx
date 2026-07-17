<#
.SYNOPSIS
    Fetches KeeperFX asset packs (gfx, sfx, custom) into the working tree.

.DESCRIPTION
    Reads build/packaging/assets.json for canonical sources (dkfans repos, pinned tags).
    If build/packaging/assets.local.json exists, its entries override the canonical ones —
    useful when working on a feature that needs a fork, branch, or local path.

    Override formats in assets.local.json:
      Fork/branch:  { "repo": "cerwym/FXGraphics", "tag": "my-branch", "dest": "gfx" }
      Local path:   { "path": "C:/source/FXGraphics", "dest": "gfx" }

.PARAMETER Force
    Re-fetch even if destination directory already exists.

.EXAMPLE
    .\build\packaging\fetch-assets.ps1
    .\build\packaging\fetch-assets.ps1 -Force
#>
param(
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Repo root is two levels up (build/packaging/ -> build/ -> repo root)
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

function Merge-Assets {
    param($base, $overrides)
    $merged = @{}
    foreach ($key in $base.PSObject.Properties.Name) {
        $merged[$key] = $base.$key
    }
    foreach ($key in $overrides.PSObject.Properties.Name) {
        if ($key -notlike '_*') {
            $merged[$key] = $overrides.$key
        }
    }
    return $merged
}

# Load manifest
$manifest = Get-Content "$PSScriptRoot\assets.json" -Raw | ConvertFrom-Json

# Apply local overrides if present
$localFile = "$PSScriptRoot\assets.local.json"
if (Test-Path $localFile) {
    Write-Host "Applying local overrides from assets.local.json" -ForegroundColor Cyan
    $localOverrides = Get-Content $localFile -Raw | ConvertFrom-Json
    $assets = Merge-Assets $manifest $localOverrides
} else {
    $assets = @{}
    foreach ($key in $manifest.PSObject.Properties.Name) {
        $assets[$key] = $manifest.$key
    }
}

foreach ($name in $assets.Keys) {
    $entry = $assets[$name]
    $dest  = Join-Path $root $entry.dest

    if ((Test-Path $dest) -and -not $Force) {
        Write-Host "[$name] already present at '$($entry.dest)' (use -Force to re-fetch)" -ForegroundColor DarkGray
        continue
    }

    if (Test-Path $dest) {
        Write-Host "[$name] removing existing '$($entry.dest)'" -ForegroundColor Yellow
        Remove-Item $dest -Recurse -Force
    }

    # Local path override — copy the directory
    if ($entry.PSObject.Properties['path']) {
        $src = $entry.path
        if (-not (Test-Path $src)) {
            Write-Error "[$name] local path '$src' does not exist"
        }
        Write-Host "[$name] copying from local path: $src" -ForegroundColor Green
        Copy-Item $src $dest -Recurse -Force
        continue
    }

    # Remote repo — shallow clone at tag or branch
    $repo   = $entry.repo
    $ref    = $entry.tag
    $url    = "https://github.com/$repo.git"
    Write-Host "[$name] cloning $repo @ $ref" -ForegroundColor Green
    git clone --quiet --depth 1 --branch $ref $url $dest
    if ($LASTEXITCODE -ne 0) {
        Write-Error "[$name] git clone failed"
    }
}

Write-Host "`nAssets ready." -ForegroundColor Green
