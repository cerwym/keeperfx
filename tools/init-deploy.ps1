# init-deploy.ps1
# Initializes local .deploy/ using two local-only Docker layers:
#   1) keeperfx-dk-originals:local   (legal files from user's original DK install)
#   2) keeperfx-runtime-assets:local (KeeperFX runtime assets from repo + generated pkg data)
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools/init-deploy.ps1 -DungeonKeeperPath "C:\Games\Dungeon Keeper"
#   powershell -ExecutionPolicy Bypass -File tools/init-deploy.ps1 -RefreshRuntimeLayer

param(
    [string]$WorkspaceFolder = (Split-Path $PSScriptRoot -Parent),
    [string]$DungeonKeeperPath,
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

$dkImage = "keeperfx-dk-originals:local"
$runtimeImage = "keeperfx-runtime-assets:local"

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

    Write-Host "Building $ImageName from DK install at $dkRoot" -ForegroundColor Cyan
    docker build --build-context "dk=$dkRoot" -f $Dockerfile -t $ImageName $RepoRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to build $ImageName"
    }
}

function Build-RuntimeAssetsInDocker {
    param([string]$ComposeFilePath)

    Write-Host "Generating pkg runtime assets in docker/linux ..." -ForegroundColor Cyan
    docker compose -f $ComposeFilePath run --rm linux bash -lc "make pkg-gfx && make pkg-sfx && make pkg-languages"
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

Write-Host "Initialized .deploy from local Docker layers:" -ForegroundColor Green
Write-Host "  - $runtimeImage" -ForegroundColor Green
Write-Host "  - $dkImage" -ForegroundColor Green

