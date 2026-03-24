# init-deploy.ps1
# Initializes local .deploy/ using two local-only Docker layers:
#   1) keeperfx-dk-originals:local   (legal files from user's original DK install)
#   2) keeperfx-runtime-assets:local (KeeperFX runtime assets from repo + generated pkg data)
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools/init-deploy.ps1 -DungeonKeeperPath "C:\Games\Dungeon Keeper"
#   powershell -ExecutionPolicy Bypass -File tools/init-deploy.ps1 -RefreshRuntimeLayer
#   powershell -ExecutionPolicy Bypass -File tools/init-deploy.ps1 -DungeonKeeperPath "C:\Games\DK" -KeeperFxReleasePath "C:\Games\KeeperFX-1.3.1"
#     -KeeperFxReleasePath  Optional. Overlays binary assets (levels, data, etc.) from a KeeperFX release
#                           into .deploy after Docker layers. Used to supply binary level map files not
#                           produced by the build (e.g. .slb, .clm, .tng). Script files are skipped.

param(
    [string]$WorkspaceFolder = (Split-Path $PSScriptRoot -Parent),
    [string]$DungeonKeeperPath,
    [string]$KeeperFxReleasePath,
    [switch]$RefreshDkLayer,
    [switch]$RefreshRuntimeLayer,
    [switch]$SkipPkgBuild
)

$ErrorActionPreference = "Stop"

$ws = (Resolve-Path $WorkspaceFolder).Path
$composeFile = Join-Path $ws "docker\compose.yml"
$deployDir = Join-Path $ws ".deploy"
$requiredDkList = Join-Path $ws "docs\files_required_from_original_dk.txt"
$dkDockerfile = Join-Path $ws "docker\dk-originals\Dockerfile"
$runtimeDockerfile = Join-Path $ws "docker\kfx-runtime-assets\Dockerfile"

$dkImage = "keeperfx/dk-originals:local"
$runtimeImage = "keeperfx/runtime-assets:local"

function Test-DockerImageExists {
    param([string]$ImageName)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "SilentlyContinue"
    docker image inspect $ImageName *> $null
    $result = ($LASTEXITCODE -eq 0)
    $ErrorActionPreference = $prev
    return $result
}

function Assert-Tooling {
    docker --version *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "Docker is required but not available on PATH."
    }

    docker compose version *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "Docker Compose is required but not available."
    }
}

function Get-RequiredDkFiles {
    param([string]$ListFile)

    $entries = @()
    Get-Content $ListFile | ForEach-Object {
        $line = $_.Trim()
        if ($line.StartsWith("./")) {
            $entries += $line.Substring(2)
        }
    }
    return $entries
}

function Assert-DkFilesPresent {
    param(
        [string]$DkRoot,
        [string[]]$RequiredFiles
    )

    $missing = @()
    foreach ($rel in $RequiredFiles) {
        # Case-insensitive search: find the file regardless of casing on disk
        $dir  = Split-Path $rel -Parent
        $leaf = Split-Path $rel -Leaf
        $searchDir = Join-Path $DkRoot $dir
        if (-not (Test-Path $searchDir)) {
            $missing += $rel
            continue
        }
        $found = Get-ChildItem -Path $searchDir -Filter $leaf -ErrorAction SilentlyContinue
        if (-not $found) {
            $missing += $rel
        }
    }

    if ($missing.Count -gt 0) {
        $joined = ($missing | ForEach-Object { "  - $_" }) -join "`n"
        throw "Dungeon Keeper path is missing required files:`n$joined"
    }
}

