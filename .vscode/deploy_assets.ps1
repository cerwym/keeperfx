#!/usr/bin/env pwsh
# KeeperFX Asset Deployment Script
# Deploys compiled assets to layered deployment directory (.deploy/)
#
# Usage Examples:
#   .\.vscode\deploy_assets.ps1 -All
#   .\.vscode\deploy_assets.ps1 -DeployGraphics -DeploySounds
#   .\.vscode\deploy_assets.ps1 -DeployExecutable

param(
    [string]$GameDirectory = (Join-Path (Join-Path $PSScriptRoot "..") ".deploy"),
    [switch]$DeployExecutable,
    [switch]$DeployGraphics,
    [switch]$DeploySounds,
    [switch]$DeployLocalization,
    [switch]$DeployConfig,
    [switch]$All,
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

if ($All) {
    $DeployExecutable = $true
    $DeployGraphics = $true
    $DeploySounds = $true
    $DeployLocalization = $true
    $DeployConfig = $true
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

Write-Host ""
Write-Host "=" * 80 -ForegroundColor Cyan
Write-Host "KeeperFX Asset Deployment (Incremental)" -ForegroundColor Cyan
Write-Host "=" * 80 -ForegroundColor Cyan
Write-Host ""
Write-Host "Target: $GameDirectory" -ForegroundColor Yellow
Write-Host ""

if (-not (Test-Path $GameDirectory)) {
    Write-Host "Error: Deployment directory not found" -ForegroundColor Red
    Write-Host "   Path: $GameDirectory" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Run tools\init-deploy.ps1 first to create the deployment." -ForegroundColor Yellow
    exit 1
}

$deployedCount = 0

# ============================================================================
# Deploy Executable
# ============================================================================

if ($DeployExecutable) {
    Write-Host "[1/5] Deploying Executable" -ForegroundColor Green
    Write-Host ""
    
    $exePath = Join-Path $repoRoot "bin\keeperfx.exe"
    if (Test-Path $exePath) {
        Copy-Item $exePath "$GameDirectory\" -Force
        Write-Host "  + keeperfx.exe" -ForegroundColor Green
        $deployedCount++
    } else {
        Write-Host "  WARNING: keeperfx.exe not found (compile first?)" -ForegroundColor Yellow
    }
    Write-Host ""
}

# ============================================================================
# Deploy Graphics
# ============================================================================

if ($DeployGraphics) {
    Write-Host "[2/5] Deploying Graphics" -ForegroundColor Green
    Write-Host ""
    
    # Ensure target directories exist
    $dataDir = Join-Path $GameDirectory "data"
    $ldataDir = Join-Path $GameDirectory "ldata"
    
    if (-not (Test-Path $dataDir)) { New-Item -ItemType Directory -Path $dataDir | Out-Null }
    if (-not (Test-Path $ldataDir)) { New-Item -ItemType Directory -Path $ldataDir | Out-Null }
    
    # GUI sprites (gui2-64.dat)
    $guiPath = Join-Path $repoRoot "pkg\data\gui2-64.dat"
    if (Test-Path $guiPath) {
        Copy-Item $guiPath "$dataDir\" -Force
        Write-Host "  + gui2-64.dat" -ForegroundColor Green
        $deployedCount++
    } else {
        Write-Host "  WARNING: gui2-64.dat not found" -ForegroundColor Yellow
    }
    
    # Engine textures (tmap*.dat)
    $tmapFiles = Get-ChildItem (Join-Path $repoRoot "pkg\data\tmap*.dat") -ErrorAction SilentlyContinue
    if ($tmapFiles) {
        foreach ($file in $tmapFiles) {
            Copy-Item $file.FullName "$dataDir\" -Force
            Write-Host "  + $($file.Name)" -ForegroundColor Green
            $deployedCount++
        }
    } else {
        Write-Host "  WARNING: No tmap*.dat files found" -ForegroundColor Yellow
    }
    
    # Land views (ldata/*.dat)
    $ldataFiles = Get-ChildItem (Join-Path $repoRoot "pkg\ldata\*.dat") -ErrorAction SilentlyContinue
    if ($ldataFiles) {
        foreach ($file in $ldataFiles) {
            Copy-Item $file.FullName "$ldataDir\" -Force
            if ($Verbose) {
                Write-Host "  + ldata\$($file.Name)" -ForegroundColor Green
            }
            $deployedCount++
        }
        if (-not $Verbose) {
            Write-Host "  + $($ldataFiles.Count) land view files" -ForegroundColor Green
        }
    } else {
        Write-Host "  WARNING: No land view files found" -ForegroundColor Yellow
    }
    
    Write-Host ""
}

# ============================================================================
# Deploy Sounds
# ============================================================================

if ($DeploySounds) {
    Write-Host "[3/5] Deploying Sounds" -ForegroundColor Green
    Write-Host ""
    
    $soundPath = Join-Path $repoRoot "pkg\data\sound.dat"
    if (Test-Path $soundPath) {
        $dataDir = Join-Path $GameDirectory "data"
        if (-not (Test-Path $dataDir)) { New-Item -ItemType Directory -Path $dataDir | Out-Null }
        
        Copy-Item $soundPath "$dataDir\" -Force
        Write-Host "  + sound.dat" -ForegroundColor Green
        $deployedCount++
    } else {
        Write-Host "  WARNING: sound.dat not found" -ForegroundColor Yellow
    }
    
    # Speech banks (speech_*.dat)
    $speechFiles = Get-ChildItem (Join-Path $repoRoot "pkg\data\speech_*.dat") -ErrorAction SilentlyContinue
    if ($speechFiles) {
        foreach ($file in $speechFiles) {
            Copy-Item $file.FullName "$dataDir\" -Force
            if ($Verbose) {
                Write-Host "  + $($file.Name)" -ForegroundColor Green
            }
            $deployedCount++
        }
        if (-not $Verbose) {
            Write-Host "  + $($speechFiles.Count) speech files" -ForegroundColor Green
        }
    }
    
    Write-Host ""
}

# ============================================================================
# Deploy Localization
# ============================================================================

if ($DeployLocalization) {
    Write-Host "[4/5] Deploying Localization" -ForegroundColor Green
    Write-Host ""
    
    $langPath = Join-Path $repoRoot "pkg\lang"
    if (Test-Path $langPath) {
        $targetLangDir = Join-Path $GameDirectory "lang"
        if (-not (Test-Path $targetLangDir)) { New-Item -ItemType Directory -Path $targetLangDir | Out-Null }
        
        $moFiles = Get-ChildItem "$langPath\*.mo" -ErrorAction SilentlyContinue
        if ($moFiles) {
            foreach ($file in $moFiles) {
                Copy-Item $file.FullName "$targetLangDir\" -Force
                if ($Verbose) {
                    Write-Host "  + $($file.Name)" -ForegroundColor Green
                }
                $deployedCount++
            }
            if (-not $Verbose) {
                Write-Host "  + $($moFiles.Count) translation files" -ForegroundColor Green
            }
        } else {
            Write-Host "  WARNING: No .mo files found" -ForegroundColor Yellow
        }
    } else {
        Write-Host "  WARNING: pkg\lang directory not found" -ForegroundColor Yellow
    }
    
    Write-Host ""
}

# ============================================================================
# Deploy Configuration
# ============================================================================

if ($DeployConfig) {
    Write-Host "[5/5] Syncing config/ -> .deploy/ (newer files only)" -ForegroundColor Green
    Write-Host ""

    $configSrc = Join-Path $repoRoot "config"
    $synced = 0
    $skipped = 0

    Get-ChildItem -Path $configSrc -Recurse -File | ForEach-Object {
        $rel  = $_.FullName.Substring($configSrc.Length).TrimStart('\/')
        $dest = Join-Path $GameDirectory $rel
        $destDir = Split-Path $dest -Parent
        if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir -Force | Out-Null }

        if (-not (Test-Path $dest) -or ($_.LastWriteTime -gt (Get-Item $dest).LastWriteTime)) {
            Copy-Item -Path $_.FullName -Destination $dest -Force
            if ($Verbose) { Write-Host "  + $rel" -ForegroundColor Green }
            $synced++
        } else {
            $skipped++
        }
    }

    Write-Host "  $synced file(s) updated, $skipped unchanged" -ForegroundColor Green
    Write-Host ""
    $deployedCount += $synced
}

# ============================================================================
# Summary
# ============================================================================

Write-Host "=" * 80 -ForegroundColor Green
if ($deployedCount -gt 0) {
    Write-Host "Deployment Complete! ($deployedCount files)" -ForegroundColor Green
} else {
    Write-Host "WARNING: No files deployed" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Tips:" -ForegroundColor Yellow
    Write-Host "  - Compile first: docker compose -f build/docker/compose.yml run --rm mingw32 bash -c 'cmake --preset windows-x86-release && cmake --build --preset windows-x86-release'" -ForegroundColor White
    Write-Host "  - Specify what to deploy: -DeployGraphics, -DeploySounds, etc." -ForegroundColor White
}
Write-Host "=" * 80 -ForegroundColor Green
Write-Host ""
