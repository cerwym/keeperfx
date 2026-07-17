<#
.SYNOPSIS
    Create a clean branch off master for an upstream PR to dkfans/keeperfx.

.DESCRIPTION
    Cherry-picks specified commits from your feature branch onto a new branch
    based on upstream/master, so the PR contains only the relevant changes
    without your Docker/devcontainer infrastructure.

.PARAMETER Name
    Name for the upstream branch (e.g. "vita-spritesheet-fix").
    Will be prefixed with "upstream/".

.PARAMETER Commits
    One or more commit hashes to cherry-pick. Accepts short or full hashes.
    Applied in the order given.

.PARAMETER Range
    A commit range (e.g. "abc123..def456") to cherry-pick instead of
    individual commits.

.PARAMETER DryRun
    Show what would be done without making changes.

.PARAMETER Repo
    Path to the main keeperfx checkout. Default: C:\Users\peter\source\repos\keeperfx

.EXAMPLE
    # Cherry-pick specific commits
    .\upstream-pr.ps1 -Name "vita-crash-tool" -Commits abc1234, def5678

    # Cherry-pick a range
    .\upstream-pr.ps1 -Name "vita-crash-tool" -Range "develop~3..develop"

    # See what the vita branch changed vs develop, then pick
    git log --oneline develop..feature/vita-hardware-acceleration
    .\upstream-pr.ps1 -Name "vita-rendering" -Commits abc1234
#>
param(
    [Parameter(Mandatory)][string]$Name,
    [string[]]$Commits,
    [string]$Range,
    [switch]$DryRun,
    [string]$Repo = "C:\Users\peter\source\repos\keeperfx"
)

$ErrorActionPreference = 'Stop'

if (!$Commits -and !$Range) {
    Write-Host "ERROR: Specify -Commits or -Range." -ForegroundColor Red
    Write-Host ""
    Write-Host "  List your feature commits with:" -ForegroundColor Yellow
    Write-Host "    git log --oneline develop..feature/vita-hardware-acceleration"
    Write-Host ""
    Write-Host "  Then pick the ones relevant to upstream:" -ForegroundColor Yellow
    Write-Host "    .\upstream-pr.ps1 -Name 'my-fix' -Commits abc1234, def5678"
    exit 1
}

$branch = "upstream/$Name"

function Write-Step($msg) { Write-Host "`n>> $msg" -ForegroundColor Cyan }

Push-Location $Repo
try {
    # Ensure we have latest upstream
    Write-Step "Fetching upstream"
    if ($DryRun) { Write-Host "   [dry-run] git fetch upstream" }
    else { git fetch upstream }

    # Check branch doesn't already exist
    $exists = git branch --list $branch
    if ($exists) {
        Write-Host "ERROR: Branch '$branch' already exists. Delete it first or choose a different name." -ForegroundColor Red
        exit 1
    }

    # Create branch off upstream/master
    Write-Step "Creating $branch from upstream/master"
    if ($DryRun) { Write-Host "   [dry-run] git checkout -b $branch upstream/master" }
    else { git checkout -b $branch upstream/master }

    # Cherry-pick
    if ($Range) {
        Write-Step "Cherry-picking range: $Range"
        if ($DryRun) { Write-Host "   [dry-run] git cherry-pick $Range" }
        else {
            git cherry-pick $Range
            if ($LASTEXITCODE -ne 0) {
                Write-Host ""
                Write-Host "  Cherry-pick conflict. Resolve, then:" -ForegroundColor Yellow
                Write-Host "    git cherry-pick --continue" -ForegroundColor Yellow
                Write-Host "    git push origin $branch" -ForegroundColor Yellow
                return
            }
        }
    } else {
        Write-Step "Cherry-picking $($Commits.Count) commit(s)"
        foreach ($c in $Commits) {
            $c = $c.Trim()
            if ($DryRun) {
                Write-Host "   [dry-run] git cherry-pick $c"
            } else {
                Write-Host "   Picking $c ..."
                git cherry-pick $c
                if ($LASTEXITCODE -ne 0) {
                    Write-Host ""
                    Write-Host "  Cherry-pick conflict on $c. Resolve, then:" -ForegroundColor Yellow
                    Write-Host "    git cherry-pick --continue" -ForegroundColor Yellow
                    Write-Host "    git push origin $branch" -ForegroundColor Yellow
                    return
                }
            }
        }
    }

    # Push
    Write-Step "Pushing $branch to origin"
    if ($DryRun) { Write-Host "   [dry-run] git push origin $branch" }
    else { git push origin $branch }

    Write-Host ""
    Write-Host "  Done! Create your upstream PR:" -ForegroundColor Green
    Write-Host "  https://github.com/dkfans/keeperfx/compare/master...cerwym:keeperfx:$($branch)?expand=1" -ForegroundColor Green
    Write-Host ""

    # Return to previous branch
    git checkout -

} finally {
    Pop-Location
}
