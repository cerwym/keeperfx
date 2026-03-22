param(
    [Parameter(Mandatory=$true)]
    [string]$WorkspaceFolder,
    [string]$DkInstallPath
)

$ErrorActionPreference = "Stop"

function Assert-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' was not found in PATH."
    }
}

function Test-DkFiles {
    param([string]$Root)
    $required = @(
        "data/bluepal.dat",
        "data/bluepall.dat",
        "data/dogpal.pal",
        "data/hitpall.dat",
        "data/lightng.pal",
        "data/main.pal",
        "data/mapfadeg.dat",
        "data/redpal.col",
        "data/redpall.dat",
        "data/slab0-0.dat",
        "data/slab0-1.dat",
        "data/vampal.pal",
        "data/whitepal.col",
        "sound/atmos1.sbk",
        "sound/atmos2.sbk",
        "sound/bullfrog.sbk"
    )

    $missing = @()
    foreach ($rel in $required) {
        $candidate = Join-Path $Root ($rel -replace '/', [IO.Path]::DirectorySeparatorChar)
        if (-not (Test-Path $candidate)) {
            $missing += $rel
        }
    }
    return $missing
}

function Get-CaseInsensitivePath {
    param(
        [string]$Root,
        [string]$RelativePath
    )
    $normalized = $RelativePath -replace '\\', '/'
    $parts = $normalized.Split('/', [System.StringSplitOptions]::RemoveEmptyEntries)
    $current = (Resolve-Path $Root).Path
    foreach ($part in $parts) {
        $next = Get-ChildItem -LiteralPath $current | Where-Object { $_.Name -ieq $part } | Select-Object -First 1
        if (-not $next) {
            return $null
        }
        $current = $next.FullName
    }
    return $current
}

Assert-Command docker

$workspace = (Resolve-Path $WorkspaceFolder).Path
$localDir = Join-Path $workspace ".local"
$configPath = Join-Path $localDir "dk-install-path.txt"

if (-not (Test-Path $localDir)) {
    New-Item -ItemType Directory -Path $localDir | Out-Null
}

if ([string]::IsNullOrWhiteSpace($DkInstallPath) -and (Test-Path $configPath)) {
    $cached = (Get-Content $configPath -Raw).Trim()
    if (-not [string]::IsNullOrWhiteSpace($cached)) {
        $DkInstallPath = $cached
    }
}

if ([string]::IsNullOrWhiteSpace($DkInstallPath)) {
    $DkInstallPath = Read-Host "Enter path to original Dungeon Keeper install (contains data/ and sound/)"
}

if ([string]::IsNullOrWhiteSpace($DkInstallPath)) {
    throw "No Dungeon Keeper install path provided."
}

$dkRoot = (Resolve-Path $DkInstallPath).Path
$missing = Test-DkFiles -Root $dkRoot
if ($missing.Count -gt 0) {
    throw "DK install path is missing required files: $($missing -join ', ')"
}

Set-Content -Path $configPath -Value $dkRoot -NoNewline

$dockerfile = Join-Path $workspace "docker/dk-originals/Dockerfile"
if (-not (Test-Path $dockerfile)) {
    throw "Dockerfile not found: $dockerfile"
}

Write-Host "Building local DK originals image keeperfx-dk-originals:local ..." -ForegroundColor Cyan
$dkStage = Join-Path $localDir "dk-stage"
if (Test-Path $dkStage) {
    Remove-Item -Recurse -Force $dkStage
}
New-Item -ItemType Directory -Force -Path (Join-Path $dkStage "data") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $dkStage "sound") | Out-Null

foreach ($rel in @(
    "data/bluepal.dat",
    "data/bluepall.dat",
    "data/dogpal.pal",
    "data/hitpall.dat",
    "data/lightng.pal",
    "data/main.pal",
    "data/mapfadeg.dat",
    "data/redpal.col",
    "data/redpall.dat",
    "data/slab0-0.dat",
    "data/slab0-1.dat",
    "data/vampal.pal",
    "data/whitepal.col",
    "sound/atmos1.sbk",
    "sound/atmos2.sbk",
    "sound/bullfrog.sbk"
)) {
    $src = Get-CaseInsensitivePath -Root $dkRoot -RelativePath $rel
    if (-not $src) {
        throw "Required file not found in DK install (case-insensitive lookup): $rel"
    }
    $dest = Join-Path $dkStage ($rel -replace '/', [IO.Path]::DirectorySeparatorChar)
    Copy-Item -LiteralPath $src -Destination $dest -Force
}

Push-Location $workspace
try {
    docker build --build-context "dk=$dkStage" -f "$dockerfile" -t keeperfx-dk-originals:local .
    if ($LASTEXITCODE -ne 0) {
        throw "Docker build failed."
    }
}
finally {
    Pop-Location
}

Write-Host "DK originals layer is ready: keeperfx-dk-originals:local" -ForegroundColor Green
Write-Host "Cached DK path: $dkRoot" -ForegroundColor Green
