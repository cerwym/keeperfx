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

Assert-Command docker

$workspace = (Resolve-Path $WorkspaceFolder).Path
$deployDir = Join-Path $workspace ".deploy"
$deployData = Join-Path $deployDir "data"
$deploySound = Join-Path $deployDir "sound"
$deployLevels = Join-Path $deployDir "levels"
$composeFile = Join-Path $workspace "docker/compose.yml"
$initDkScript = Join-Path $workspace ".vscode/init_dk_layer.ps1"

if (-not (Test-Path $composeFile)) {
    throw "Compose file not found: $composeFile"
}
if (-not (Test-Path $initDkScript)) {
    throw "Script not found: $initDkScript"
}

Write-Host "Ensuring local DK originals layer exists..." -ForegroundColor Cyan
if ($DkInstallPath) {
    & $initDkScript -WorkspaceFolder $workspace -DkInstallPath $DkInstallPath
} else {
    & $initDkScript -WorkspaceFolder $workspace
}
if ($LASTEXITCODE -ne 0) {
    throw "Failed to initialize DK layer image."
}

Write-Host "Creating .deploy scaffold..." -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path $deployData | Out-Null
New-Item -ItemType Directory -Force -Path $deploySound | Out-Null
New-Item -ItemType Directory -Force -Path $deployLevels | Out-Null

$dkStage = Join-Path $workspace ".local/dk-stage"
$dkConfigPath = Join-Path $workspace ".local/dk-install-path.txt"
if ((Test-Path (Join-Path $dkStage "data")) -and (Test-Path (Join-Path $dkStage "sound"))) {
    Write-Host "Copying DK originals from normalized local stage..." -ForegroundColor Cyan
    Copy-Item (Join-Path $dkStage "data/*") $deployData -Recurse -Force
    Copy-Item (Join-Path $dkStage "sound/*") $deploySound -Recurse -Force
} elseif (Test-Path $dkConfigPath) {
    $dkRoot = (Get-Content $dkConfigPath -Raw).Trim()
    if ([string]::IsNullOrWhiteSpace($dkRoot) -or -not (Test-Path $dkRoot)) {
        throw "Cached DK install path is invalid; rerun Init DK Originals Layer."
    }
    Write-Host "Copying DK originals directly from cached install path..." -ForegroundColor Cyan
    Copy-Item (Join-Path $dkRoot "data/*") $deployData -Recurse -Force
    Copy-Item (Join-Path $dkRoot "sound/*") $deploySound -Recurse -Force
} else {
    throw "No DK source found. Rerun Init DK Originals Layer."
}

if (Test-Path $dkConfigPath) {
    $dkRootFromCfg = (Get-Content $dkConfigPath -Raw).Trim()
    $dkLevels = Join-Path $dkRootFromCfg "levels"
    if (-not [string]::IsNullOrWhiteSpace($dkRootFromCfg) -and (Test-Path $dkLevels)) {
        Write-Host "Copying original DK levels into .deploy/levels..." -ForegroundColor Cyan
        Copy-Item (Join-Path $dkLevels "*") $deployLevels -Recurse -Force
    }
}

Write-Host "Building KeeperFX asset packages (pkg-gfx/pkg-languages/pkg-sfx)..." -ForegroundColor Cyan
docker compose -f "$composeFile" run --rm linux bash -lc "make -j1 pkg-gfx && make -j1 pkg-languages && make -j1 pkg-sfx"
if ($LASTEXITCODE -ne 0) {
    throw "Asset packaging failed."
}

Write-Host "Staging generated pkg assets into .deploy..." -ForegroundColor Cyan
$pkgData = Join-Path $workspace "pkg/data"
$pkgLdata = Join-Path $workspace "pkg/ldata"
$pkgFxdata = Join-Path $workspace "pkg/fxdata"
$pkgCampgns = Join-Path $workspace "pkg/campgns"
$pkgLevels = Join-Path $workspace "pkg/levels"
$deployLdata = Join-Path $deployDir "ldata"
$deployFxdata = Join-Path $deployDir "fxdata"
$deployCampgns = Join-Path $deployDir "campgns"
$deployLevels = Join-Path $deployDir "levels"

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

# Keep DK/release-provided sound banks for runtime. pkg/sound assets are not
# guaranteed to match the expected bank layout consumed by bflib_sndlib.

# KeeperFX community maps (classic/standard/lostlvls) ship their binary data
# only in the official "complete" release archive, not in the git repo.
# Download and extract them when needed.
$kfxReleaseUrl = "https://github.com/dkfans/keeperfx/releases/download/v1.3.1/keeperfx_1_3_1_complete.7z"
$kfxCacheDir = Join-Path $workspace ".local/kfx-complete"
$kfxArchive = Join-Path $kfxCacheDir "keeperfx_1_3_1_complete.7z"
$kfxExtracted = Join-Path $kfxCacheDir "extracted"
$sevenZip = "C:\Program Files\7-Zip\7z.exe"

$needRelease = $false
foreach ($pack in @("classic", "standard", "lostlvls")) {
    $packDir = Join-Path $deployLevels $pack
    $hasDat = (Get-ChildItem $packDir -File -Filter "*.dat" -ErrorAction SilentlyContinue).Count -gt 0
    if (-not $hasDat) { $needRelease = $true; break }
}

if ($needRelease -and (Test-Path $sevenZip)) {
    Write-Host "Community map binaries missing; sourcing from KeeperFX release archive..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $kfxCacheDir | Out-Null
    if (-not (Test-Path $kfxArchive)) {
        Write-Host "Downloading keeperfx complete release (~356 MB)..." -ForegroundColor Yellow
        Invoke-WebRequest -Uri $kfxReleaseUrl -OutFile $kfxArchive -UseBasicParsing
    }
    if (-not (Test-Path (Join-Path $kfxExtracted "levels"))) {
        Write-Host "Extracting map pack levels..." -ForegroundColor Yellow
        New-Item -ItemType Directory -Force -Path $kfxExtracted | Out-Null
        & $sevenZip x $kfxArchive -o"$kfxExtracted" -y 'levels\classic\*' 'levels\standard\*' 'levels\lostlvls\*' | Out-Null
    }
    foreach ($pack in @("classic", "standard", "lostlvls")) {
        $src = Join-Path $kfxExtracted "levels/$pack"
        $dst = Join-Path $deployLevels $pack
        if (Test-Path $src) {
            New-Item -ItemType Directory -Force -Path $dst | Out-Null
            Copy-Item "$src\*" $dst -Recurse -Force
            Write-Host "  Hydrated levels/$pack from release archive." -ForegroundColor Green
        }
    }
} elseif ($needRelease) {
    Write-Host "WARNING: Community map binaries missing and 7-Zip not found at $sevenZip. Free Play maps will be unavailable." -ForegroundColor Yellow
}

Write-Host "Running runtime asset preflight checks..." -ForegroundColor Cyan
& (Join-Path $workspace ".vscode/validate_runtime_assets.ps1") -WorkspaceFolder $workspace -DeploySubdir ".deploy"
if ($LASTEXITCODE -ne 0) {
    throw "Runtime asset preflight failed. See .deploy/runtime-asset-report.txt"
}

Write-Host ".deploy is initialized and layered for host runtime testing." -ForegroundColor Green
