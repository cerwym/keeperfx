# clion-build-deploy.ps1
# CLion "Before Launch" build + deploy script.
# Mirrors the VS Code "Build & Deploy Windows Debug" task exactly:
#   1. docker compose builds keeperfx (windows-x86-debug preset, in mingw32 container)
#   2. Copies keeperfx.exe into .deploy/ so CLion can launch it from there with CWD = .deploy/
#
# Run "Init .deploy/" task once before first use to populate the layered game-data directory.

param(
    [string]$WorkspaceFolder = (Split-Path $PSScriptRoot -Parent)
)

$ws = Resolve-Path $WorkspaceFolder
$composeFile = "$ws\docker\compose.yml"
$deployDir = "$ws\.deploy"

# Require initialized layered runtime assets (created once via init-deploy.ps1).
if (-not (Test-Path "$deployDir\data\main.pal") -or -not (Test-Path "$deployDir\sound\bullfrog.sbk")) {
    Write-Host "ERROR: .deploy is not initialized (missing required runtime assets)." -ForegroundColor Red
    Write-Host "Run once:" -ForegroundColor Yellow
    Write-Host '  powershell -ExecutionPolicy Bypass -File tools\init-deploy.ps1 -DungeonKeeperPath "<path-to-DungeonKeeper>"' -ForegroundColor Yellow
    exit 1
}

Write-Host "--- KeeperFX: Docker Build (windows-x86-debug) ---" -ForegroundColor Cyan

# If a previous devcontainer build left a stale CMakeCache pointing at /workspaces, nuke it first
docker compose -f $composeFile run --rm mingw32 bash -c `
    "if grep -q /workspaces out/build/windows-x86-debug/CMakeCache.txt 2>/dev/null; then rm -rf out/build/windows-x86-debug/CMakeCache.txt out/build/windows-x86-debug/CMakeFiles; fi && cmake --preset windows-x86-debug && cmake --build --preset windows-x86-debug"

if ($LASTEXITCODE -ne 0) {
    Write-Host "BUILD FAILED (exit $LASTEXITCODE)" -ForegroundColor Red
    exit 1
}

$exeSrc = "$ws\out\build\windows-x86-debug\keeperfx.exe"
$exeDst = "$ws\.deploy\keeperfx.exe"

if (-not (Test-Path $exeSrc)) {
    Write-Host "ERROR: $exeSrc not found after build." -ForegroundColor Red
    exit 1
}

Copy-Item $exeSrc $exeDst -Force
Write-Host "Deployed: keeperfx.exe -> .deploy/" -ForegroundColor Green

# Also copy into the CLion local-run profile dir so CMakeRunConfiguration finds it.
# CLion's "windows-x86-debug-run" profile has GENERATION_DIR=out/build/windows-x86-debug-run/
# and uses the local (non-Docker) toolchain so it runs keeperfx.exe on the Windows host.
$runDir = "$ws\out\build\windows-x86-debug-run"
if (-not (Test-Path $runDir)) { New-Item -ItemType Directory -Path $runDir | Out-Null }
Copy-Item $exeSrc "$runDir\keeperfx.exe" -Force
Write-Host "Deployed: keeperfx.exe -> out/build/windows-x86-debug-run/" -ForegroundColor Green
