# LOCAL-ONLY IMAGE -- never pushed to any registry.
# Caches KeeperFX runtime assets used by local debug runs.
# Built by scripts/init-deploy.ps1 as keeperfx/runtime-assets:local.

FROM scratch

# Static runtime content from the repository
COPY config/keeperfx.cfg /kfx/keeperfx.cfg
COPY config/creatrs/ /kfx/creatrs/
COPY config/fxdata/ /kfx/fxdata/
COPY config/mods/ /kfx/mods/

# KeeperFX extended graphics packs (.zip) and fonts (font*.fon) are included above via config/fxdata/
COPY campgns/ /kfx/campgns/
COPY levels/ /kfx/levels/

# Generated package assets (produced in Docker by pkg-* targets)
COPY pkg/data/ /kfx/data/
COPY pkg/ldata/ /kfx/ldata/
COPY pkg/sound/ /kfx/sound/
COPY pkg/campgns/ /kfx/campgns/
COPY pkg/levels/ /kfx/levels/
COPY pkg/fxdata/*.dat /kfx/fxdata/

