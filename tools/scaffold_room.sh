#!/bin/bash
# KeeperFX Room Configuration Scaffolder
#
# Generates boilerplate configuration for new rooms
# Usage: ./tools/scaffold_room.sh ROOM_NAME SLAB_NAME LARGE_SPRITE_IDX SMALL_SPRITE_IDX

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

# Check arguments
if [ $# -ne 4 ]; then
    echo -e "${RED}Usage: $0 ROOM_NAME SLAB_NAME LARGE_SPRITE_IDX SMALL_SPRITE_IDX${RESET}"
    echo ""
    echo "Example:"
    echo "  $0 CASINO CASINO_AREA 57 87"
    echo ""
    echo "This will generate configuration templates for:"
    echo "  - terrain.cfg [slab] and [room] sections"
    echo "  - room_data.h enum entry"
    echo "  - Checklist of remaining manual steps"
    exit 1
fi

ROOM_NAME=$1
SLAB_NAME=$2
LARGE_IDX=$3
SMALL_IDX=$4

# Uppercase for constants
ROOM_UPPER=$(echo "$ROOM_NAME" | tr '[:lower:]' '[:upper:]')
SLAB_UPPER=$(echo "$SLAB_NAME" | tr '[:lower:]' '[:upper:]')

# Lowercase for code
ROOM_LOWER=$(echo "$ROOM_NAME" | tr '[:upper:]' '[:lower:]')

echo -e "${BOLD}${BLUE}════════════════════════════════════════════════════════${RESET}"
echo -e "${BOLD}${BLUE}  KeeperFX Room Configuration Scaffolder${RESET}"
echo -e "${BOLD}${BLUE}════════════════════════════════════════════════════════${RESET}"
echo ""
echo -e "${CYAN}Room:${RESET} $ROOM_UPPER"
echo -e "${CYAN}Slab:${RESET} $SLAB_UPPER"
echo -e "${CYAN}Sprites:${RESET} Large=$LARGE_IDX, Small=$SMALL_IDX"
echo ""

# Generate slab configuration
echo -e "${GREEN}Generated Slab Configuration (add to terrain.cfg):${RESET}"
echo -e "${YELLOW}─────────────────────────────────────────────────────────${RESET}"
cat <<EOF

[slabNN]  ; Replace NN with next available slab number
Name = ${SLAB_UPPER}_AREA
TooltipTextID = XXX  ; Add to guitext.po
BlockFlagsHeight = 1
BlockHealthIndex = 5
BlockFlags = IS_ROOM
NoBlockFlags = BLOCKING
FillStyle = 0
Category = 2
SlbID = XX  ; Replace with next room ID
Indestructible = 0
Wibble = 0
Animated = 0
IsSafeLand = 1
IsDiggable = 0
IsOwnable = 1
WlbType = 0
GoldHeld = 0

[slabNN+1]  ; Wall slab
Name = ${SLAB_UPPER}_WALL
TooltipTextID = XXX
BlockFlagsHeight = 4
BlockHealthIndex = 3
BlockFlags = BLOCKING FILLED IS_ROOM
NoBlockFlags =
FillStyle = 0
Category = 4
SlbID = XX  ; Same as area
Indestructible = 0
Wibble = 0
Animated = 0
IsSafeLand = 0
IsDiggable = 0
IsOwnable = 1
WlbType = 0
GoldHeld = 0
EOF

echo -e "${YELLOW}─────────────────────────────────────────────────────────${RESET}"
echo ""

# Generate room configuration
echo -e "${GREEN}Generated Room Configuration (add to terrain.cfg):${RESET}"
echo -e "${YELLOW}─────────────────────────────────────────────────────────${RESET}"
cat <<EOF

[roomNN]  ; Replace NN with next available room number
Name = $ROOM_UPPER
NameTextID = XXX  ; Add to guitext.po (e.g., "GUI/200")
TooltipTextID = XXX  ; Add to guitext.po (e.g., "GUI/203")
Cost = 200  ; TODO: Balance cost
Health = 1000  ; TODO: Balance health
SlabAssign = ${SLAB_UPPER}_AREA
Roles = ROOM_ROLE_CUSTOM  ; TODO: Define actual role
Properties = HAS_NO_ENSIGN  ; TODO: Adjust properties
Messages = 0 0 0  ; TODO: Add message IDs if needed
AmbientSndSample = 0  ; TODO: Add ambient sound sample ID
TotalCapacity = slabs_all_only  ; TODO: Choose capacity function
UsedCapacity = none none  ; TODO: Define usage tracking
SlabSynergy = none
StorageHeight = 1
SymbolSprites = $LARGE_IDX $SMALL_IDX
PointerSprites = 31  ; TODO: Verify pointer sprite index
PanelTabIndex = 15  ; TODO: Choose panel position (1-16 first page, 17-32 second)
EOF

echo -e "${YELLOW}─────────────────────────────────────────────────────────${RESET}"
echo ""

# Generate enum entry
echo -e "${GREEN}Generated Enum Entry (add to room_data.h):${RESET}"
echo -e "${YELLOW}─────────────────────────────────────────────────────────${RESET}"
cat <<EOF

enum RoomKinds {
    // ... existing rooms ...
    RoK_${ROOM_UPPER} = XX,  // Replace XX with next room number
    RoK_TYPES_COUNT = XX+1,  // Update count
};
EOF

echo -e "${YELLOW}─────────────────────────────────────────────────────────${RESET}"
echo ""

# Generate checklist
echo -e "${BOLD}${CYAN}Implementation Checklist:${RESET}"
echo ""
echo -e "${YELLOW}[ ]${RESET} 1. Add slab sections to config/fxdata/terrain.cfg"
echo -e "${YELLOW}[ ]${RESET} 2. Add room section to config/fxdata/terrain.cfg"
echo -e "${YELLOW}[ ]${RESET} 3. Update RoomKinds enum in src/room_data.h"
echo -e "${YELLOW}[ ]${RESET} 4. Create room implementation files:"
echo "       - src/room_${ROOM_LOWER}.c"
echo "       - src/room_${ROOM_LOWER}.h"
echo -e "${YELLOW}[ ]${RESET} 5. Add localization strings to lang/*.po files:"
echo "       - Room name (NameTextID)"
echo "       - Tooltip (TooltipTextID)"
echo "       - Messages (if any)"
echo -e "${YELLOW}[ ]${RESET} 6. Create/verify sprite files exist:"
echo "       - gfx/menufx/gui2-64/room_64/${ROOM_LOWER}_std.png (50×64)"
echo "       - gfx/menufx/gui2-64/room_64/${ROOM_LOWER}_dis.png (50×64)"
echo "       - gfx/menufx/gui2-64/room_32/${ROOM_LOWER}_std.png (38×46)"
echo "       - gfx/menufx/gui2-64/room_32/${ROOM_LOWER}_dis.png (38×46)"
echo "       - gfx/menufx/gui2-32/room_64/${ROOM_LOWER}_std.png (25×32)"
echo "       - gfx/menufx/gui2-32/room_32/${ROOM_LOWER}_std.png (19×23)"
echo -e "${YELLOW}[ ]${RESET} 7. Add sprites to filelists:"
echo "       - gfx/menufx/gui2-64/filelist_gui2.txt (verify indices)"
echo "       - gfx/menufx/gui2-32/filelist_gui2.txt (verify indices)"
echo -e "${YELLOW}[ ]${RESET} 8. Rebuild sprite packages:"
echo "       make pkg/data/gui2-64.dat pkg/data/gui2-32.dat -B"
echo -e "${YELLOW}[ ]${RESET} 9. Validate configuration:"
echo "       python3 tools/validate_sprite_config.py --verbose"
echo -e "${YELLOW}[ ]${RESET} 10. Implement room logic functions:"
echo "       - create_${ROOM_LOWER}()"
echo "       - update_${ROOM_LOWER}()"
echo "       - process_${ROOM_LOWER}_function()"
echo -e "${YELLOW}[ ]${RESET} 11. Add to Makefile sources if new files created"
echo -e "${YELLOW}[ ]${RESET} 12. Test in-game and iterate"
echo ""
echo -e "${BOLD}${GREEN}✓ Configuration templates generated!${RESET}"
echo -e "${CYAN}Note: Replace placeholders (NN, XX, XXX) with actual values${RESET}"