function New-NormalizedDkContext {
    # Copies required DK files into a temp directory with all-lowercase names.
    # This ensures the Docker build context is always case-consistent regardless
    # of whether the source install uses UPPERCASE, lowercase, or Mixed casing.
    param(
        [string]$DkRoot,
        [string[]]$RequiredFiles
    )

    $tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("dk-ctx-" + [System.IO.Path]::GetRandomFileName())
    New-Item -ItemType Directory -Path $tmpDir | Out-Null

    foreach ($rel in $RequiredFiles) {
        $dir  = Split-Path $rel -Parent
        $leaf = Split-Path $rel -Leaf
        $searchDir = Join-Path $DkRoot $dir
        $srcFile = Get-ChildItem -Path $searchDir -Filter $leaf -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($srcFile) {
            $destDir = Join-Path $tmpDir $dir.ToLower()
            if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir | Out-Null }
            Copy-Item -Path $srcFile.FullName -Destination (Join-Path $destDir $leaf.ToLower()) -Force
        }
    }

    # Also copy ldata/ wholesale (lowercased) for the wildcard COPY in the Dockerfile
    $ldataSrc = Join-Path $DkRoot "ldata"
    if (-not (Test-Path $ldataSrc)) {
        $ldataSrc = Get-ChildItem -Path $DkRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -ieq "ldata" } | Select-Object -First 1 -ExpandProperty FullName
    }
    if ($ldataSrc -and (Test-Path $ldataSrc)) {
        $ldataDest = Join-Path $tmpDir "ldata"
        if (-not (Test-Path $ldataDest)) { New-Item -ItemType Directory -Path $ldataDest | Out-Null }
        Get-ChildItem -Path $ldataSrc | ForEach-Object {
            Copy-Item -Path $_.FullName -Destination (Join-Path $ldataDest $_.Name.ToLower()) -Force
        }
    }

    return $tmpDir
}

function Ensure-DkOriginalsLayer {
    param(
        [string]$ImageName,
        [string]$Dockerfile,
        [string]$RepoRoot,
        [string]$DkPath,
        [string[]]$RequiredFiles,
        [switch]$ForceRebuild
    )

    $haveImage = Test-DockerImageExists -ImageName $ImageName
    if ($haveImage -and -not $ForceRebuild) {
        Write-Host "Using existing $ImageName" -ForegroundColor Green
        return
    }

    if ([string]::IsNullOrWhiteSpace($DkPath)) {
        throw "-DungeonKeeperPath is required when creating or refreshing $ImageName"
    }

    $dkRoot = (Resolve-Path $DkPath).Path
    Assert-DkFilesPresent -DkRoot $dkRoot -RequiredFiles $RequiredFiles

    Write-Host "Normalizing DK file casing into temp context..." -ForegroundColor Cyan
    $tmpCtx = New-NormalizedDkContext -DkRoot $dkRoot -RequiredFiles $RequiredFiles
    try {
        Write-Host "Building $ImageName from DK install at $dkRoot" -ForegroundColor Cyan
        docker build --build-context "dk=$tmpCtx" -f $Dockerfile -t $ImageName $RepoRoot
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to build $ImageName"
        }
    } finally {
        Remove-Item -Recurse -Force $tmpCtx
    }
}

function Build-RuntimeAssetsInDocker {
    param([string]$ComposeFilePath)

    Write-Host "Generating pkg runtime assets in docker/linux ..." -ForegroundColor Cyan
    docker compose -f $ComposeFilePath run --rm --remove-orphans linux bash -lc "make pkg-gfx && make pkg-sfx && make pkg-languages"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to generate pkg assets (pkg-gfx/pkg-sfx/pkg-languages)."
    }
}

function Ensure-RuntimeLayer {
    param(
        [string]$ImageName,
        [string]$Dockerfile,
        [string]$RepoRoot,
        [switch]$ForceRebuild,
        [switch]$SkipPkg
    )

    if (-not $SkipPkg) {
        Build-RuntimeAssetsInDocker -ComposeFilePath $composeFile
    }

    Write-Host "Building $ImageName (local runtime assets cache) ..." -ForegroundColor Cyan
    if ($ForceRebuild) {
        docker build --no-cache -f $Dockerfile -t $ImageName $RepoRoot
    } else {
        docker build -f $Dockerfile -t $ImageName $RepoRoot
    }

    if ($LASTEXITCODE -ne 0) {
        throw "Failed to build $ImageName"
    }
}

