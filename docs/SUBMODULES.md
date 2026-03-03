# Git Submodules - Developer Guide

**For**: KeeperFX Modular Asset Architecture  
**Date**: February 11, 2026

---

## What Are Git Submodules?

Submodules allow you to keep git repositories inside other git repositories as subdirectories, while maintaining separate version control for each.

**In KeeperFX**:
- `keeperfx/` - Main code repository
- `keeperfx/gfx/` - FXGraphics submodule (tracks your fork on develop branch)
- `keeperfx/sfx/` - FXsounds submodule (tracks your fork on develop branch)
- `keeperfx/deps/kfx/` - kfx-deps submodule
- `keeperfx/custom/` - Your custom assets submodule

---

## Daily Workflow

### Starting Work

```powershell
cd C:\Users\peter\source\repos\keeperfx.worktrees\feature-casino-room

# Update all submodules to latest develop
git submodule update --remote --merge

# Or update specific submodule
cd gfx
git pull origin develop
cd ..
```

### Editing Assets

1. **Edit file** in submodule directory:
   ```powershell
   code gfx\menufx\gui2-64\room_64\casino_std.png
   ```

2. **Watch auto-compile** (if watcher running):
   - File saved → watcher detects → runs `docker compose -f docker/compose.yml run --rm linux bash -c "make pkg-gfx"` → outputs DAT files

3. **Commit changes** in submodule:
   ```powershell
   cd gfx
   git add menufx\gui2-64\room_64\casino_std.png
   git commit -m "Add casino room icon graphics"
   git push origin develop
   cd ..
   ```

4. **Update parent repo** to track new submodule commit:
   ```powershell
   git add gfx
   git commit -m "Update gfx submodule: casino icons"
   git push
   ```

---

## Common Operations

### Check Submodule Status

```powershell
# Show all submodules and their commits
git submodule status

# Expected output:
# +a1b2c3d gfx (heads/develop)
# +e4f5g6h sfx (heads/develop)
# +i7j8k9l deps/kfx (heads/develop)
# +m0n1o2p custom (heads/develop)
```

The `+` means submodule commit differs from parent repo's recorded commit.

### Pull Latest Changes

```powershell
# Update all submodules from their remotes
git submodule update --remote --merge

# Update just graphics
cd gfx && git pull origin develop && cd ..
```

### Create Feature Branch (in submodule)

```powershell
cd gfx
git checkout develop
git pull origin develop
git checkout -b feature/casino-textures
# ... make changes ...
git push -u origin feature/casino-textures
cd ..
```

### Merge Feature Branch

```powershell
cd gfx
git checkout develop
git merge feature/casino-textures
git push origin develop
# Optional: delete feature branch
git branch -d feature/casino-textures
git push origin --delete feature/casino-textures
cd ..
```

### Sync from Upstream (dkfans)

To pull changes from the original dkfans repositories:

```powershell
cd gfx
git fetch upstream
git checkout develop
git merge upstream/main  # or: git rebase upstream/main
git push origin develop
cd ..
```

**Important**: Your `develop` branch is independent. Only sync when you WANT upstream changes.

---

## Understanding the Dual Repository Model

Each submodule fork has TWO remotes:

### origin (Your Fork)
- URL: `https://github.com/Cerwym/FXGraphics.git`
- Branch: `develop` (your custom branch)
- Purpose: Your independent development
- Push: `git push origin develop`

### upstream (Original dkfans)
- URL: `https://github.com/dkfans/FXGraphics.git`
- Branch: `main` (official releases)
- Purpose: Sync official changes when desired
- Fetch: `git fetch upstream`

**Philosophy**: You control when to sync from upstream. No forced updates.

---

## Working with Custom Assets

The `custom/` submodule is entirely yours (no upstream).

### Adding New Assets

```powershell
cd custom

# Create directory structure
mkdir -p sprites\rooms\casino
mkdir -p textures\casino

# Add files (copy from artist, create new, etc.)
cp C:\Downloaded\casino_icon.png sprites\rooms\casino\

# Commit
git add sprites\rooms\casino\casino_icon.png
git commit -m "Add casino icon sprite"
git push origin develop
cd ..

# Update parent
git add custom
git commit -m "Update custom assets: casino icon"
git push
```

### Directory Structure

```
custom/
├── sprites/
│   ├── rooms/
│   │   └── casino/
│   │       ├── casino_icon_64.png
│   │       ├── casino_icon_32.png
│   │       └── casino_pointer_64.png
│   └── creatures/
│       └── custom_creature_sprite.png
└── textures/
    └── casino/
        ├── floor_variant01.png
        ├── floor_variant02.png
        └── wall_column.png
```

---

## Build System Integration

After editing assets in submodules, compile them:

