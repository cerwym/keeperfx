#!/usr/bin/env python3
"""Fix remaining config files - run after the main transform script."""
import re
import os

SRC = r'C:/Users/peter/source/repos/keeperfx.worktrees/renderer-abstraction/src'

def read_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read()

def write_file(path, content):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(content)

def transform_game_field_arg(arg):
    arg = arg.strip()
    if arg == 'game.pay_day_progress[0]':
        return ('(void*)((ptrdiff_t)__builtin_offsetof(struct Game, pay_day_progress[0]) - '
                '(ptrdiff_t)__builtin_offsetof(struct Game, conf.rules[0])),\n'
                '   var_type(((struct Game*)0)->pay_day_progress[0])')
    m = re.match(r'^(game\..*?\[\d+\])\.(.+)$', arg)
    if m:
        elem0 = m.group(1).replace('game.', 'gpGame->', 1)
        return f'field({elem0}, {m.group(2)})'
    return None

def process_game_file(filename, array_start, nfs_start, getter_code, old_set, new_set):
    path = os.path.join(SRC, filename)
    content = read_file(path)

    def field_replacer(m):
        inner = m.group(1)
        result = transform_game_field_arg(inner)
        if result is not None:
            return result
        return m.group(0)
    content = re.sub(r'field\(([^)]+)\)', field_replacer, content)

    pragma_push = '#pragma push_macro("game")\n#undef game\n'
    pragma_pop = '#pragma pop_macro("game")\n'

    idx_start = content.find(array_start)
    assert idx_start != -1, f"Array start not found in {filename}: {array_start[:60]}"

    nfs_pos = content.find(nfs_start)
    assert nfs_pos != -1, f"NFS start not found in {filename}"

    region = content[:nfs_pos]
    last_close = region.rfind('};\n')
    idx_end = last_close + len('};\n')

    content = (content[:idx_start] +
               pragma_push +
               content[idx_start:idx_end] +
               pragma_pop + '\n' +
               getter_code +
               content[idx_end:])

    assert old_set in content, f"NamedFieldSet not found in {filename}"
    content = content.replace(old_set, new_set)

    write_file(path, content)
    print(f"Modified {filename}")

# config_crtrstates.c
process_game_file(
    'config_crtrstates.c',
    'const struct NamedField crstates_states_named_fields[] = {',
    '\nconst struct NamedFieldSet crstates_states_named_fields_set = {',
    ('static int32_t* get_crstates_count(void) { return &game.conf.crtr_conf.states_count; }\n'
     'static void* get_crstates_base(void) { return game.conf.crtr_conf.states; }\n\n'),
    ('const struct NamedFieldSet crstates_states_named_fields_set = {\n'
     '    &game.conf.crtr_conf.states_count,\n'
     '    "state",\n'
     '    crstates_states_named_fields,\n'
     '    creatrstate_desc,\n'
     '    CREATURE_STATES_MAX,\n'
     '    sizeof(game.conf.crtr_conf.states[0]),\n'
     '    game.conf.crtr_conf.states,\n'
     '};'),
    ('const struct NamedFieldSet crstates_states_named_fields_set = {\n'
     '    get_crstates_count,\n'
     '    "state",\n'
     '    crstates_states_named_fields,\n'
     '    creatrstate_desc,\n'
     '    CREATURE_STATES_MAX,\n'
     '    sizeof(gpGame->conf.crtr_conf.states[0]),\n'
     '    get_crstates_base,\n'
     '};')
)

# config_effects.c
process_game_file(
    'config_effects.c',
    'const struct NamedField effects_effectgenerator_named_fields[] = {',
    '\nconst struct NamedFieldSet effects_effectgenerator_named_fields_set = {',
    ('static int32_t* get_effectgen_count(void) { return &game.conf.effects_conf.effectgen_cfgstats_count; }\n'
     'static void* get_effectgen_base(void) { return game.conf.effects_conf.effectgen_cfgstats; }\n\n'),
    ('const struct NamedFieldSet effects_effectgenerator_named_fields_set = {\n'
     '    &game.conf.effects_conf.effectgen_cfgstats_count,\n'
     '    "effectGenerator",\n'
     '    effects_effectgenerator_named_fields,\n'
     '    effectgen_desc,\n'
     '    EFFECTSGEN_TYPES_MAX,\n'
     '    sizeof(game.conf.effects_conf.effectgen_cfgstats[0]),\n'
     '    game.conf.effects_conf.effectgen_cfgstats,\n'
     '};'),
    ('const struct NamedFieldSet effects_effectgenerator_named_fields_set = {\n'
     '    get_effectgen_count,\n'
     '    "effectGenerator",\n'
     '    effects_effectgenerator_named_fields,\n'
     '    effectgen_desc,\n'
     '    EFFECTSGEN_TYPES_MAX,\n'
     '    sizeof(gpGame->conf.effects_conf.effectgen_cfgstats[0]),\n'
     '    get_effectgen_base,\n'
     '};')
)

