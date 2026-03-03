<#
.SYNOPSIS
    Fetch and parse the latest KeeperFX crash dump from the PS Vita.

.DESCRIPTION
    Lists .psp2dmp files on the Vita FTP, downloads the latest (or a chosen one),
    and runs vita-parse-core via Docker (keeperfx-build-vitasdk image) to produce
    a readable stack trace.

    vita-parse-core and its Python venv are persisted in a Docker volume
    (keeperfx-vita-tools) so setup only happens once.

.PARAMETER VitaFTP
    Vita FTP address. Default: 192.168.0.66:1337

.PARAMETER All
    Show all dumps, not just eboot.bin ones.

.EXAMPLE
    .\tools\vita-parse-crash.ps1
    .\tools\vita-parse-crash.ps1 -VitaFTP 192.168.0.100:1337
#>
param(
    [string]$VitaFTP = "192.168.0.66:1337",
    [switch]$All
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path $PSScriptRoot -Parent

# Docker image — same one used for CI vita builds
$DOCKER_ORG = if ($env:DOCKER_ORG) { $env:DOCKER_ORG } else { "cerwym" }
$VITASDK_IMAGE = "ghcr.io/$DOCKER_ORG/keeperfx-build-vitasdk:latest"

# Docker volume: persists vita-parse-core venv across runs (no re-clone each time)
$TOOLS_VOLUME = "keeperfx-vita-tools"

# --- Resolve ELF: pick the most recently modified ELF across all Vita build dirs ---
# vita-debug has the most symbols; reldebug has good symbols; release is stripped.
# Newest mtime wins so we always symbolize against the build we actually deployed.
$ElfCandidates = @(
    "out/build/vita-debug/keeperfx",
    "out/build/vita-reldebug/keeperfx",
    "out/build/vita-release/keeperfx"
) | ForEach-Object {
    $full = Join-Path $RepoRoot $_
    if (Test-Path $full) { Get-Item $full }
} | Where-Object { $_ -ne $null } |
    Sort-Object LastWriteTime -Descending

$ElfFile = $null
if ($ElfCandidates) { $ElfFile = $ElfCandidates[0].FullName }
if (-not $ElfFile) {
    Write-Error "No Vita ELF found. Build vita-debug, vita-reldebug, or vita-release first."
    exit 1
}
Write-Host "Using ELF: $ElfFile  (modified $($ElfCandidates[0].LastWriteTime))" -ForegroundColor Cyan

# --- List dumps on Vita via Docker curl ---
Write-Host "Fetching dump list from ftp://$VitaFTP/ux0:/data/ ..." -ForegroundColor Cyan
$listing = docker run --rm $VITASDK_IMAGE bash -c "curl --disable-epsv -s 'ftp://$VitaFTP/ux0:/data/' 2>&1"
$pattern = if ($All) { "psp2dmp$" } else { "eboot\.bin\.psp2dmp$" }
$dumps = $listing | Select-String $pattern | ForEach-Object {
    $fields = ($_ -split '\s+')
    [PSCustomObject]@{
        Size     = [long]$fields[4]
        Date     = "$($fields[5]) $($fields[6]) $($fields[7])"
        Filename = $fields[8]
    }
}

if (-not $dumps) {
    Write-Error "No complete .psp2dmp files found on Vita (only .tmp incomplete ones may exist)."
    exit 1
}

# --- Pick latest ---
Write-Host "`nAvailable dumps:" -ForegroundColor Yellow
$i = 0
foreach ($d in $dumps) {
    Write-Host "  [$i] $($d.Filename)  ($($d.Size) bytes, $($d.Date))"
    $i++
}
Write-Host ""

$choice = Read-Host "Select dump number [default: $($dumps.Count - 1) = latest]"
if ($choice -eq "") { $choice = $dumps.Count - 1 }
$selected = $dumps[[int]$choice]
Write-Host "Selected: $($selected.Filename)" -ForegroundColor Green

# --- Download dump into out/vita-dumps/ via Docker curl ---
$DumpDir = Join-Path $RepoRoot "out\vita-dumps"
New-Item -ItemType Directory -Force -Path $DumpDir | Out-Null
$LocalDump = Join-Path $DumpDir $selected.Filename

Write-Host "Downloading..." -ForegroundColor Cyan
docker run --rm `
    -v "${DumpDir}:/dumps" `
    $VITASDK_IMAGE `
    bash -c "curl --disable-epsv -s 'ftp://$VitaFTP/ux0:/data/$($selected.Filename)' -o '/dumps/$($selected.Filename)'"
Write-Host "Saved to: $LocalDump" -ForegroundColor Green

# --- Ensure vita-parse-core + venv ready in Docker volume ---
Write-Host "`nChecking vita-parse-core tooling (volume: $TOOLS_VOLUME)..." -ForegroundColor Cyan
$setupCmd = @"
set -e
if [ ! -f /tools/venv/bin/activate ]; then
    python3 -m venv /tools/venv
    . /tools/venv/bin/activate
    pip install -q 'pyelftools==0.29'
else
    . /tools/venv/bin/activate
fi
if [ ! -f /tools/vita-parse-core/main.py ]; then
    git clone -q https://github.com/xyzz/vita-parse-core /tools/vita-parse-core
    sed -i 's/from elftools.common.py3compat import str2bytes/def str2bytes(s): return s.encode("utf-8") if isinstance(s, str) else s/' /tools/vita-parse-core/util.py
    sed -i 's/buf\[off\] != .\\0./buf[off] != 0/' /tools/vita-parse-core/util.py
    sed -i 's/out += buf\[off\]/out += chr(buf[off]) if isinstance(buf[off], int) else buf[off]/' /tools/vita-parse-core/util.py
fi
"@
docker run --rm -v "${TOOLS_VOLUME}:/tools" $VITASDK_IMAGE bash -c $setupCmd

# --- Run vita-parse-core ---
$ElfDir  = Split-Path $ElfFile -Parent
$ElfName = Split-Path $ElfFile -Leaf
$DumpName = $selected.Filename

Write-Host "`n=== vita-parse-core output ===" -ForegroundColor Yellow
$parseCmd = ". /tools/venv/bin/activate && export PATH=/usr/local/vitasdk/bin:`$PATH && python3 /tools/vita-parse-core/main.py '/dumps/$DumpName' '/elf/$ElfName' 2>&1"
$output = docker run --rm `
    -v "${TOOLS_VOLUME}:/tools" `
    -v "${DumpDir}:/dumps" `
    -v "${ElfDir}:/elf" `
    $VITASDK_IMAGE `
    bash -c $parseCmd
$output
Write-Host "==============================" -ForegroundColor Yellow

# --- Symbolicate stack: resolve keeperfx@1 offsets via addr2line ---
$offsets = $output |
    Select-String 'keeperfx@1 \+ (0x[0-9a-f]+)' -AllMatches |
    ForEach-Object { $_.Matches } |
    ForEach-Object { $_.Groups[1].Value } |
    Sort-Object -Unique

$symOutput = @()
if ($offsets) {
    Write-Host "`n=== Symbolicated call stack ===" -ForegroundColor Yellow

    $elfBase = 0x81000000
    $a2lLines = @("export PATH=/usr/local/vitasdk/bin:`$PATH")
    foreach ($off in $offsets) {
        $addr = "0x{0:x}" -f ($elfBase + [Convert]::ToInt64($off, 16))
        $a2lLines += "result=`$(arm-vita-eabi-addr2line -e '/elf/$ElfName' -fCa $addr 2>&1)"
        $a2lLines += "echo `"  keeperfx@1+$off  =>  `$result`""
    }
    $a2lCmd = $a2lLines -join "`n"

    $symOutput = docker run --rm `
        -v "${ElfDir}:/elf" `
        $VITASDK_IMAGE `
        bash -c $a2lCmd
    $symOutput
    Write-Host "================================" -ForegroundColor Yellow
}

# --- Save output next to where the script was invoked ---
$BaseName = [System.IO.Path]::GetFileNameWithoutExtension($selected.Filename)
$OutFile = Join-Path (Get-Location) "$BaseName.txt"
($output + @("", "=== Symbolicated call stack ===") + $symOutput) | Set-Content -Path $OutFile -Encoding UTF8
Write-Host "`nSaved: $OutFile" -ForegroundColor Green