```powershell
# Graphics (processes gfx/ and custom/)
docker compose -f docker/compose.yml run --rm linux bash -c "make pkg-gfx"

# Sounds (processes sfx/)
docker compose -f docker/compose.yml run --rm linux bash -c "make pkg-sfx"

# Localization (processes lang/)
docker compose -f docker/compose.yml run --rm linux bash -c "make pkg-lang"

# All assets
docker compose -f docker/compose.yml run --rm linux bash -c "make pkg-all"
```

**With file watcher**: Compilation happens automatically on save!

```powershell
# Start watcher
.\.vscode\watch_assets.ps1

# Edit gfx/menufx/test.png
# → Watcher detects change
# → Runs docker compose run linux bash -c "make pkg-gfx"
# → Outputs pkg/data/gui2-64.dat
```

---

## Troubleshooting

### "Submodule not initialized"

```powershell
git submodule update --init --recursive
```

### "Detached HEAD" State

Submodules often end up in detached HEAD state after certain operations.

**Fix**:
```powershell
cd gfx
git checkout develop
git pull origin develop
cd ..
```

### "Local changes would be overwritten"

Submodule has uncommitted changes.

**Fix**:
```powershell
cd gfx
git status  # See what changed
git add .
git commit -m "WIP: save changes"
git push origin develop
cd ..
```

### Parent Repo Shows Modified Submodule

This is normal! Parent repo tracks submodule commits.

**Fix**:
```powershell
# Commit the new submodule pointer
git add gfx
git commit -m "Update gfx submodule to latest develop"
git push
```

### Submodule Won't Update

```powershell
# Nuclear option: reset submodule
git submodule deinit -f gfx
git submodule update --init gfx
cd gfx
git checkout develop
cd ..
```

---

## Advanced: Submodule foreach

Execute commands across all submodules:

```powershell
# Pull all submodules
git submodule foreach git pull origin develop

# Check status of all
git submodule foreach git status

# Create branch in all
git submodule foreach 'git checkout -b feature/new-feature'
```

---

## Best Practices

### DO:
- ✓ Commit submodule changes before committing parent
- ✓ Push submodule commits before pushing parent
- ✓ Keep submodules on `develop` branch
- ✓ Use meaningful commit messages in submodules
- ✓ Sync from upstream deliberately (when you want updates)

### DON'T:
- ✗ Leave submodules in detached HEAD
- ✗ Forget to push submodule commits (parent will reference unpushed commits)
- ✗ Force-push to develop (breaks collaboration)
- ✗ Delete .gitmodules file
- ✗ Sync from upstream automatically (defeats independence)

---

## Collaboration Workflow

If multiple developers work on assets:

1. **Developer A** edits casino icon:
   ```powershell
   cd gfx
   # edit file
   git add menufx\gui2-64\room_64\casino_std.png
   git commit -m "Update casino icon"
   git push origin develop
   cd ..
   git add gfx
   git commit -m "Update gfx: casino icon"
   git push
   ```

2. **Developer B** pulls changes:
   ```powershell
   git pull
   git submodule update --remote --merge
   # Now has A's casino icon changes
   ```

---

## CI/CD Integration

For automated builds (GitHub Actions, etc.):

```yaml
# .github/workflows/build.yml
- name: Checkout with submodules
  uses: actions/checkout@v3
  with:
    submodules: recursive
    token: ${{ secrets.GITHUB_TOKEN }}

- name: Update submodules
  run: git submodule update --init --recursive

- name: Build assets
  run: |
    docker compose -f docker/compose.yml run --rm linux bash -c "make pkg-gfx && make pkg-sfx"
```

---

## Backup Strategy

Submodules are independent repos, so back them up separately:

```powershell
# Backup main repo
git clone https://github.com/Cerwym/keeperfx.git backup_keeperfx

# Backup submodules (automatically included with --recursive)
git clone --recursive https://github.com/Cerwym/keeperfx.git backup_full

# Or backup individual submodules
git clone https://github.com/Cerwym/FXGraphics.git backup_gfx
```

---

## Migration Back (If Needed)

If you ever need to revert to the old ephemeral clone system:

```powershell
# Remove submodules
git submodule deinit -f gfx sfx deps/kfx custom
git rm gfx sfx deps/kfx custom
rm -rf .git/modules/*

# Restore original pkg_*.mk files
cp pkg_gfx.mk.backup pkg_gfx.mk
cp pkg_sfx.mk.backup pkg_sfx.mk

# Commit
git commit -m "Revert to ephemeral asset clones"
```

---

## Resources

- [Official Git Submodules Guide](https://git-scm.com/book/en/v2/Git-Tools-Submodules)
- [Atlassian Submodules Tutorial](https://www.atlassian.com/git/tutorials/git-submodule)
- `.local/MODULAR_ASSET_ARCHITECTURE.md` - Full architecture design
- `.local/QUICK_START_MODULAR.md` - Quick setup guide

---

**Questions?** Check the troubleshooting section or see full architecture docs.
