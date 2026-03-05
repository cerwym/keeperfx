# KeeperFX Development Tools

This directory contains tools to assist with KeeperFX development, particularly for asset integration and configuration management.

## Asset Validation Tools

### `validate_sprite_config.py`

Validates sprite configuration consistency across the codebase.

**Purpose:**
- Prevents runtime crashes from invalid sprite indices
- Catches sprite package/filelist mismatches at build time
- Validates room configurations in `terrain.cfg`

**Usage:**
```bash
# Basic validation
python3 tools/validate_sprite_config.py

# Verbose output (shows all rooms)
python3 tools/validate_sprite_config.py --verbose

# Integrated with Make
make validate-sprites
make validate  # Runs all validators
```

**What it checks:**
1. All `SymbolSprites` indices in `terrain.cfg` are within bounds
2. Filelist entry counts match compiled sprite package counts
3. No out-of-bounds sprite references

**Example output:**
```
═══════════════════════════════════════════════════════════
  KeeperFX Sprite Configuration Validator
═══════════════════════════════════════════════════════════

Found 18 room configurations in terrain.cfg

Validating Room Sprite Indices:
  ✓ [room2] TREASURE large:29 small:59
  ✓ [room17] CASINO large:57 small:87
  ...

✓ All validations passed!
```

---

## Room Scaffolding Tools

### `scaffold_room.sh`

Generates configuration boilerplate for new rooms.

**Purpose:**
- Reduces manual work when creating new room types
- Ensures consistent configuration structure
- Provides implementation checklist

**Usage:**
```bash
# Generate configuration for a new room
./tools/scaffold_room.sh ROOM_NAME SLAB_NAME LARGE_SPRITE_IDX SMALL_SPRITE_IDX

# Example: Casino room
./tools/scaffold_room.sh CASINO CASINO_AREA 57 87
```

**What it generates:**
1. `[slabN]` section template for `terrain.cfg`
2. `[roomN]` section template for `terrain.cfg`
3. `RoomKinds` enum entry for `room_data.h`
4. Implementation checklist with file paths

**Example output:**
```
════════════════════════════════════════════════════════
  KeeperFX Room Configuration Scaffolder
════════════════════════════════════════════════════════

Room: CASINO
Slab: CASINO_AREA
Sprites: Large=57, Small=87

Generated Slab Configuration (add to terrain.cfg):
─────────────────────────────────────────────────────────
[slabNN]
Name = CASINO_AREA
...

Implementation Checklist:
[ ] 1. Add slab sections to config/fxdata/terrain.cfg
[ ] 2. Add room section to config/fxdata/terrain.cfg
[ ] 3. Update RoomKinds enum in src/room_data.h
...
```

---

## Workflow Integration

### Adding a New Room (Recommended Process)

1. **Create sprite assets** (PNG files at required sizes)
   - 50×64, 38×46, 25×32, 19×23 for each variant

2. **Add to filelists**
   - `gfx/menufx/gui2-64/filelist_gui2.txt`
   - `gfx/menufx/gui2-32/filelist_gui2.txt`
   - Note the line numbers (these become sprite indices)

3. **Generate configuration**
   ```bash
   ./tools/scaffold_room.sh MYROOM MYROOM_AREA <large_idx> <small_idx>
   ```

4. **Apply generated config**
   - Copy slab/room sections to `terrain.cfg`
   - Add enum to `room_data.h`
   - Replace placeholders (NN, XX, XXX) with actual values

5. **Rebuild sprite packages**
   ```bash
   make pkg/data/gui2-64.dat pkg/data/gui2-32.dat -B
   ```

6. **Validate configuration**
   ```bash
   python3 tools/validate_sprite_config.py --verbose
   ```

7. **Implement room logic**
   - Create `src/room_myroom.c` and `.h`
   - Implement required functions
   - Add to Makefile sources

8. **Deploy and test**
   ```bash
   make
   # Copy files to .deploy/
   # Test in-game
   ```

---

## Troubleshooting

### "Tab file not found" error
**Problem:** Sprite packages not built yet  
**Solution:** Run `make pkg-gfx` or `make pkg/data/gui2-64.dat`

### "Sprite index X >= Y" error
**Problem:** Configuration references non-existent sprite  
**Solution:** 
1. Check filelist line numbers match indices
2. Rebuild sprite packages with `-B` flag if PNGs changed
3. Fix indices in `terrain.cfg`

### "Filelist has X entries, but .tab has Y sprites"
**Problem:** Sprite package is stale (PNGs changed after last build)  
**Solution:** Force rebuild with `make pkg/data/gui2-64.dat -B`

---

## Future Enhancements

Planned improvements to the toolchain:

1. **Auto-generate sprite enums** (`sprites_generated.h`)
   - Eliminate manual enum synchronization
   - Generate from filelists at build time

2. **Content-based Make dependencies**
   - Auto-rebuild when PNG contents change
   - No more manual `-B` flag needed

3. **Startup sprite validator** (C++ runtime)
   - Fail-fast on launch with clear errors
   - Validate all config sprite indices exist

4. **Named sprite references in configs**
   - Use names instead of indices: `SymbolSprites = CASINO_LARGE CASINO_SMALL`
   - Immune to filelist reordering

---

## Contributing

When adding new tools or improving existing ones:

1. Follow the existing tool patterns
2. Add usage documentation to this README
3. Include error handling and helpful messages
4. Test with both valid and invalid inputs
5. Consider integration with Make/build system

---

## Related Documentation

- [Sprite System Architecture](../docs/data_structure.md)
- [Campaign Creation Guide](../docs/creating_campaigns.txt)
- [Build Instructions](../docs/build_instructions.txt)
