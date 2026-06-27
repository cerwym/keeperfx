# init-deploy.ps1
# Initializes local .deploy/ using two local-only Docker layers:
#   1) keeperfx/dk-originals:local   (legal files from user's original DK install)
#   2) keeperfx/runtime-assets:local (KeeperFX runtime assets from repo + generated pkg data)
#
# Then optionally overlays a KeeperFX release and/or alpha patch for binary assets
# (community map levels, etc.) that the build does not produce.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools/init-deploy.ps1 -DungeonKeeperPath "C:\Games\Dungeon Keeper"
#   powershell -ExecutionPolicy Bypass -File tools/init-deploy.ps1 -RefreshRuntimeLayer
#   powershell -ExecutionPolicy Bypass -File tools/init-deploy.ps1 -UseAlpha
#   powershell -ExecutionPolicy Bypass -File tools/init-deploy.ps1 -KeeperFxVersion 1.3.2
#   powershell -ExecutionPolicy Bypass -File tools/init-deploy.ps1 -KeeperFxReleasePath "C:\Downloads\kfx"
#
# Parameters:
#   -DungeonKeeperPath     Path to original DK install. Cached in ~/.keeperfx-dev/ after first use.
#   -KeeperFxVersion       Pin a specific KFX release version (e.g. "1.3.2"). Default: auto-detect latest.
#   -UseAlpha              Also overlay the latest alpha patch on top of the full release.
#   -SkipKfxOverlay        Skip the KFX release/alpha overlay entirely.
#   -KeeperFxReleasePath   Use a locally extracted KFX release instead of downloading. Skips auto-resolve.
#   -RefreshDkLayer        Force rebuild of the DK originals Docker image.
#   -RefreshRuntimeLayer   Force rebuild of the runtime assets Docker image.
#   -SkipPkgBuild          Skip running make pkg-* before rebuilding the runtime image.