# config_cubes.c
process_game_file(
    'config_cubes.c',
    'static const struct NamedField cubes_named_fields[] = {',
    '\nconst struct NamedFieldSet cubes_named_fields_set = {',
    ('static int32_t* get_cubes_count(void) { return &game.conf.cube_conf.cube_types_count; }\n'
     'static void* get_cubes_base(void) { return game.conf.cube_conf.cube_cfgstats; }\n\n'),
    ('const struct NamedFieldSet cubes_named_fields_set = {\n'
     '    &game.conf.cube_conf.cube_types_count,\n'
     '    "cube",\n'
     '    cubes_named_fields,\n'
     '    cube_desc,\n'
     '    CUBE_ITEMS_MAX,\n'
     '    sizeof(game.conf.cube_conf.cube_cfgstats[0]),\n'
     '    game.conf.cube_conf.cube_cfgstats,\n'
     '};'),
    ('const struct NamedFieldSet cubes_named_fields_set = {\n'
     '    get_cubes_count,\n'
     '    "cube",\n'
     '    cubes_named_fields,\n'
     '    cube_desc,\n'
     '    CUBE_ITEMS_MAX,\n'
     '    sizeof(gpGame->conf.cube_conf.cube_cfgstats[0]),\n'
     '    get_cubes_base,\n'
     '};')
)

# config_compp.c
def transform_compp_field_arg(arg):
    arg = arg.strip()
    m = re.match(r'^(comp_player_conf\..*?\[\d+\])\.(.+)$', arg)
    if m:
        return f'field({m.group(1)}, {m.group(2)})'
    m = re.match(r'^(comp_player_conf)\.(.+)$', arg)
    if m:
        return f'field({m.group(1)}, {m.group(2)})'
    return None

path = os.path.join(SRC, 'config_compp.c')
content = read_file(path)

def compp_field_replacer(m):
    inner = m.group(1)
    result = transform_compp_field_arg(inner)
    if result is not None:
        return result
    return m.group(0)
content = re.sub(r'field\(([^)]+)\)', compp_field_replacer, content)

getters = (
    'static void* get_compp_common_base(void) { return &comp_player_conf; }\n'
    'static int32_t* get_processes_count(void) { return &comp_player_conf.processes_count; }\n'
    'static void* get_processes_base(void) { return comp_player_conf.process_types; }\n'
    'static int32_t* get_checks_count(void) { return &comp_player_conf.checks_count; }\n'
    'static void* get_checks_base(void) { return comp_player_conf.check_types; }\n'
    'static int32_t* get_events_count(void) { return &comp_player_conf.events_count; }\n'
    'static void* get_events_base(void) { return comp_player_conf.event_types; }\n'
    'static int32_t* get_computers_count(void) { return &comp_player_conf.computers_count; }\n'
    'static void* get_computers_base(void) { return comp_player_conf.computer_types; }\n\n'
)
first_nfs = '\nconst struct NamedFieldSet compp_common_named_fields_set = {'
idx = content.find(first_nfs)
assert idx != -1
content = content[:idx] + '\n' + getters + content[idx+1:]

