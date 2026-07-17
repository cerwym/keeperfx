# Fork Workflow Documentation

This document describes the branch structure and workflow for maintaining the enhanced KeeperFX fork while contributing features back to upstream.

## Branch Structure

```
Cerwym/keeperfx (your fork)
├── dev-environment ─────► DEFAULT BRANCH - Enhanced fork base (modular assets, tooling)
│   └── Infrastructure + all development happens here
├── master ──────────────► 1:1 sync with dkfans/keeperfx:master (SYNC ONLY, never commit)
│   └── Pure upstream mirror - only pull from upstream, push to origin for mirroring
└── feature/* ───────────► Feature branches (based on dev-environment)
    └── Pure feature code (rebased onto dev-environment)

dkfans/keeperfx (upstream)
└── master ──────────────► Receives clean PRs (features only, no infrastructure)
```

**Key Principle:** `dev-environment` is YOUR truth (default), `master` is THEIR truth (sync point).

## Key Principles

1. **dev-environment is your default**: All development starts here - it's the default branch on GitHub
2. **master is sync-only**: Configured to track `upstream` remote only - NEVER commit directly to it
3. **Infrastructure stays in fork**: Modular asset architecture, layered deployment, and development tooling live on `dev-environment`
4. **Features are portable**: Feature branches contain only code relevant to upstream
5. **PRs are clean**: Cherry-pick feature commits onto `upstream/master` for submission
6. **Development is enhanced**: Work in feature branches inherits all fork infrastructure

### Git Configuration

The repository is configured with these branch tracking settings:
- `master` → tracks `upstream/master` (dkfans/keeperfx) - sync only
- `dev-environment` → tracks `origin/dev-environment` (Cerwym/keeperfx) - default & development base
- `feature/*` → track `origin/feature/*` (Cerwym/keeperfx) - working branches

To verify: `git config --get branch.master.remote` should return `upstream`

## Daily Workflow

### Starting a New Feature

```powershell
# In main workspace (not worktree)
cd C:\Users\peter\source\repos\keeperfx

# Ensure dev-environment is up to date (it's the default branch)
git checkout dev-environment  # Already default, but explicit is clear
git pull origin dev-environment

# Create feature branch (automatically based on current branch = dev-environment)
git checkout -b feature/new-thing
git push -u origin feature/new-thing

# Create worktree
git worktree add ..\keeperfx.worktrees\feature-new-thing feature/new-thing
cd ..\keeperfx.worktrees\feature-new-thing

# Initialize deployment
.\.vscode\init_layered_deploy.ps1

# Start developing with full infrastructure!
```

**Note:** Since `dev-environment` is now the default branch, new clones automatically get the enhanced infrastructure.

### Developing a Feature

```powershell
# In feature worktree
cd C:\Users\peter\source\repos\keeperfx.worktrees\feature-new-thing

# Edit code in src/
# Compile: wsl make
# Deploy: .\.vscode\deploy_assets.ps1 -All
# Test: .\.vscode\launch_deploy.ps1

# Commit feature changes
git add src/new_thing.c src/new_thing.h
git commit -m "feat: Implement new thing"
git push origin feature/new-thing
```

### Preparing a PR for Upstream

When your feature is ready to submit to `dkfans/keeperfx`:

```powershell
# In main workspace
cd C:\Users\peter\source\repos\keeperfx

# Fetch latest upstream
git fetch upstream
git checkout master
git merge --ff-only upstream/master
git push origin master  # Keep fork's master synced

# Create PR branch from upstream
git checkout -b pr/new-thing upstream/master

# Cherry-pick ONLY feature commits (exclude infrastructure)
git log --oneline dev-environment..feature/new-thing
# Review the list, identify feature commits to keep

git cherry-pick <commit1> <commit2> <commit3>  # Feature commits only

# Push PR branch
git push origin pr/new-thing
```

Then create PR: `Cerwym/keeperfx:pr/new-thing` → `dkfans/keeperfx:master`

**Critical**: The PR branch contains ZERO infrastructure changes (.gitmodules, .vscode/, pkg_gfx.mk modifications, etc.)

### Syncing Upstream Changes

Periodically pull upstre (master tracks upstream ONLY - never commit to it)
git checkout master
git pull upstream master    # Gets dkfans/keeperfx changes
git push origin master      # Mirrors to your fork (optional)

# Merge into dev-environment (review carefully!)
git checkout dev-environment
git merge master
# Resolve conflicts if infrastructure conflicts with upstream changes
git push origin dev-environment

# Update feature branches
git checkout feature/my-thing
git rebase dev-environment
git push --force-with-lease origin feature/my-thing
```

**Important:** `master` is configured to track `upstream` remote only. Never make commits directly to `master` - it exists purely as a sync point with dkfans/keeperfx. checkout feature/my-thing
git rebase dev-environment
git push --force-with-lease origin feature/my-thing
```

## Asset Submodule Workflow

Asset repositories (gfx, sfx, deps/kfx, custom) track `develop` branches independently:

```powershell
# Update asset submodule
cd gfx
git checkout develop
git pull origin develop

# Make changes
# Edit PNGs, commit, push

git add some-sprite.png
git commit -m "Update sprite for casino feature"
git push origin develop

# Update parent repo to track new commit
cd ..
git add gfx
git commit -m "Update gfx submodule (casino sprites)"
```

**In PRs**: Don't include submodule pointer updates unless coordinating with upstream asset maintainers. They may want assets in a separate PR.

