<#
.SYNOPSIS
    Syncs the keeperfx fork with upstream and rebases the Vita feature branch.

.DESCRIPTION
    Run from anywhere on your Windows host. Performs:
      1. Fetches upstream (dkfans/keeperfx)
      2. Fast-forwards master to upstream/master
      3. Merges master into develop (your customizations branch)
      4. Rebases feature/vita-hardware-acceleration onto develop
      5. Pushes everything to origin (cerwym/keeperfx)

.PARAMETER MainRepo
    Path to the main keeperfx checkout. Default: C:\Users\peter\source\repos\keeperfx

.PARAMETER Worktree
    Path to the Vita worktree. Default: C:\Users\peter\source\repos\keeperfx.worktrees\vita-hardware-acceleration

.EXAMPLE
    .\sync-repos.ps1
    .\sync-repos.ps1 -MainRepo "D:\repos\keeperfx"
#>
param(
    [string]$MainRepo   = "C:\Users\peter\source\repos\keeperfx",
    [string]$Worktree   = "C:\Users\peter\source\repos\keeperfx.worktrees\vita-hardware-acceleration"
)

$ErrorActionPreference = 'Stop'

function Write-Step($msg) { Write-Host "`n>> $msg" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "   $msg"   -ForegroundColor Green }
function Write-Warn($msg) { Write-Host "   $msg"   -ForegroundColor Yellow }

# ── Validate paths ──────────────────────────────────────────────────────────
if (!(Test-Path $MainRepo)) { throw "Main repo not found: $MainRepo" }

# ── Work in the main repo ──────────────────────────────────────────────────
Push-Location $MainRepo
try {
    # Bail if working tree is dirty
    $dirty = git status --porcelain
    if ($dirty) {
        Write-Warn "Main repo has uncommitted changes — aborting."
        git status --short
        return
    }

    # ── 1. Fetch ────────────────────────────────────────────────────────────
    Write-Step "Fetching upstream + origin"
    git fetch upstream
    git fetch origin

    # ── 2. Update master ────────────────────────────────────────────────────
    Write-Step "Fast-forwarding master to upstream/master"
    git checkout master
    $mergeResult = git merge --ff-only upstream/master 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Warn "Cannot fast-forward master. You may have local commits on master."
        Write-Host $mergeResult
        return
    }
    Write-Ok "master is up to date with upstream."

    git push origin master
    Write-Ok "Pushed master to origin."

    # ── 3. Update develop ───────────────────────────────────────────────────
    Write-Step "Merging master into develop"
    git checkout develop
    git merge master --no-edit
    if ($LASTEXITCODE -ne 0) {
        Write-Warn "Merge conflict merging master into develop. Resolve manually."
        return
    }
    Write-Ok "develop merged with master."

    git push origin develop
    Write-Ok "Pushed develop to origin."

    # Return to master so the main checkout stays clean
    git checkout master

} finally {
    Pop-Location
}

# ── Work in the Vita worktree ───────────────────────────────────────────────
if (!(Test-Path $Worktree)) {
    Write-Warn "Vita worktree not found at $Worktree — skipping rebase."
    return
}

Push-Location $Worktree
try {
    $dirty = git status --porcelain
    if ($dirty) {
        Write-Warn "Vita worktree has uncommitted changes — stashing first."
        git stash push -m "sync-repos auto-stash"
        $stashed = $true
    }

    Write-Step "Rebasing feature/vita-hardware-acceleration onto develop"
    git fetch origin
    git rebase origin/develop
    if ($LASTEXITCODE -ne 0) {
        Write-Warn "Rebase conflict. Resolve with: git rebase --continue"
        return
    }
    Write-Ok "Vita branch rebased onto develop."

    git push origin feature/vita-hardware-acceleration --force-with-lease
    Write-Ok "Pushed vita branch (force-with-lease)."

    if ($stashed) {
        Write-Step "Restoring stashed changes"
        git stash pop
        Write-Ok "Stash restored."
    }

} finally {
    Pop-Location
}

Write-Host "`nAll synced." -ForegroundColor Green
