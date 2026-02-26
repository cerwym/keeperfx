#!/usr/bin/env python3
"""
Generate a GOG Galaxy-compatible VDF file from KeeperFX achievement .cfg files.

Usage:
    python tools/generate_achievements_vdf.py [--output achievements.vdf]

Parses data/achievements_global.cfg and all campgns/*/achievements.cfg files,
then emits a single VDF file suitable for upload to the GOG Developer Portal
via the "Upload VDF" button on the Achievements page.

VDF format reference: https://docs.gog.com/sdk-steam-import/
"""

import os
import re
import sys
import glob
import argparse

def parse_achievements_cfg(filepath, scope_prefix=None):
    """Parse a KeeperFX achievements.cfg file and return a list of achievement dicts."""
    achievements = []
    current = None

    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith(';'):
                continue

            if line.startswith('ACHIEVEMENT '):
                ach_id = line.split(None, 1)[1].strip()
                current = {
                    'id': ach_id,
                    'name': '',
                    'description': '',
                    'hidden': '0',
                    'gog_id': '',
                    'scope': scope_prefix or 'global',
                }
            elif line == 'END_ACHIEVEMENT' and current:
                if not current['gog_id']:
                    current['gog_id'] = current['id']
                achievements.append(current)
                current = None
            elif current:
                if line.startswith('NAME '):
                    match = re.search(r'"([^"]*)"', line)
                    if match:
                        current['name'] = match.group(1)
                elif line.startswith('DESCRIPTION '):
                    match = re.search(r'"([^"]*)"', line)
                    if match:
                        current['description'] = match.group(1)
                elif line.startswith('HIDDEN '):
                    current['hidden'] = line.split(None, 1)[1].strip()
                elif line.startswith('GOG_ID '):
                    current['gog_id'] = line.split(None, 1)[1].strip()

    return achievements


def escape_vdf(s):
    """Escape a string for VDF output."""
    return s.replace('\\', '\\\\').replace('"', '\\"')


def generate_vdf(achievements, output_path):
    """Write achievements to a VDF file in GOG-compatible format."""
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write('"0"\n{\n')
        f.write('\t"stats"\n\t{\n')
        f.write('\t\t"0"\n\t\t{\n')
        f.write('\t\t\t"bits"\n\t\t\t{\n')

        for i, ach in enumerate(achievements):
            api_key = ach['gog_id'] if ach['gog_id'] else ach['id']
            f.write(f'\t\t\t\t"{i}"\n')
            f.write('\t\t\t\t{\n')
            f.write(f'\t\t\t\t\t"name" "{escape_vdf(api_key)}"\n')
            f.write('\t\t\t\t\t"display"\n')
            f.write('\t\t\t\t\t{\n')
            f.write('\t\t\t\t\t\t"name"\n')
            f.write('\t\t\t\t\t\t{\n')
            f.write(f'\t\t\t\t\t\t\t"english" "{escape_vdf(ach["name"])}"\n')
            f.write('\t\t\t\t\t\t}\n')
            f.write('\t\t\t\t\t\t"desc"\n')
            f.write('\t\t\t\t\t\t{\n')
            f.write(f'\t\t\t\t\t\t\t"english" "{escape_vdf(ach["description"])}"\n')
            f.write('\t\t\t\t\t\t}\n')
            f.write(f'\t\t\t\t\t\t"hidden" "{ach["hidden"]}"\n')
            f.write('\t\t\t\t\t}\n')
            f.write('\t\t\t\t}\n')

        f.write('\t\t\t}\n')
        f.write('\t\t}\n')
        f.write('\t}\n')
        f.write('}\n')


def main():
    parser = argparse.ArgumentParser(description='Generate GOG VDF from KeeperFX achievement configs')
    parser.add_argument('-o', '--output', default='achievements.vdf',
                        help='Output VDF file path (default: achievements.vdf)')
    parser.add_argument('--root', default='.',
                        help='KeeperFX repository root (default: current directory)')
    args = parser.parse_args()

    root = args.root
    all_achievements = []

    # Parse global achievements
    global_cfg = os.path.join(root, 'data', 'achievements_global.cfg')
    if os.path.exists(global_cfg):
        achs = parse_achievements_cfg(global_cfg, scope_prefix='global')
        print(f"  Global: {len(achs)} achievements from {global_cfg}")
        all_achievements.extend(achs)
    else:
        print(f"  Warning: {global_cfg} not found", file=sys.stderr)

    # Parse campaign achievements
    campaign_pattern = os.path.join(root, 'campgns', '*', 'achievements.cfg')
    for cfg_path in sorted(glob.glob(campaign_pattern)):
        campaign_name = os.path.basename(os.path.dirname(cfg_path))
        achs = parse_achievements_cfg(cfg_path, scope_prefix=campaign_name)
        print(f"  Campaign '{campaign_name}': {len(achs)} achievements from {cfg_path}")
        all_achievements.extend(achs)

    if not all_achievements:
        print("Error: No achievements found!", file=sys.stderr)
        sys.exit(1)

    # Check for duplicate GOG IDs
    gog_ids = {}
    for ach in all_achievements:
        gog_id = ach['gog_id'] or ach['id']
        if gog_id in gog_ids:
            print(f"  Warning: Duplicate GOG_ID '{gog_id}' "
                  f"(in {ach['scope']} and {gog_ids[gog_id]})", file=sys.stderr)
        gog_ids[gog_id] = ach['scope']

    generate_vdf(all_achievements, args.output)
    print(f"\nGenerated {args.output} with {len(all_achievements)} achievements")


if __name__ == '__main__':
    main()
