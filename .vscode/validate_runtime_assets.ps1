param(
    [Parameter(Mandatory=$true)]
    [string]$WorkspaceFolder,
    [string]$DeploySubdir = ".deploy"
)

$ErrorActionPreference = "Stop"

$workspace = (Resolve-Path $WorkspaceFolder).Path
$deployDir = Join-Path $workspace $DeploySubdir
$deployLevels = Join-Path $deployDir "levels"
$srcLevels = Join-Path $workspace "levels"
$reportPath = Join-Path $deployDir "runtime-asset-report.txt"

$missing = New-Object System.Collections.Generic.List[string]
$notes = New-Object System.Collections.Generic.List[string]

function Add-Missing {
    param([string]$Text)
    $missing.Add($Text) | Out-Null
}

function Add-Note {
    param([string]$Text)
    $notes.Add($Text) | Out-Null
}

function Get-ConfigValue {
    param(
        [string]$ConfigPath,
        [string]$Key
    )

    if (-not (Test-Path $ConfigPath)) {
        return $null
    }

    foreach ($line in Get-Content $ConfigPath) {
        if ($line -match "^\s*$Key\s*=\s*(.+)$") {
            $raw = $Matches[1]
            $raw = ($raw -split ";", 2)[0].Trim()
            $raw = ($raw -split "//", 2)[0].Trim()
            return $raw.Trim('"', "'")
        }
    }
    return $null
}

