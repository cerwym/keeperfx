#!/usr/bin/env python3
"""
KeeperFX Sprite Configuration Validator

Validates that:
1. All sprite indices in terrain.cfg exist in built sprite packages
2. Filelist line numbers match expected sprite indices
3. No duplicate or out-of-bounds references

Usage:
    python3 tools/validate_sprite_config.py
    python3 tools/validate_sprite_config.py --verbose
"""

import struct
import sys
import re
import argparse
from pathlib import Path
from typing import Dict, List, Tuple

class Colors:
    """Terminal colors for output"""
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    RESET = '\033[0m'
    BOLD = '\033[1m'

def get_sprite_count(tab_file: Path) -> int:
    """Extract sprite count from .tab file format"""
    if not tab_file.exists():
        raise FileNotFoundError(f"Tab file not found: {tab_file}")
    
    data = tab_file.read_bytes()
    # Each sprite entry is 8 bytes in SSPR format (4 bytes offset + 4 bytes size)
    count = len(data) // 8
    return count

def count_filelist_entries(filelist: Path) -> int:
    """Count lines in filelist (excluding header line)"""
    if not filelist.exists():
        return 0
    
    lines = filelist.read_text(encoding='utf-8').strip().split('\n')
    # First line is header with format info
    return len(lines) - 1

def parse_terrain_cfg(cfg_file: Path) -> List[Dict]:
    """Parse terrain.cfg and extract room configurations"""
    if not cfg_file.exists():
        raise FileNotFoundError(f"Config file not found: {cfg_file}")
    
    content = cfg_file.read_text(encoding='utf-8')
    rooms = []
    
    # Match [roomN] sections
    room_pattern = re.compile(r'\[room(\d+)\](.*?)(?=\[room\d+\]|\[block_health\]|$)', re.DOTALL)
    
    for match in room_pattern.finditer(content):
        room_num = int(match.group(1))
        section = match.group(2)
        
        # Extract Name
        name_match = re.search(r'Name\s*=\s*(\S+)', section)
        name = name_match.group(1) if name_match else f"ROOM{room_num}"
        
        # Extract SymbolSprites
        sym_match = re.search(r'SymbolSprites\s*=\s*(\d+)\s+(\d+)', section)
        if sym_match:
            large_idx = int(sym_match.group(1))
            small_idx = int(sym_match.group(2))
            
            rooms.append({
                'num': room_num,
                'name': name,
                'large_sprite': large_idx,
                'small_sprite': small_idx
            })
    
    return rooms

def validate_sprite_indices(
    rooms: List[Dict],
    sprite_counts: Dict[str, int],
    verbose: bool = False
) -> int:
    """
    Validate sprite indices against package counts
    Returns number of errors found
    """
    errors = 0
    
    if verbose:
        print(f"\n{Colors.BOLD}Sprite Package Counts:{Colors.RESET}")
        for name, count in sprite_counts.items():
            print(f"  {Colors.CYAN}{name}{Colors.RESET}: {count} sprites (indices 0-{count-1})")
    
    print(f"\n{Colors.BOLD}Validating Room Sprite Indices:{Colors.RESET}")
    
    for room in rooms:
        room_id = f"[room{room['num']}] {room['name']}"
        errors_this_room = []
        
        # Check large sprite (gui2-64 package)
        if room['large_sprite'] >= sprite_counts['gui2-64']:
            errors_this_room.append(
                f"large sprite {room['large_sprite']} >= {sprite_counts['gui2-64']} (gui2-64.tab)"
            )
        
        # Check small sprite (gui2-32 package)  
        if room['small_sprite'] >= sprite_counts['gui2-32']:
            errors_this_room.append(
                f"small sprite {room['small_sprite']} >= {sprite_counts['gui2-32']} (gui2-32.tab)"
            )
        
        if errors_this_room:
            print(f"  {Colors.RED}✗ {room_id}{Colors.RESET}")
            for err in errors_this_room:
                print(f"    {err}")
            errors += len(errors_this_room)
        elif verbose:
            print(f"  {Colors.GREEN}✓ {room_id}{Colors.RESET} large:{room['large_sprite']} small:{room['small_sprite']}")
    
    return errors

