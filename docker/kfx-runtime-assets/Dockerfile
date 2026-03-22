# LOCAL-ONLY IMAGE -- never pushed to any registry.
# Caches KeeperFX runtime assets used by local debug runs.
#
# This image is expected to be rebuilt locally after asset regeneration:
#   - make pkg-gfx
#   - make pkg-sfx
#   - make pkg-languages
#
# Built by tools/init-deploy.ps1 as keeperfx-runtime-assets:local.

FROM scratch

# Static runtime content from the repository
COPY config/keeperfx.cfg /kfx/keeperfx.cfg
COPY config/creatrs/ /kfx/creatrs/
COPY config/fxdata/ /kfx/fxdata/
COPY config/mods/ /kfx/mods/

# KeeperFX extended graphics packs (.zip) and fonts (font*.fon) are included above via config/fxdata/
COPY campgns/ /kfx/campgns/
COPY levels/ /kfx/levels/

# SDL runtime DLLs used by the MinGW Windows build
COPY sdl/for_final_package/SDL2.dll /kfx/SDL2.dll
COPY sdl/for_final_package/SDL2_image.dll /kfx/SDL2_image.dll
COPY sdl/for_final_package/SDL2_mixer.dll /kfx/SDL2_mixer.dll
COPY sdl/for_final_package/SDL2_net.dll /kfx/SDL2_net.dll

# Generated package assets (produced in Docker by pkg-* targets)
COPY pkg/data/ /kfx/data/
COPY pkg/ldata/ /kfx/ldata/
COPY pkg/sound/ /kfx/sound/
COPY pkg/campgns/ /kfx/campgns/
COPY pkg/levels/ /kfx/levels/
COPY pkg/fxdata/*.dat /kfx/fxdata/

