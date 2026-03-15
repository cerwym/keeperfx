param(
    [string]$WorkspaceFolder = "$PSScriptRoot\..",
    [string]$SonarHostUrl = "http://host.docker.internal:9000",
    [string]$SonarProjectKey = "keeperfx-local-vita-hardware-acceleration",
    [int]$PollSeconds = 20,
    [int]$DebounceSeconds = 45,
    [int]$MinScanIntervalSeconds = 180,
    [int]$CppcheckRefreshMinutes = 20,
    [string]$SonarToken = ""
)

$WorkspaceFolder = (Resolve-Path $WorkspaceFolder).Path
$tokenFile = Join-Path $WorkspaceFolder ".vscode/.sonar-token"

if ([string]::IsNullOrWhiteSpace($SonarToken)) {
    if (-not [string]::IsNullOrWhiteSpace($env:SONAR_TOKEN)) {
        $SonarToken = $env:SONAR_TOKEN
    } elseif (Test-Path $tokenFile) {
        $SonarToken = (Get-Content $tokenFile -ErrorAction SilentlyContinue | Select-Object -First 1).Trim()
    }
}

if ([string]::IsNullOrWhiteSpace($SonarToken)) {
    Write-Host "[auto-sonar] missing token. Set SONAR_TOKEN env var or create .vscode/.sonar-token" -ForegroundColor Yellow
    Write-Host "[auto-sonar] watcher not started." -ForegroundColor Yellow
    exit 1
}

function Get-WorkFingerprint {
    $status = git -C $WorkspaceFolder status --porcelain --untracked-files=no 2>$null
    if ($LASTEXITCODE -ne 0) {
        return ""
    }
    return ($status -join "`n")
}

function Test-CompileCommandsReady {
    $ccdb = Join-Path $WorkspaceFolder "out/build/linux-x64-release/compile_commands.json"
    if (Test-Path $ccdb) {
        return $true
    }

    Write-Host "[auto-sonar] compile_commands missing; generating..." -ForegroundColor Cyan
    $cmd = "docker compose -f '$WorkspaceFolder/docker/compose.yml' run --rm linux bash -c 'cmake --preset linux-x64-release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON'"
    Invoke-Expression $cmd | Out-Null
    return (Test-Path $ccdb)
}

function Update-CppcheckReport {
    Write-Host "[auto-sonar] refreshing cppcheck report..." -ForegroundColor Cyan
    $cmd = "docker compose -f '$WorkspaceFolder/docker/compose.yml' run --rm linux bash -c 'cmake --preset linux-x64-release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && mkdir -p out && cppcheck --project=out/build/linux-x64-release/compile_commands.json --xml --xml-version=2 --enable=warning,style,performance,portability,information --inconclusive --suppress=missingIncludeSystem -j 4 2>out/cppcheck.xml || true'"
    Invoke-Expression $cmd | Out-Null
}

function Invoke-SonarScan {
    Write-Host "[auto-sonar] running sonar scan..." -ForegroundColor Cyan
    $cmd = "docker compose -f '$WorkspaceFolder/docker/compose.yml' run --rm -e SONAR_HOST_URL='$SonarHostUrl' sonarscanner sh -c 'sonar-scanner -Dproject.settings=sonar-project.properties -Dsonar.projectKey=$SonarProjectKey -Dsonar.token=$SonarToken'"
    Invoke-Expression $cmd | Out-Null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[auto-sonar] scan complete." -ForegroundColor Green
    } else {
        Write-Host "[auto-sonar] scan failed (exit $LASTEXITCODE)." -ForegroundColor Red
    }
}

Write-Host "[auto-sonar] watching $WorkspaceFolder" -ForegroundColor Green
Write-Host "[auto-sonar] poll=${PollSeconds}s debounce=${DebounceSeconds}s minInterval=${MinScanIntervalSeconds}s" -ForegroundColor Green

$lastFingerprint = Get-WorkFingerprint
$dirtySince = $null
$lastScan = [DateTime]::MinValue
$lastCppcheck = [DateTime]::MinValue

while ($true) {
    $fp = Get-WorkFingerprint
    if ($fp -ne $lastFingerprint) {
        $lastFingerprint = $fp
        if (-not [string]::IsNullOrWhiteSpace($fp)) {
            $dirtySince = Get-Date
            Write-Host "[auto-sonar] changes detected; waiting for debounce..." -ForegroundColor DarkCyan
        } else {
            $dirtySince = $null
        }
    }

    if ($null -ne $dirtySince) {
        $now = Get-Date
        $debounced = ($now - $dirtySince).TotalSeconds -ge $DebounceSeconds
        $intervalOk = ($now - $lastScan).TotalSeconds -ge $MinScanIntervalSeconds
        if ($debounced -and $intervalOk) {
            if (Test-CompileCommandsReady) {
                if (((Get-Date) - $lastCppcheck).TotalMinutes -ge $CppcheckRefreshMinutes -or -not (Test-Path (Join-Path $WorkspaceFolder "out/cppcheck.xml"))) {
                    Update-CppcheckReport
                    $lastCppcheck = Get-Date
                }
                Invoke-SonarScan
                $lastScan = Get-Date
            } else {
                Write-Host "[auto-sonar] compile_commands still missing; skipping scan." -ForegroundColor Yellow
            }
            # Wait for next modification before scanning again.
            $dirtySince = $null
        }
    }

    Start-Sleep -Seconds $PollSeconds
}