## Layered Deployment System

The `.deploy/` directory provides isolated testing without polluting the GOG install:

### Initial Setup

```powershell
# Set up clean master location
.\.vscode\setup_clean_master.ps1 -SourcePath "C:\temp"

# Create layered deployment
.\.vscode\init_layered_deploy.ps1
```

### Daily Usage

```powershell
# Build
wsl make

# Deploy compiled assets
.\.vscode\deploy_assets.ps1 -All

# Run
.\.vscode\launch_deploy.ps1
```

### Management

```powershell
# Reset deployment (preserves clean master)
.\.vscode\reset_layered_deploy.ps1

# Recreate deployment
.\.vscode\init_layered_deploy.ps1
```

## Key Files

### Fork Infrastructure (dev-environment branch only)

- `.gitmodules` – Asset submodule definitions
- `pkg_gfx.mk` – Modified to use submodules
- `pkg_sfx.mk` – Modified to use submodules
- `.vscode/init_layered_deploy.ps1` – Deployment setup
- `.vscode/deploy_assets.ps1` – Asset deployment
- `.vscode/watch_assets.ps1` – Auto-compilation
- `.vscode/setup_clean_master.ps1` – Clean master management
- `docs/SUBMODULES.md` – Submodule guide
- `.local/` – Development documentation

### Feature Code (included in PRs)

- `src/*.c`, `src/*.h` – Feature implementation
- `config/fxdata/*.cfg` – Configuration additions
- `Makefile` – Build entries forfeature files
- Feature-specific documentation in `docs/`

## Common Scenarios

### Scenario: Upstream accepts my PR

```powershell
# Your PR was merged to dkfans/keeperfx:master
git checkout master
git pull upstream master
git push origin master

# Optional: Merge into dev-environment to get your changes back
git checkout dev-environment
git merge master
# Your feature is now in both upstream and your dev base
```

### Scenario: Upstream rejects infrastructure

*No problem!* Infrastructure lives on `dev-environment` and never needs to go upstream. Continue using enhanced workflow for all your features.

### Scenario: Upstream wants infrastructure too

```powershell
# Create PR from dev-environment
git checkout -b pr/modular-assets dev-environment

# Rebase onto upstream to minimize conflicts
git rebase upstream/master

# May need to resolve conflicts with upstream build system
git push origin pr/modular-assets

# Create PR: Cerwym/keeperfx:pr/modular-assets → dkfans/keeperfx:master
```

### Scenario: Feature needs upstream fix

```powershell
# Get upstream fix
git checkout master
git pull upstream master

# Merge into dev-environment
git checkout dev-environment
git merge master

# Rebase feature branch
git checkout feature/my-thing
git rebase dev-environment
```

## Verification Checklist

Before submitting a PR:

- [ ] PR branch created from `upstream/master` (not dev-environment)
- [ ] Only feature commits cherry-picked (no infrastructure)
- [ ] No `.gitmodules` changes
- [ ] No `.vscode/` script changes
- [ ] No `pkg_gfx.mk` / `pkg_sfx.mk` infrastructure modifications
- [ ] Feature compiles without submodules (test in clean clone)
- [ ] Commit messages follow upstream conventions
- [ ] Documentation updated for feature (if needed)
- [ ] No `.deploy/` references
- [ ] No workflow file changes

## Benefits of This Workflow

### For Development
- **Fast iteration**: Submodules + auto-compilation + layered deployment = 10-second edit-test cycle
- **Safe testing**: `.deploy/` isolates experiments from GOG install
- **Full version control**: Asset changes tracked in git with proper history
- **Independent branches**: Work on multiple features simultaneously in worktrees

### For Contributing Upstream
- **Clean PRs**: Zero fork-specific cruft in submitted code
- **Easy maintenance**: Upstream changes merge cleanly into master, optional for dev-environment
- **Flexible timing**: Submit features immediately, defer infrastructure discussion
- **Reduced friction**: Maintainers see only relevant feature code

### For Long-term Maintenance
- **Fork remains useful**: Enhanced workflow persists even if upstream diverges
- **Selective sync**: Cherry-pick upstream improvements without breaking custom tooling
- **Clear separation**: Infrastructure decisions independent from feature contributions
- **Documented strategy**: New contributors understand the structure

## Troubleshooting

### "fatal: refusing to fetch into branch checked out at..."
You're trying to modify a branch that's checked out in a worktree. Work in the worktree itself or use detached HEAD.

### Submodule shows as "dirty" in git status
Changes exist in submodule working directory. Commit them in the submodule, then update parent pointer.

### Deployment shows wrong files after build
Run `.\.vscode\deploy_assets.ps1 -All` to copy new binaries over junctions/hard links.

### Can't find clean master
Run `.\.vscode\setup_clean_master.ps1` to configure the path.

### Merge conflicts when syncing upstream
Infrastructure on dev-environment may conflict with upstream changes. Resolve in favor of your infrastructure (you're not submitting it).

## See Also

- [docs/SUBMODULES.md](../docs/SUBMODULES.md) – Detailed submodule operations
- [.local/MODULAR_ASSET_ARCHITECTURE.md](MODULAR_ASSET_ARCHITECTURE.md) – Infrastructure design
- [.local/IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md) – Current implementation state
- [.local/QUICK_START_MODULAR.md](QUICK_START_MODULAR.md) – Quick start guide

---

**Remember**: The fork serves YOU. Infrastructure enables fast development. Features benefit EVERYONE. Keep them separate, profit from both.