function Get-SourceMapIds {
    param([string]$PackName)

    $packSrcDir = Join-Path $srcLevels $PackName
    if (-not (Test-Path $packSrcDir)) {
        return @()
    }

    $ids = New-Object System.Collections.Generic.List[string]
    Get-ChildItem $packSrcDir -File -Filter "map*.txt" -ErrorAction SilentlyContinue | ForEach-Object {
        $m = [regex]::Match($_.BaseName, "^map(\d{5})$", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if ($m.Success) {
            $ids.Add($m.Groups[1].Value) | Out-Null
        }
    }
    return $ids.ToArray() | Sort-Object -Unique
}

function Get-MapNameFromScript {
    param([string]$ScriptPath)

    if (-not (Test-Path $ScriptPath)) {
        return $null
    }

    foreach ($line in Get-Content $ScriptPath) {
        if ($line -match '^\s*NAME_TEXT\s+"(.+)"') {
            return $Matches[1].Trim()
        }
    }
    return $null
}

function Ensure-PackMapAssets {
    param(
        [string]$PackName,
        [string]$PackDir,
        [string[]]$MapIds
    )

    $copied = 0
    foreach ($id in $MapIds) {
        $destDat = Join-Path $PackDir ("map{0}.dat" -f $id)
        if (Test-Path $destDat) {
            continue
        }

        $rootCandidates = @(
            ("map{0}.dat" -f $id), ("map{0}.clm" -f $id), ("map{0}.slb" -f $id), ("map{0}.tng" -f $id), ("map{0}.own" -f $id), ("map{0}.wlb" -f $id),
            ("MAP{0}.DAT" -f $id), ("MAP{0}.CLM" -f $id), ("MAP{0}.SLB" -f $id), ("MAP{0}.TNG" -f $id), ("MAP{0}.OWN" -f $id), ("MAP{0}.WLB" -f $id)
        )

        $haveAny = $false
        foreach ($candidate in $rootCandidates) {
            $sourcePath = Join-Path $deployLevels $candidate
            if (-not (Test-Path $sourcePath)) {
                continue
            }
            $ext = [System.IO.Path]::GetExtension($candidate).ToLowerInvariant()
            $destPath = Join-Path $PackDir (("map{0}{1}" -f $id, $ext))
            Copy-Item $sourcePath $destPath -Force
            $haveAny = $true
            $copied++
        }

        if (-not $haveAny) {
            Add-Missing ("Missing map binary for pack '{0}', map{1}: expected in DK/repo as map{1}.dat and related files" -f $PackName, $id)
        }
    }

    if ($copied -gt 0) {
        Add-Note ("Hydrated $copied map asset files into levels/$PackName from DK-level root files.")
    }
}

if (-not (Test-Path $deployDir)) {
    throw "Deploy directory not found: $deployDir"
}

foreach ($requiredDir in @("data", "sound", "levels", "fxdata", "campgns", "ldata")) {
    $path = Join-Path $deployDir $requiredDir
    if (-not (Test-Path $path)) {
        Add-Missing ("Missing runtime directory: $requiredDir")
    }
}

foreach ($requiredFile in @("keeperfx.cfg", "fxdata/gtext_eng.dat")) {
    $path = Join-Path $deployDir $requiredFile
    if (-not (Test-Path $path)) {
        Add-Missing ("Missing runtime file: $requiredFile")
    }
}

if (Test-Path $deployLevels) {
    foreach ($pack in @("classic", "standard", "lostlvls")) {
        $cfg = Join-Path $deployLevels ("{0}.cfg" -f $pack)
        if (-not (Test-Path $cfg)) {
            Add-Missing ("Missing map-pack config: levels/{0}.cfg" -f $pack)
            continue
        }

        $levelsLocation = Get-ConfigValue -ConfigPath $cfg -Key "LEVELS_LOCATION"
        if ([string]::IsNullOrWhiteSpace($levelsLocation)) {
            Add-Missing ("Missing LEVELS_LOCATION in levels/{0}.cfg" -f $pack)
            continue
        }

        $packDir = Join-Path $deployDir $levelsLocation
        if (-not (Test-Path $packDir)) {
            New-Item -ItemType Directory -Force -Path $packDir | Out-Null
            Add-Note ("Created missing levels location directory: {0}" -f $levelsLocation)
        }

        $sourceIds = Get-SourceMapIds -PackName $pack
        if ($sourceIds.Count -eq 0) {
            Add-Missing ("No source map scripts found for pack '{0}' under repository levels/{0}" -f $pack)
            continue
        }

        Ensure-PackMapAssets -PackName $pack -PackDir $packDir -MapIds $sourceIds

        $lifCount = (Get-ChildItem $packDir -File -Filter "*.lif" -ErrorAction SilentlyContinue).Count
        $lofCount = (Get-ChildItem $packDir -File -Filter "*.lof" -ErrorAction SilentlyContinue).Count

        if (($lifCount + $lofCount) -eq 0) {
            $lifPath = Join-Path $packDir ("{0}.lif" -f $pack)
            $entries = New-Object System.Collections.Generic.List[string]
            foreach ($id in $sourceIds) {
                $mapDat = Join-Path $packDir ("map{0}.dat" -f $id)
                if (-not (Test-Path $mapDat)) {
                    continue
                }
                $scriptPath = Join-Path (Join-Path $srcLevels $pack) ("map{0}.txt" -f $id)
                $name = Get-MapNameFromScript -ScriptPath $scriptPath
                if ([string]::IsNullOrWhiteSpace($name)) {
                    $name = "Map $id"
                }
                $entries.Add(("{0}, {1}" -f [int]$id, $name)) | Out-Null
            }

            if ($entries.Count -gt 0) {
                Set-Content -Path $lifPath -Value $entries -Encoding ASCII
                Add-Note ("Generated levels metadata file: {0}" -f $lifPath)
                $lifCount = 1
            }
        }

        if (($lifCount + $lofCount) -eq 0) {
            Add-Missing ("Pack '{0}' has no .lif/.lof metadata in {1}" -f $pack, $levelsLocation)
        }
    }
}

$report = New-Object System.Collections.Generic.List[string]
$report.Add(("Runtime asset preflight report ({0})" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))) | Out-Null
$report.Add(("Workspace: {0}" -f $workspace)) | Out-Null
$report.Add(("Deploy: {0}" -f $deployDir)) | Out-Null
$report.Add("") | Out-Null

if ($notes.Count -gt 0) {
    $report.Add("Applied fixes:") | Out-Null
    foreach ($line in $notes) {
        $report.Add(("- {0}" -f $line)) | Out-Null
    }
    $report.Add("") | Out-Null
}

if ($missing.Count -eq 0) {
    $report.Add("Status: OK") | Out-Null
    $report.Add("All required runtime assets for current checks are present.") | Out-Null
    Set-Content -Path $reportPath -Value $report -Encoding ASCII
    Write-Host "Runtime asset preflight passed." -ForegroundColor Green
    Write-Host "Report: $reportPath" -ForegroundColor DarkGray
    exit 0
}

$report.Add("Status: FAILED") | Out-Null
$report.Add("Missing assets:") | Out-Null
foreach ($line in $missing) {
    $report.Add(("- {0}" -f $line)) | Out-Null
}
Set-Content -Path $reportPath -Value $report -Encoding ASCII

Write-Host "Runtime asset preflight failed." -ForegroundColor Red
Write-Host "Report: $reportPath" -ForegroundColor Yellow
foreach ($line in $missing) {
    Write-Host (" - {0}" -f $line) -ForegroundColor Red
}
exit 2