function Copy-ImageTreeToHost {
    param(
        [string]$ImageName,
        [string]$ContainerPath,
        [string]$DestinationPath
    )

    $cid = (docker create $ImageName /)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($cid)) {
        throw "Failed to create temp container from $ImageName"
    }

    try {
        docker cp "${cid}:${ContainerPath}/." $DestinationPath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to copy ${ContainerPath} from $ImageName"
        }
    }
    finally {
        docker rm $cid *> $null
    }
}

function Reset-DeployDirectory {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
        return
    }

    Get-ChildItem -Path $Path -Force | Remove-Item -Recurse -Force
}

function Copy-ReleaseOverlay {
    # Overlays binary asset files from a KeeperFX release into .deploy.
    # Script/config files (.txt, .cfg, .toml) are skipped - those come from the repo.
    param(
        [string]$ReleasePath,
        [string]$DeployPath
    )

    $releaseRoot = (Resolve-Path $ReleasePath).Path
    Write-Host "Overlaying binary assets from KeeperFX release at $releaseRoot" -ForegroundColor Cyan

    # Directories to overlay - copy binary data files only, skip script/config files
    $overlayDirs = @('levels', 'data', 'ldata', 'sound', 'music', 'fxdata', 'campgns')
    $skipExtensions = @('.txt', '.cfg', '.toml', '.log', '.ini', '.md')

    foreach ($dir in $overlayDirs) {
        $srcDir = Join-Path $releaseRoot $dir
        if (-not (Test-Path $srcDir)) { continue }

        Get-ChildItem -Path $srcDir -Recurse -File | ForEach-Object {
            if ($skipExtensions -contains $_.Extension.ToLower()) { return }
            $rel = $_.FullName.Substring($srcDir.Length).TrimStart('\')
            $dest = Join-Path $DeployPath (Join-Path $dir $rel)
            $destDir = Split-Path $dest -Parent
            if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir | Out-Null }
            # Only copy if destination doesn't already have the file (Docker layers take precedence)
            if (-not (Test-Path $dest)) {
                Copy-Item -Path $_.FullName -Destination $dest -Force
            }
        }
    }

    Write-Host "Release overlay complete." -ForegroundColor Green
}

Assert-Tooling

$requiredDkFiles = Get-RequiredDkFiles -ListFile $requiredDkList

Ensure-DkOriginalsLayer `
    -ImageName $dkImage `
    -Dockerfile $dkDockerfile `
    -RepoRoot $ws `
    -DkPath $DungeonKeeperPath `
    -RequiredFiles $requiredDkFiles `
    -ForceRebuild:$RefreshDkLayer

Ensure-RuntimeLayer `
    -ImageName $runtimeImage `
    -Dockerfile $runtimeDockerfile `
    -RepoRoot $ws `
    -ForceRebuild:$RefreshRuntimeLayer `
    -SkipPkg:$SkipPkgBuild

Write-Host "Resetting .deploy at $deployDir" -ForegroundColor Cyan
Reset-DeployDirectory -Path $deployDir

Copy-ImageTreeToHost -ImageName $runtimeImage -ContainerPath "/kfx" -DestinationPath $deployDir
Copy-ImageTreeToHost -ImageName $dkImage -ContainerPath "/dk" -DestinationPath $deployDir

if (-not [string]::IsNullOrWhiteSpace($KeeperFxReleasePath)) {
    Copy-ReleaseOverlay -ReleasePath $KeeperFxReleasePath -DeployPath $deployDir
}

Write-Host "Initialized .deploy from local Docker layers:" -ForegroundColor Green
Write-Host "  - $runtimeImage" -ForegroundColor Green
Write-Host "  - $dkImage" -ForegroundColor Green
if (-not [string]::IsNullOrWhiteSpace($KeeperFxReleasePath)) {
    Write-Host "  - KeeperFX release overlay: $KeeperFxReleasePath" -ForegroundColor Green
}

