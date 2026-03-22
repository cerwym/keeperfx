<#
.SYNOPSIS
    One-time setup: installs git hooks and configures the upstream remote.

.DESCRIPTION
    - Points core.hooksPath to .githooks/ (checked into repo, shared across worktrees)
    - Adds the upstream remote if missing
    - Sets master to track upstream/master

.PARAMETER Repo
    Path to the main keeperfx checkout.
#>
param(
    [string]$Repo = "C:\Users\peter\source\repos\keeperfx"
)

$ErrorActionPreference = 'Stop'

function Write-Step($msg) { Write-Host "`n>> $msg" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "   $msg"   -ForegroundColor Green }

Push-Location $Repo
try {
    # ── Git hooks ───────────────────────────────────────────────────────
    Write-Step "Setting core.hooksPath to .githooks/"
    git config core.hooksPath .githooks
    Write-Ok "Hooks installed. They apply to all branches in this repo and its worktrees."

    # ── Upstream remote ─────────────────────────────────────────────────
    $remotes = git remote
    if ($remotes -notcontains 'upstream') {
        Write-Step "Adding upstream remote"
        git remote add upstream https://github.com/dkfans/keeperfx.git
        Write-Ok "upstream -> https://github.com/dkfans/keeperfx.git"
    } else {
        Write-Ok "upstream remote already configured."
    }

    git fetch upstream

    # ── Master tracking ─────────────────────────────────────────────────
    Write-Step "Ensuring master tracks upstream/master"
    git branch --set-upstream-to=upstream/master master
    Write-Ok "master -> upstream/master"

    Write-Host "`nSetup complete." -ForegroundColor Green
    Write-Host ""
    Write-Host "  Hooks active:" -ForegroundColor White
    Write-Host "    pre-commit    - blocks commits to master"
    Write-Host "    pre-push      - blocks ALL pushes to dkfans/keeperfx (unless ALLOW_DKFANS_PUSH=1)"
    Write-Host "    post-checkout - reminds you when on master"
    Write-Host ""
    Write-Host "  Scripts:" -ForegroundColor White
    Write-Host "    tools\sync-repos.ps1   - sync upstream -> master -> develop -> rebase vita"
    Write-Host "    tools\upstream-pr.ps1  - cherry-pick commits into clean upstream PR branch"
    Write-Host ""

} finally {
    Pop-Location
}
