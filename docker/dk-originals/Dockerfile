# LOCAL-ONLY IMAGE — never pushed to any registry.
# Caches the 16 original Dungeon Keeper files required by KeeperFX.
# Built once per machine by .vscode/init_dk_layer.ps1.
#
# Build command (run by init_dk_layer.ps1):
#   docker build \
#     --build-context dk=<path-to-DK-install> \
#     -f docker/dk-originals/Dockerfile \
#     -t keeperfx-dk-originals:local \
#     .
#
# Files sourced from docs/files_required_from_original_dk.txt

FROM scratch

# 14 palette/map files from the original DK data/ directory
COPY --from=dk data/bluepal.dat   /dk/data/bluepal.dat
COPY --from=dk data/bluepall.dat  /dk/data/bluepall.dat
COPY --from=dk data/ceiling.txt   /dk/data/ceiling.txt
COPY --from=dk data/dogpal.pal    /dk/data/dogpal.pal
COPY --from=dk data/hitpall.dat   /dk/data/hitpall.dat
COPY --from=dk data/lightng.pal   /dk/data/lightng.pal
COPY --from=dk data/main.pal      /dk/data/main.pal
COPY --from=dk data/mapfadeg.dat  /dk/data/mapfadeg.dat
COPY --from=dk data/redpal.col    /dk/data/redpal.col
COPY --from=dk data/redpall.dat   /dk/data/redpall.dat
COPY --from=dk data/slab0-0.dat   /dk/data/slab0-0.dat
COPY --from=dk data/slab0-1.dat   /dk/data/slab0-1.dat
COPY --from=dk data/vampal.pal    /dk/data/vampal.pal
COPY --from=dk data/whitepal.col  /dk/data/whitepal.col

# UI/frontend/door/flag sprites, videos, and other assets from ldata/
COPY --from=dk ldata/ /dk/ldata/

# Redbook CD music — Steam stores these in the install root, not a music/ subdir
COPY --from=dk keeper02.ogg /dk/music/keeper02.ogg
COPY --from=dk keeper03.ogg /dk/music/keeper03.ogg
COPY --from=dk keeper04.ogg /dk/music/keeper04.ogg
COPY --from=dk keeper05.ogg /dk/music/keeper05.ogg
COPY --from=dk keeper06.ogg /dk/music/keeper06.ogg
COPY --from=dk keeper07.ogg /dk/music/keeper07.ogg

# 3 sound bank files from the original DK sound/ directory
COPY --from=dk sound/atmos1.sbk   /dk/sound/atmos1.sbk
COPY --from=dk sound/atmos2.sbk   /dk/sound/atmos2.sbk
COPY --from=dk sound/bullfrog.sbk /dk/sound/bullfrog.sbk
