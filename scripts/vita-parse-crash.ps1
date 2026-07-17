<#
.SYNOPSIS
    Fetch and parse the latest KeeperFX crash dump from the PS Vita.

.DESCRIPTION
    Runs the vita_crash Python tool inside Docker (build-vitasdk image).
    Downloads crash dumps via FTP, parses the .psp2dmp binary, symbolicates the
    stack using DWARF debug info, and produces text + HTML reports.

    Python dependencies (pyelftools) are cached in a Docker volume.

.PARAMETER VitaIP
    Vita IP address. Default: 192.168.0.66

.PARAMETER VitaPort
    Vita FTP port. Default: 1337

.PARAMETER Dump
    Local .psp2dmp file to analyze (skips FTP download).

.PARAMETER Interactive
    Prompt to select dump from FTP listing.

.PARAMETER All
    Show all dumps, not just eboot.bin ones.

.PARAMETER Format
    Output format: text, html, or all. Default: all.

.EXAMPLE
    .\tools\vita-parse-crash.ps1
    .\tools\vita-parse-crash.ps1 -VitaIP 192.168.0.100
    .\tools\vita-parse-crash.ps1 -Dump out\vita-dumps\crash.psp2dmp
#>
param(
    [string]$VitaIP = $(if ($env:VITA_IP) { $env:VITA_IP } else { "192.168.0.66" }),
    [int]$VitaPort = 1337,
    [string]$Dump,
    [switch]$Interactive,
    [switch]$All,
    [ValidateSet("text", "html", "all")]
    [string]$Format = "all"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path $PSScriptRoot -Parent

$DOCKER_ORG = if ($env:DOCKER_ORG) { $env:DOCKER_ORG } else { "cerwym" }
$VITASDK_IMAGE = "ghcr.io/$DOCKER_ORG/build-vitasdk:latest"
$TOOLS_VOLUME = "keeperfx-vita-tools"

# Build CLI arguments for the Python tool
$toolArgs = @(
    "--vita-ip", $VitaIP,
    "--vita-port", $VitaPort,
    "--format", $Format
)

if ($Dump) {
    # Map local dump path to container path
    $DumpDir = Split-Path (Resolve-Path $Dump) -Parent
    $DumpName = Split-Path $Dump -Leaf
    $toolArgs += @("--dump", "/dumps/$DumpName")
}
if ($Interactive) { $toolArgs += "--interactive" }
if ($All) { $toolArgs += "--all" }

# Docker volumes
$volumes = @(
    "-v", "${TOOLS_VOLUME}:/tools",
    "-v", "${RepoRoot}:/src"
)
if ($Dump) {
    $volumes += @("-v", "${DumpDir}:/dumps")
}

Write-Host "Running vita_crash tool in Docker..." -ForegroundColor Cyan

docker run --rm @volumes `
    -w /src `
    $VITASDK_IMAGE `
    bash /src/scripts/vita_crash/docker-entry.sh @toolArgs

if ($LASTEXITCODE -ne 0) {
    Write-Host "Crash analysis failed." -ForegroundColor Red
    exit 1
}

Write-Host "`nDone. Reports saved to out/vita-dumps/" -ForegroundColor Green