replacements = [
    ('const struct NamedFieldSet compp_common_named_fields_set = {\n'
     '  NULL,\n  "common",\n  compp_common_named_fields,\n  NULL,\n  0,\n  0,\n  NULL,\n};',
     'const struct NamedFieldSet compp_common_named_fields_set = {\n'
     '  NULL,\n  "common",\n  compp_common_named_fields,\n  NULL,\n  0,\n  0,\n  get_compp_common_base,\n};'),
    ('const struct NamedFieldSet compp_process_named_fields_set = {\n'
     '  &comp_player_conf.processes_count,\n  "process",\n  compp_process_named_fields,\n  NULL,\n'
     '  COMPUTER_PROCESS_TYPES_COUNT,\n  sizeof(comp_player_conf.process_types[0]),\n  comp_player_conf.process_types,\n};',
     'const struct NamedFieldSet compp_process_named_fields_set = {\n'
     '  get_processes_count,\n  "process",\n  compp_process_named_fields,\n  NULL,\n'
     '  COMPUTER_PROCESS_TYPES_COUNT,\n  sizeof(comp_player_conf.process_types[0]),\n  get_processes_base,\n};'),
    ('const struct NamedFieldSet compp_check_named_fields_set = {\n'
     '  &comp_player_conf.checks_count,\n  "check",\n  compp_check_named_fields,\n  NULL,\n'
     '  COMPUTER_CHECKS_TYPES_COUNT,\n  sizeof(comp_player_conf.check_types[0]),\n  comp_player_conf.check_types,\n};',
     'const struct NamedFieldSet compp_check_named_fields_set = {\n'
     '  get_checks_count,\n  "check",\n  compp_check_named_fields,\n  NULL,\n'
     '  COMPUTER_CHECKS_TYPES_COUNT,\n  sizeof(comp_player_conf.check_types[0]),\n  get_checks_base,\n};'),
    ('const struct NamedFieldSet compp_event_named_fields_set = {\n'
     '  &comp_player_conf.events_count,\n  "event",\n  compp_event_named_fields,\n  NULL,\n'
     '  COMPUTER_EVENTS_TYPES_COUNT,\n  sizeof(comp_player_conf.event_types[0]),\n  comp_player_conf.event_types,\n};',
     'const struct NamedFieldSet compp_event_named_fields_set = {\n'
     '  get_events_count,\n  "event",\n  compp_event_named_fields,\n  NULL,\n'
     '  COMPUTER_EVENTS_TYPES_COUNT,\n  sizeof(comp_player_conf.event_types[0]),\n  get_events_base,\n};'),
    ('const struct NamedFieldSet compp_computer_named_fields_set = {\n'
     '  &comp_player_conf.computers_count,\n  "computer",\n  compp_computer_named_fields,\n  NULL,\n'
     '  COMPUTER_MODELS_COUNT,\n  sizeof(comp_player_conf.computer_types[0]),\n  comp_player_conf.computer_types,\n};',
     'const struct NamedFieldSet compp_computer_named_fields_set = {\n'
     '  get_computers_count,\n  "computer",\n  compp_computer_named_fields,\n  NULL,\n'
     '  COMPUTER_MODELS_COUNT,\n  sizeof(comp_player_conf.computer_types[0]),\n  get_computers_base,\n};'),
]
for old, new in replacements:
    assert old in content, f"Not found:\n{old[:120]}"
    content = content.replace(old, new)

write_file(path, content)
print("Modified config_compp.c")

# config_lenses.c
def transform_lenses_field_arg(arg):
    arg = arg.strip()
    m = re.match(r'^(lenses_conf\..*?\[\d+\])\.(.+)$', arg)
    if m:
        return f'field({m.group(1)}, {m.group(2)})'
    return None

path = os.path.join(SRC, 'config_lenses.c')
content = read_file(path)

def lenses_field_replacer(m):
    inner = m.group(1)
    result = transform_lenses_field_arg(inner)
    if result is not None:
        return result
    return m.group(0)
content = re.sub(r'field\(([^)]+)\)', lenses_field_replacer, content)

getters = ('static int32_t* get_lenses_count(void) { return &lenses_conf.lenses_count; }\n'
           'static void* get_lenses_base(void) { return lenses_conf.lenses; }\n\n')
nfs_pos = content.find('\nconst struct NamedFieldSet lenses_data_named_fields_set = {')
assert nfs_pos != -1
content = content[:nfs_pos] + '\n' + getters + content[nfs_pos+1:]

old_set = ('const struct NamedFieldSet lenses_data_named_fields_set = {\n'
           '    &lenses_conf.lenses_count,\n'
           '    "lens",\n'
           '    lenses_data_named_fields,\n'
           '    lenses_desc,\n'
           '    LENS_ITEMS_MAX,\n'
           '    sizeof(lenses_conf.lenses[0]),\n'
           '    lenses_conf.lenses,\n'
           '};')
new_set = ('const struct NamedFieldSet lenses_data_named_fields_set = {\n'
           '    get_lenses_count,\n'
           '    "lens",\n'
           '    lenses_data_named_fields,\n'
           '    lenses_desc,\n'
           '    LENS_ITEMS_MAX,\n'
           '    sizeof(lenses_conf.lenses[0]),\n'
           '    get_lenses_base,\n'
           '};')
assert old_set in content, "lenses_data_named_fields_set not found"
content = content.replace(old_set, new_set)

old_pallete = ('    if (LbFileLoadAt(fname, (char*)(named_field->field) + named_fields_set->struct_size * idx) != PALETTE_SIZE)')
new_pallete = ('    if (LbFileLoadAt(fname, (char*)named_fields_set->get_struct_base() + named_fields_set->struct_size * idx + (ptrdiff_t)named_field->field) != PALETTE_SIZE)')
assert old_pallete in content, "value_pallete not found in config_lenses.c"
content = content.replace(old_pallete, new_pallete)

write_file(path, content)
print("Modified config_lenses.c")

print("All remaining files done!")