param(
    [string]$WorkspaceFolder = (Split-Path $PSScriptRoot -Parent),
    [string]$DungeonKeeperPath,
    [string]$KeeperFxVersion,
    [string]$KeeperFxReleasePath,
    [switch]$UseAlpha,
    [switch]$SkipKfxOverlay,
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

# Shared cache directory in user home — works across git worktrees
$kfxDevDir  = Join-Path $HOME ".keeperfx-dev"
$kfxCacheDir = Join-Path $kfxDevDir "cache"
$kfxExtractDir = Join-Path $kfxCacheDir "extracted"
$dkPathFile = Join-Path $kfxDevDir "dk-install-path.txt"

# ---------------------------------------------------------------------------
# Tooling checks
# ---------------------------------------------------------------------------

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

function Assert-SevenZip {
    $script:sevenZipExe = $null
    foreach ($candidate in @("7z", "C:\Program Files\7-Zip\7z.exe", "C:\Program Files (x86)\7-Zip\7z.exe")) {
        $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($cmd) { $script:sevenZipExe = $cmd.Source; return }
    }
    throw "7-Zip is required but was not found. Install from https://www.7-zip.org/"
}

# ---------------------------------------------------------------------------
# DK required-files list
# ---------------------------------------------------------------------------

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

# ---------------------------------------------------------------------------
# DK path resolution (user-home cache → repo-local fallback → interactive)
# ---------------------------------------------------------------------------

function Resolve-DkInstallPath {
    param([string]$Provided)

    if (-not [string]::IsNullOrWhiteSpace($Provided)) {
        $resolved = (Resolve-Path $Provided).Path
        Set-Content -Path $dkPathFile -Value $resolved -NoNewline
        return $resolved
    }

    # 1. Shared cache in ~/.keeperfx-dev/
    if (Test-Path $dkPathFile) {
        $cached = (Get-Content $dkPathFile -Raw).Trim()
        if (-not [string]::IsNullOrWhiteSpace($cached) -and (Test-Path $cached)) {
            Write-Host "Using cached DK path: $cached" -ForegroundColor DarkGray
            return $cached
        }
    }

    # 2. Legacy per-repo .local/ (backward compat — read-only)
    $legacyPath = Join-Path $ws ".local\dk-install-path.txt"
    if (Test-Path $legacyPath) {
        $cached = (Get-Content $legacyPath -Raw).Trim()
        if (-not [string]::IsNullOrWhiteSpace($cached) -and (Test-Path $cached)) {
            Write-Host "Migrating DK path from .local/ to ~/.keeperfx-dev/ ..." -ForegroundColor DarkGray
            Set-Content -Path $dkPathFile -Value $cached -NoNewline
            return $cached
        }
    }

    return $null
}

# ---------------------------------------------------------------------------
# DK file validation + normalized Docker context
# ---------------------------------------------------------------------------

function Assert-DkFilesPresent {
    param([string]$DkRoot, [string[]]$RequiredFiles)

    $missing = @()
    foreach ($rel in $RequiredFiles) {
        $dir  = Split-Path $rel -Parent
        $leaf = Split-Path $rel -Leaf
        $searchDir = Join-Path $DkRoot $dir
        if (-not (Test-Path $searchDir)) { $missing += $rel; continue }
        $found = Get-ChildItem -Path $searchDir -Filter $leaf -ErrorAction SilentlyContinue
        if (-not $found) { $missing += $rel }
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
    param([string]$DkRoot, [string[]]$RequiredFiles)

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

    # Copy ldata/ wholesale (lowercased) for the wildcard COPY in the Dockerfile
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

# ---------------------------------------------------------------------------
# Docker layer management
# ---------------------------------------------------------------------------

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

    $haveImage = Test-DockerImageExists -ImageName $ImageName
    if ($haveImage -and -not $ForceRebuild) {
        Write-Host "Using existing $ImageName" -ForegroundColor Green
        return
    }

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
    param([string]$ImageName, [string]$ContainerPath, [string]$DestinationPath)

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

# ---------------------------------------------------------------------------
# KFX version resolution via GitHub API
# ---------------------------------------------------------------------------

function Invoke-GitHubApi {
    param([string]$Url)

    $headers = @{ "User-Agent" = "keeperfx-init-deploy/1.0"; "Accept" = "application/vnd.github+json" }
    $token = $env:GITHUB_TOKEN
    if (-not [string]::IsNullOrWhiteSpace($token)) {
        $headers["Authorization"] = "Bearer $token"
    }

    return Invoke-RestMethod -Uri $Url -Headers $headers -ErrorAction Stop
}

function Assert-GitHubToken {
    if ([string]::IsNullOrWhiteSpace($env:GITHUB_TOKEN)) {
        throw @"
A GitHub token is required to download alpha patch artifacts.
Set `$env:GITHUB_TOKEN before running this script, or create one at:
  https://github.com/settings/tokens
The token only needs the 'public_repo' read scope (or no scopes for public repos).
"@
    }
}

function Resolve-KfxLatestRelease {
    # Returns @{ Tag = "v1.3.2"; DownloadUrl = "https://...complete.7z"; FileName = "..." }
    Write-Host "Querying GitHub for latest KFX stable release..." -ForegroundColor Cyan
    $releases = Invoke-GitHubApi "https://api.github.com/repos/dkfans/keeperfx/releases"
    $stable = $releases | Where-Object { -not $_.prerelease -and -not $_.draft } | Select-Object -First 1
    if (-not $stable) { throw "No stable KFX release found on GitHub." }

    $asset = $stable.assets | Where-Object { $_.name -like "*_complete.7z" } | Select-Object -First 1
    if (-not $asset) { throw "No _complete.7z asset found in release $($stable.tag_name)." }

    return @{ Tag = $stable.tag_name; DownloadUrl = $asset.browser_download_url; FileName = $asset.name }
}

function Resolve-KfxSpecificRelease {
    param([string]$Version)
    $tag = if ($Version.StartsWith("v")) { $Version } else { "v$Version" }
    Write-Host "Querying GitHub for KFX release $tag ..." -ForegroundColor Cyan
    try {
        $release = Invoke-GitHubApi "https://api.github.com/repos/dkfans/keeperfx/releases/tags/$tag"
    } catch {
        throw "KFX release $tag not found on GitHub: $_"
    }
    $asset = $release.assets | Where-Object { $_.name -like "*_complete.7z" } | Select-Object -First 1
    if (-not $asset) { throw "No _complete.7z asset found in release $tag." }
    return @{ Tag = $release.tag_name; DownloadUrl = $asset.browser_download_url; FileName = $asset.name }
}

function Get-KfxAlphaBuilds {
    # Returns a list of unique Windows signed alpha builds, newest first.
    # Each entry: @{ BuildNum = 5159; Version = "1.3.2"; ArtifactId = ...; Date = ...; SizeMB = ... }
    Write-Host "Fetching alpha build list from GitHub Actions..." -ForegroundColor Cyan

    $allArtifacts = @()
    $page = 1
    do {
        $response = Invoke-GitHubApi "https://api.github.com/repos/dkfans/keeperfx/actions/artifacts?per_page=100&page=$page"
        $allArtifacts += $response.artifacts
        $hasMore = $response.artifacts.Count -eq 100
        $page++
    } while ($hasMore -and $allArtifacts.Count -lt 500)

    # Filter: Windows (no 'linux' prefix), signed alpha patches, not expired
    $alphaPattern = '^keeperfx-(\d+)_(\d+)_(\d+)_(\d+)_Alpha-patch-signed$'
    $seen = @{}
    $builds = @()

    foreach ($art in ($allArtifacts | Where-Object { -not $_.expired })) {
        if ($art.name -match $alphaPattern) {
            $buildNum = [int]$Matches[4]
            $version  = "$($Matches[1]).$($Matches[2]).$($Matches[3])"
            if (-not $seen.ContainsKey($buildNum)) {
                $seen[$buildNum] = $true
                $builds += @{
                    BuildNum   = $buildNum
                    Version    = $version
                    ArtifactId = $art.id
                    ArtifactName = $art.name
                    Date       = ([datetime]$art.created_at).ToString("yyyy-MM-dd")
                    SizeMB     = [math]::Round($art.size_in_bytes / 1MB, 1)
                }
            }
        }
    }

    return $builds | Sort-Object { $_.BuildNum } -Descending
}

function Select-KfxAlpha {
    # Shows a paginated list of alpha builds and returns the selected one.
    # If $AutoLatest is set, skips the UI and returns the latest build.
    param([switch]$AutoLatest)

    $builds = Get-KfxAlphaBuilds

    if ($builds.Count -eq 0) {
        throw "No alpha builds found. They may have expired (GitHub keeps artifacts for 90 days)."
    }

    if ($AutoLatest) {
        $sel = $builds[0]
        Write-Host "Auto-selected latest alpha: Build #$($sel.BuildNum) v$($sel.Version) ($($sel.Date))" -ForegroundColor Cyan
        return $sel
    }

    $pageSize = 10
    $page = 0
    $totalPages = [math]::Ceiling($builds.Count / $pageSize)

    while ($true) {
        $start = $page * $pageSize
        $slice = $builds | Select-Object -Skip $start -First $pageSize

        Write-Host ""
        Write-Host "KeeperFX Alpha Patches (page $($page + 1) of $totalPages):" -ForegroundColor Cyan
        $i = 1
        foreach ($b in $slice) {
            $latest = if ($page -eq 0 -and $i -eq 1) { "  <- latest" } else { "" }
            Write-Host ("  {0,2}) Build #{1}  v{2}  {3}  {4} MB{5}" -f $i, $b.BuildNum, $b.Version, $b.Date, $b.SizeMB, $latest)
            $i++
        }
        Write-Host ""
        $prompt = "[Enter]=latest (#{0})  [1-$([math]::Min($pageSize, $slice.Count))]=select" -f $builds[0].BuildNum
        if ($page -lt $totalPages - 1) { $prompt += "  [n]=next" }
        if ($page -gt 0)               { $prompt += "  [p]=prev" }
        $prompt += "  [q]=quit"
        $input = Read-Host $prompt

        if ([string]::IsNullOrWhiteSpace($input)) {
            return $builds[0]
        }
        if ($input -eq 'q') { throw "Alpha selection cancelled." }
        if ($input -eq 'n' -and $page -lt $totalPages - 1) { $page++; continue }
        if ($input -eq 'p' -and $page -gt 0)               { $page--; continue }

        $num = 0
        if ([int]::TryParse($input, [ref]$num) -and $num -ge 1 -and $num -le $slice.Count) {
            return $slice[$num - 1]
        }
        Write-Host "Invalid selection - try again." -ForegroundColor Yellow
    }
}

# ---------------------------------------------------------------------------
# Archive download + extraction (cached in ~/.keeperfx-dev/)
# ---------------------------------------------------------------------------

function Get-CachedArchive {
    # Downloads a release archive (no auth needed) and caches it.
    param([string]$DownloadUrl, [string]$FileName)

    New-Item -ItemType Directory -Force -Path $kfxCacheDir | Out-Null
    $dest = Join-Path $kfxCacheDir $FileName

    if (Test-Path $dest) {
        Write-Host "Using cached archive: $FileName" -ForegroundColor DarkGray
        return $dest
    }

    Write-Host "Downloading $FileName ..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $DownloadUrl -OutFile $dest -UseBasicParsing
    Write-Host "Download complete: $FileName" -ForegroundColor Green
    return $dest
}

function Get-CachedArtifact {
    # Downloads a GitHub Actions artifact (requires $env:GITHUB_TOKEN).
    # GitHub wraps artifact content in a .zip; this saves it and returns the path.
    param([long]$ArtifactId, [string]$ArtifactName)

    New-Item -ItemType Directory -Force -Path $kfxCacheDir | Out-Null
    $destZip = Join-Path $kfxCacheDir ("$ArtifactName.zip")

    if (Test-Path $destZip) {
        Write-Host "Using cached artifact: $ArtifactName" -ForegroundColor DarkGray
        return $destZip
    }

    $url = "https://api.github.com/repos/dkfans/keeperfx/actions/artifacts/$ArtifactId/zip"
    $headers = @{
        "User-Agent"    = "keeperfx-init-deploy/1.0"
        "Accept"        = "application/vnd.github+json"
        "Authorization" = "Bearer $env:GITHUB_TOKEN"
    }

    Write-Host "Downloading alpha artifact $ArtifactName ..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $url -OutFile $destZip -Headers $headers
    Write-Host "Download complete: $ArtifactName" -ForegroundColor Green
    return $destZip
}

function Expand-KfxArchive {
    # Extracts a .7z archive, caches the extracted dir.
    param([string]$ArchivePath, [string]$Label)

    New-Item -ItemType Directory -Force -Path $kfxExtractDir | Out-Null
    $outDir = Join-Path $kfxExtractDir $Label

    if (Test-Path $outDir) {
        Write-Host "Using cached extraction: $Label" -ForegroundColor DarkGray
        return $outDir
    }

    Write-Host "Extracting $Label ..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    & $script:sevenZipExe x $ArchivePath "-o$outDir" -y | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Remove-Item $outDir -Recurse -Force -ErrorAction SilentlyContinue
        throw "Extraction failed for $ArchivePath"
    }

    Write-Host "Extraction complete: $Label" -ForegroundColor Green
    return $outDir
}

function Expand-KfxArtifactZip {
    # Expands the GitHub artifact .zip wrapper.
    # If the zip contains a single .7z file (the actual patch), also extracts that.
    param([string]$ZipPath, [string]$Label)

    New-Item -ItemType Directory -Force -Path $kfxExtractDir | Out-Null
    $outDir = Join-Path $kfxExtractDir $Label

    if (Test-Path $outDir) {
        Write-Host "Using cached artifact extraction: $Label" -ForegroundColor DarkGray
        return $outDir
    }

    Write-Host "Extracting artifact $Label ..." -ForegroundColor Cyan
    $zipStage = "${outDir}_zip"
    New-Item -ItemType Directory -Force -Path $zipStage | Out-Null
    Expand-Archive -Path $ZipPath -DestinationPath $zipStage -Force

    # Check if the zip contained a single .7z (the actual patch archive)
    $innerArchives = Get-ChildItem $zipStage -Filter "*.7z" -ErrorAction SilentlyContinue
    if ($innerArchives.Count -eq 1) {
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
        & $script:sevenZipExe x $innerArchives[0].FullName "-o$outDir" -y | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Remove-Item $outDir -Recurse -Force -ErrorAction SilentlyContinue
            Remove-Item $zipStage -Recurse -Force -ErrorAction SilentlyContinue
            throw "Extraction of inner archive failed: $($innerArchives[0].Name)"
        }
        Remove-Item $zipStage -Recurse -Force
    } else {
        # Zip already contained the patch files directly
        Rename-Item $zipStage $outDir
    }

    Write-Host "Extraction complete: $Label" -ForegroundColor Green
    return $outDir
}

# ---------------------------------------------------------------------------
# KFX release overlay
# ---------------------------------------------------------------------------

function Apply-KfxOverlay {
    # Overlays asset files from an extracted KFX tree into .deploy.
    # Only root-level 'keeperfx.cfg' is protected (repo-specific user config).
    # All other files in overlay dirs -- including .cfg/.toml game data -- are copied.
    # Documentation/log files (.txt, .md, .log) are skipped everywhere.
    # When $Overwrite is set, existing files are replaced (used for alpha patches on top of release).
    param([string]$SourceRoot, [string]$DeployPath, [switch]$Overwrite, [string]$Label)

    $label = if ($Label) { $Label } else { $SourceRoot }
    Write-Host "Overlaying assets from $label ..." -ForegroundColor Cyan

    $overlayDirs    = @('levels', 'data', 'ldata', 'sound', 'music', 'fxdata', 'campgns', 'creatrs', 'mods', 'multiplayer')
    $skipExtensions = @('.txt', '.log', '.md')

    foreach ($dir in $overlayDirs) {
        $srcDir = Join-Path $SourceRoot $dir
        if (-not (Test-Path $srcDir)) { continue }

        Get-ChildItem -Path $srcDir -Recurse -File | ForEach-Object {
            if ($skipExtensions -contains $_.Extension.ToLower()) { return }
            $rel  = $_.FullName.Substring($srcDir.Length).TrimStart('\')
            $dest = Join-Path $DeployPath (Join-Path $dir $rel)
            $destParent = Split-Path $dest -Parent
            if (-not (Test-Path $destParent)) { New-Item -ItemType Directory -Path $destParent | Out-Null }
            if ($Overwrite -or -not (Test-Path $dest)) {
                Copy-Item -Path $_.FullName -Destination $dest -Force
            }
        }
    }

    # Also copy root-level DLLs and support executables (but not keeperfx.exe --
    # that comes from the developer's own build).
    $skipRootFiles = @('keeperfx.exe', 'keeperfx.map', 'keeperfx.ilk', 'keeperfx.pdb')
    Get-ChildItem -Path $SourceRoot -File | ForEach-Object {
        if ($skipExtensions -contains $_.Extension.ToLower()) { return }
        if ($skipRootFiles -contains $_.Name.ToLower()) { return }
        $dest = Join-Path $DeployPath $_.Name
        if ($Overwrite -or -not (Test-Path $dest)) {
            Copy-Item -Path $_.FullName -Destination $dest -Force
        }
    }

    Write-Host "Overlay complete: $label" -ForegroundColor Green
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

Assert-Tooling

# Ensure shared cache dirs exist
New-Item -ItemType Directory -Force -Path $kfxDevDir | Out-Null

$requiredDkFiles = Get-RequiredDkFiles -ListFile $requiredDkList

# Resolve DK path (param > ~/.keeperfx-dev/ > .local/ > prompt)
$resolvedDkPath = Resolve-DkInstallPath -Provided $DungeonKeeperPath
if ([string]::IsNullOrWhiteSpace($resolvedDkPath)) {
    if (Test-DockerImageExists -ImageName $dkImage) {
        Write-Host "No DK path provided; using existing $dkImage" -ForegroundColor Yellow
    } else {
        $resolvedDkPath = Read-Host "Enter path to original Dungeon Keeper install"
        if ([string]::IsNullOrWhiteSpace($resolvedDkPath)) {
            throw "A Dungeon Keeper install path is required to build $dkImage for the first time."
        }
        $resolvedDkPath = (Resolve-Path $resolvedDkPath).Path
        Set-Content -Path $dkPathFile -Value $resolvedDkPath -NoNewline
    }
}

Ensure-DkOriginalsLayer `
    -ImageName $dkImage `
    -Dockerfile $dkDockerfile `
    -RepoRoot $ws `
    -DkPath $resolvedDkPath `
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
Copy-ImageTreeToHost -ImageName $dkImage      -ContainerPath "/dk"  -DestinationPath $deployDir

# KFX release + alpha overlay
if (-not $SkipKfxOverlay) {
    if (-not [string]::IsNullOrWhiteSpace($KeeperFxReleasePath)) {
        # Manual local path provided — use it directly, skip download
        Apply-KfxOverlay -SourceRoot $KeeperFxReleasePath -DeployPath $deployDir -Label "local release at $KeeperFxReleasePath"
        if ($UseAlpha) {
            Write-Host "WARNING: -UseAlpha is ignored when -KeeperFxReleasePath is specified." -ForegroundColor Yellow
        }
    } else {
        Assert-SevenZip

        # Resolve full release
        $releaseInfo = if (-not [string]::IsNullOrWhiteSpace($KeeperFxVersion)) {
            Resolve-KfxSpecificRelease -Version $KeeperFxVersion
        } else {
            Resolve-KfxLatestRelease
        }

        Write-Host "KFX release: $($releaseInfo.Tag)" -ForegroundColor Cyan
        $releaseArchive  = Get-CachedArchive -DownloadUrl $releaseInfo.DownloadUrl -FileName $releaseInfo.FileName
        $releaseExtracted = Expand-KfxArchive -ArchivePath $releaseArchive -Label $releaseInfo.Tag

        # The complete archive may have a single top-level subfolder
        $releaseRoot = $releaseExtracted
        $children = Get-ChildItem $releaseExtracted -Directory
        if ($children.Count -eq 1 -and -not (Test-Path (Join-Path $releaseExtracted "levels"))) {
            $releaseRoot = $children[0].FullName
        }

        Apply-KfxOverlay -SourceRoot $releaseRoot -DeployPath $deployDir -Overwrite -Label "KFX $($releaseInfo.Tag)"

        if ($UseAlpha) {
            Assert-GitHubToken
            $alphaBuild = Select-KfxAlpha
            Write-Host "KFX alpha:   Build #$($alphaBuild.BuildNum) v$($alphaBuild.Version) ($($alphaBuild.Date))" -ForegroundColor Cyan
            $alphaLabel  = "alpha-build-$($alphaBuild.BuildNum)"
            $alphaZip    = Get-CachedArtifact -ArtifactId $alphaBuild.ArtifactId -ArtifactName $alphaBuild.ArtifactName
            $alphaExtracted = Expand-KfxArtifactZip -ZipPath $alphaZip -Label $alphaLabel

            $alphaRoot = $alphaExtracted
            $alphaChildren = Get-ChildItem $alphaExtracted -Directory
            if ($alphaChildren.Count -eq 1 -and -not (Test-Path (Join-Path $alphaExtracted "levels"))) {
                $alphaRoot = $alphaChildren[0].FullName
            }

            # Alpha overlays on top with overwrite — patch takes precedence over release
            Apply-KfxOverlay -SourceRoot $alphaRoot -DeployPath $deployDir -Overwrite -Label "KFX alpha build #$($alphaBuild.BuildNum)"
        }
    }
}

Write-Host ""
Write-Host "Initialized .deploy from:" -ForegroundColor Green
Write-Host "  $runtimeImage" -ForegroundColor Green
Write-Host "  $dkImage" -ForegroundColor Green
if (-not $SkipKfxOverlay) {
    if (-not [string]::IsNullOrWhiteSpace($KeeperFxReleasePath)) {
        Write-Host "  KFX release overlay (local): $KeeperFxReleasePath" -ForegroundColor Green
    } elseif (Test-Path variable:releaseInfo) {
        Write-Host "  KFX release overlay: $($releaseInfo.Tag)" -ForegroundColor Green
        if ($UseAlpha -and (Test-Path variable:alphaBuild)) {
            Write-Host "  KFX alpha overlay:   Build #$($alphaBuild.BuildNum) v$($alphaBuild.Version)" -ForegroundColor Green
        }
    }
}

