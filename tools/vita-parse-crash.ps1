<#
.SYNOPSIS
    Fetch and parse the latest KeeperFX crash dump from the PS Vita.

.DESCRIPTION
    Lists .psp2dmp files on the Vita FTP, downloads the latest (or a chosen one),
    and runs vita-parse-core via WSL to produce a readable stack trace.

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

# --- List dumps on Vita ---
Write-Host "Fetching dump list from ftp://$VitaFTP/ux0:/data/ ..." -ForegroundColor Cyan
$listing = wsl bash -c "curl --disable-epsv -s 'ftp://$VitaFTP/ux0:/data/' 2>&1"
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

# --- Download ---
$DumpDir = Join-Path $RepoRoot "out\vita-dumps"
New-Item -ItemType Directory -Force -Path $DumpDir | Out-Null
$LocalDump = Join-Path $DumpDir $selected.Filename

Write-Host "Downloading..." -ForegroundColor Cyan
$WslDumpDir = $DumpDir -replace '\\','/' -replace 'C:','/mnt/c'
wsl bash -c "curl --disable-epsv -s 'ftp://$VitaFTP/ux0:/data/$($selected.Filename)' -o '$WslDumpDir/$($selected.Filename)'"
Write-Host "Saved to: $LocalDump" -ForegroundColor Green

# --- Ensure WSL tooling is ready ---
$SetupScript = @'
set -e
# Persistent venv
if [ ! -f ~/venv-vita/bin/activate ]; then
    echo "Creating ~/venv-vita..."
    python3 -m venv ~/venv-vita
    source ~/venv-vita/bin/activate
    pip install -q 'pyelftools==0.29'
else
    source ~/venv-vita/bin/activate
fi

# vita-parse-core
if [ ! -f ~/vita-parse-core/main.py ]; then
    echo "Cloning vita-parse-core..."
    git clone -q https://github.com/xyzz/vita-parse-core ~/vita-parse-core
    # Python 3 compat patches
    sed -i 's/from elftools.common.py3compat import str2bytes/def str2bytes(s): return s.encode("utf-8") if isinstance(s, str) else s/' ~/vita-parse-core/util.py
    sed -i "s/buf\[off\] != '\\\\0'/buf[off] != 0/" ~/vita-parse-core/util.py
    sed -i "s/out += buf\[off\]/out += chr(buf[off]) if isinstance(buf[off], int) else buf[off]/" ~/vita-parse-core/util.py
fi
'@

Write-Host "`nChecking WSL tooling..." -ForegroundColor Cyan
# Write to temp file to avoid CRLF and $ expansion issues
$TmpSetup = Join-Path $RepoRoot "out\vita-dumps\_setup.sh"
[System.IO.File]::WriteAllText($TmpSetup, $SetupScript.Replace("`r`n", "`n"))
$WslSetup = $TmpSetup -replace '\\','/' -replace 'C:','/mnt/c'
wsl bash "$WslSetup"
Remove-Item $TmpSetup -ErrorAction SilentlyContinue

# --- Run vita-parse-core ---
$WslDump = $LocalDump -replace '\\','/' -replace 'C:','/mnt/c'
$WslElf  = $ElfFile   -replace '\\','/' -replace 'C:','/mnt/c'

$ParseScript = "#!/bin/bash`nsource ~/venv-vita/bin/activate`nexport PATH=/usr/local/vitasdk/bin:`$PATH`npython3 ~/vita-parse-core/main.py '$WslDump' '$WslElf' 2>&1`n"
$TmpParse = Join-Path $RepoRoot "out\vita-dumps\_parse.sh"
[System.IO.File]::WriteAllText($TmpParse, $ParseScript)
$WslParse = $TmpParse -replace '\\','/' -replace 'C:','/mnt/c'

Write-Host "`n=== vita-parse-core output ===" -ForegroundColor Yellow
$output = wsl bash "$WslParse"
$output
Write-Host "==============================" -ForegroundColor Yellow
Remove-Item $TmpParse -ErrorAction SilentlyContinue

# --- Symbolicate stack: resolve keeperfx@1 offsets via addr2line ---
# Extract all unique "keeperfx@1 + 0xXXXXXX" offsets from the output
$offsets = $output |
    Select-String 'keeperfx@1 \+ (0x[0-9a-f]+)' -AllMatches |
    ForEach-Object { $_.Matches } |
    ForEach-Object { $_.Groups[1].Value } |
    Sort-Object -Unique

if ($offsets) {
    Write-Host "`n=== Symbolicated call stack ===" -ForegroundColor Yellow

    # Build addr2line bash script — compute hex addresses in PowerShell
    $elfBase = 0x81000000
    $lines = @("#!/bin/bash", "export PATH=/usr/local/vitasdk/bin:`$PATH")
    foreach ($off in $offsets) {
        $addr = "0x{0:x}" -f ($elfBase + [Convert]::ToInt64($off, 16))
        $lines += "result=`$(arm-vita-eabi-addr2line -e '$WslElf' -fCa $addr 2>&1)"
        $lines += "echo `"  keeperfx@1+$off  =>  `$result`""
    }
    $a2lScript = $lines -join "`n"
    $TmpA2l = Join-Path $RepoRoot "out\vita-dumps\_a2l.sh"
    [System.IO.File]::WriteAllText($TmpA2l, $a2lScript)
    $WslA2l = $TmpA2l -replace '\\','/' -replace 'C:','/mnt/c'
    $symOutput = wsl bash "$WslA2l"
    $symOutput
    Remove-Item $TmpA2l -ErrorAction SilentlyContinue
    Write-Host "================================" -ForegroundColor Yellow
}

# --- Save output next to where the script was invoked ---
$BaseName = [System.IO.Path]::GetFileNameWithoutExtension($selected.Filename)
$OutFile = Join-Path (Get-Location) "$BaseName.txt"
($output + @("", "=== Symbolicated call stack ===") + $symOutput) | Set-Content -Path $OutFile -Encoding UTF8
Write-Host "`nSaved: $OutFile" -ForegroundColor Green