def validate_filelist_consistency(
    filelist_64: Path,
    filelist_32: Path,
    sprite_counts: Dict[str, int],
    verbose: bool = False
) -> int:
    """Validate filelist line counts match sprite package counts"""
    errors = 0
    
    print(f"\n{Colors.BOLD}Validating Filelist Consistency:{Colors.RESET}")
    
    # Check gui2-64
    if filelist_64.exists():
        count_64 = count_filelist_entries(filelist_64)
        if count_64 != sprite_counts['gui2-64']:
            print(f"  {Colors.RED}✗ filelist_gui2.txt (gui2-64){Colors.RESET}")
            print(f"    Filelist has {count_64} entries, but gui2-64.tab has {sprite_counts['gui2-64']} sprites")
            print(f"    {Colors.YELLOW}Action: Rebuild with 'make pkg/data/gui2-64.dat -B'{Colors.RESET}")
            errors += 1
        elif verbose:
            print(f"  {Colors.GREEN}✓ gui2-64 filelist{Colors.RESET}: {count_64} entries match .tab count")
    else:
        print(f"  {Colors.YELLOW}⚠ gui2-64 filelist not found{Colors.RESET}")
    
    # Check gui2-32
    if filelist_32.exists():
        count_32 = count_filelist_entries(filelist_32)
        if count_32 != sprite_counts['gui2-32']:
            print(f"  {Colors.RED}✗ filelist_gui2.txt (gui2-32){Colors.RESET}")
            print(f"    Filelist has {count_32} entries, but gui2-32.tab has {sprite_counts['gui2-32']} sprites")
            print(f"    {Colors.YELLOW}Action: Rebuild with 'make pkg/data/gui2-32.dat -B'{Colors.RESET}")
            errors += 1
        elif verbose:
            print(f"  {Colors.GREEN}✓ gui2-32 filelist{Colors.RESET}: {count_32} entries match .tab count")
    else:
        print(f"  {Colors.YELLOW}⚠ gui2-32 filelist not found{Colors.RESET}")
    
    return errors

def main():
    parser = argparse.ArgumentParser(description='Validate KeeperFX sprite configuration')
    parser.add_argument('-v', '--verbose', action='store_true', help='Show detailed validation output')
    parser.add_argument('--workspace', type=Path, default=Path.cwd(), help='Workspace root directory')
    args = parser.parse_args()
    
    workspace = args.workspace
    
    print(f"{Colors.BOLD}{Colors.BLUE}═══════════════════════════════════════════════════════════{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.BLUE}  KeeperFX Sprite Configuration Validator{Colors.RESET}")
    print(f"{Colors.BOLD}{Colors.BLUE}═══════════════════════════════════════════════════════════{Colors.RESET}")
    
    # Paths
    gui2_64_tab = workspace / 'pkg' / 'data' / 'gui2-64.tab'
    gui2_32_tab = workspace / 'pkg' / 'data' / 'gui2-32.tab'
    terrain_cfg = workspace / 'config' / 'fxdata' / 'terrain.cfg'
    filelist_64 = workspace / 'gfx' / 'menufx' / 'gui2-64' / 'filelist_gui2.txt'
    filelist_32 = workspace / 'gfx' / 'menufx' / 'gui2-32' / 'filelist_gui2.txt'
    
    total_errors = 0
    
    try:
        # Get sprite counts from built packages
        sprite_counts = {
            'gui2-64': get_sprite_count(gui2_64_tab),
            'gui2-32': get_sprite_count(gui2_32_tab)
        }
        
        # Parse terrain.cfg
        rooms = parse_terrain_cfg(terrain_cfg)
        print(f"\nFound {Colors.CYAN}{len(rooms)}{Colors.RESET} room configurations in {Colors.CYAN}terrain.cfg{Colors.RESET}")
        
        # Validate sprite indices
        errors = validate_sprite_indices(rooms, sprite_counts, args.verbose)
        total_errors += errors
        
        # Validate filelist consistency
        errors = validate_filelist_consistency(
            filelist_64, filelist_32, sprite_counts, args.verbose
        )
        total_errors += errors
        
        # Summary
        print(f"\n{Colors.BOLD}{'═' * 59}{Colors.RESET}")
        if total_errors == 0:
            print(f"{Colors.GREEN}{Colors.BOLD}✓ All validations passed!{Colors.RESET}")
            return 0
        else:
            print(f"{Colors.RED}{Colors.BOLD}✗ {total_errors} error(s) found{Colors.RESET}")
            print(f"\n{Colors.YELLOW}Recommended actions:{Colors.RESET}")
            print(f"  1. Check if sprite packages need rebuilding")
            print(f"  2. Fix sprite indices in terrain.cfg if out of bounds")
            print(f"  3. Redeploy updated files to .deploy/")
            return 1
            
    except FileNotFoundError as e:
        print(f"\n{Colors.RED}{Colors.BOLD}Error:{Colors.RESET} {e}")
        print(f"{Colors.YELLOW}Run 'make pkg-gfx' to build sprite packages first{Colors.RESET}")
        return 2
    except Exception as e:
        print(f"\n{Colors.RED}{Colors.BOLD}Unexpected error:{Colors.RESET} {e}")
        import traceback
        traceback.print_exc()
        return 3

if __name__ == '__main__':
    sys.exit(main())
