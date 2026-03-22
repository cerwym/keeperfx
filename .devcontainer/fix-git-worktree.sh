#!/usr/bin/env bash
# fix-git-worktree.sh — Make git work inside devcontainers opened from worktrees
#
# Problem: when VS Code opens a git worktree in a devcontainer, the .git file
# contains a host-side path (e.g. C:/Users/.../keeperfx/.git/worktrees/...).
# If the Dev Containers extension fails to auto-mount the main .git directory,
# git commands inside the container fail with "not a git repository".
#
# Fix: detect the broken worktree pointer, extract the remote + branch from
# disk, and re-initialise a standalone repo that tracks the same remote/branch.
# This makes git commit/push/pull work inside the container.
#
# Usage: Called by postStartCommand in devcontainer.json
#   bash .devcontainer/fix-git-worktree.sh
set -euo pipefail

WORKSPACE="${1:-$(pwd)}"
GIT_FILE="$WORKSPACE/.git"

# --- Not a worktree? Just ensure filemode is off (Windows host mount noise) ---
if [[ -d "$GIT_FILE" ]]; then
    git -C "$WORKSPACE" config core.filemode false
    echo "✓ Git repository OK (not a worktree)."
    exit 0
fi

# --- Worktree pointer exists; check if it resolves ---
if [[ -f "$GIT_FILE" ]]; then
    TARGET="$(sed 's/^gitdir: //' "$GIT_FILE")"
    if [[ -d "$TARGET" ]]; then
        echo "✓ Git worktree OK (target exists)."
        exit 0
    fi
fi

# --- Broken worktree — try to recover remote + branch info ---
echo "Git worktree target unreachable: $TARGET"
echo "Re-initialising standalone git repo..."

# Stash the old .git file
mv "$GIT_FILE" "$GIT_FILE.worktree.bak"

cd "$WORKSPACE"
git init -b main --quiet

# Try to recover remote URL from .gitmodules, README, or common patterns
# Most reliable: check if GITHUB_REPOSITORY is set (e.g. in Codespaces)
REMOTE_URL=""
if [[ -n "${GITHUB_REPOSITORY:-}" ]]; then
    REMOTE_URL="https://github.com/$GITHUB_REPOSITORY.git"
fi

# Fallback: parse the worktree path for repo name hints
if [[ -z "$REMOTE_URL" ]]; then
    # Try to extract org/repo from the Windows path
    # e.g. C:/Users/peter/source/repos/keeperfx/.git/worktrees/...
    if echo "$TARGET" | grep -qP '[\\/]([^\\/]+)[\\/]\.git[\\/]'; then
        REPO_NAME="$(echo "$TARGET" | grep -oP '[\\/]\K([^\\/]+)(?=[\\/]\.git[\\/])')"
        if [[ -n "$REPO_NAME" ]]; then
            # Check .gitmodules or remote config for the org
            # Fall back to common GitHub patterns
            for ORG in cerwym keeperfx; do
                CANDIDATE="https://github.com/$ORG/$REPO_NAME.git"
                if git ls-remote --quiet "$CANDIDATE" HEAD &>/dev/null 2>&1; then
                    REMOTE_URL="$CANDIDATE"
                    break
                fi
            done
        fi
    fi
fi

if [[ -z "$REMOTE_URL" ]]; then
    echo "WARNING: Could not determine remote URL."
    echo "  Run: git remote add origin <url>"
    echo "  Then: git fetch && git checkout <branch>"
    exit 0
fi

echo "Remote: $REMOTE_URL"
git remote add origin "$REMOTE_URL"

# Try to determine the branch from the worktree backup
BRANCH=""
if [[ -f "$GIT_FILE.worktree.bak" ]]; then
    # The worktree name is typically the branch name
    WT_NAME="$(basename "$TARGET" 2>/dev/null || true)"
    if [[ -n "$WT_NAME" ]]; then
        BRANCH="$WT_NAME"
    fi
fi

# Disable filemode tracking — Windows bind-mounts set executable bits on
# everything, causing every file to appear modified in Linux git.
git -C "$WORKSPACE" config core.filemode false

# Fetch and set up tracking
echo "Fetching from origin..."
if [[ -n "$BRANCH" ]] && git fetch --quiet origin "$BRANCH" 2>/dev/null; then
    git checkout -B "$BRANCH" "origin/$BRANCH" --quiet 2>/dev/null || true
    echo "✓ Git ready — branch: $BRANCH (tracking origin/$BRANCH)"
elif git fetch --quiet origin main 2>/dev/null; then
    git checkout -B main origin/main --quiet 2>/dev/null || true
    echo "✓ Git ready — branch: main"
else
    git fetch --quiet origin 2>/dev/null || true
    echo "✓ Git initialised. Run: git checkout <branch>"
fi

echo "  Note: local-only repo (worktree data is on host)."
echo "  Commits and pushes work normally."
