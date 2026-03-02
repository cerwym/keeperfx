#!/usr/bin/env python3
"""
Transform KeeperFX config named-field system from raw pointer to offset-based approach.
"""
import re
import os

SRC = r'C:/Users/peter/source/repos/keeperfx.worktrees/renderer-abstraction/src'

def read_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read()

def write_file(path, content):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(content)

# ===========================
# config.h changes
# ===========================
def modify_config_h():
    path = os.path.join(SRC, 'config.h')
    content = read_file(path)

    old = '#define field(field)\\\n    &field, var_type(field)'
    new = ('#define field(elem0_expr, member_path) \\\n'
           '    (void*)(ptrdiff_t)__builtin_offsetof(__typeof__(elem0_expr), member_path), \\\n'
           '    var_type(((elem0_expr).member_path))')
    assert old in content, "field macro not found in config.h"
    content = content.replace(old, new)

    old = ('struct NamedFieldSet {\n'
           '    int32_t *const count_field;\n'
           '    const char* block_basename;\n'
           '    const struct NamedField* named_fields;\n'
           '    struct NamedCommand* names;\n'
           '    const int max_count;\n'
           '    const size_t struct_size;\n'
           '    const void* struct_base;\n'
           '};')
    new = ('struct NamedFieldSet {\n'
           '    int32_t* (*get_count)(void);\n'
           '    const char* block_basename;\n'
           '    const struct NamedField* named_fields;\n'
           '    struct NamedCommand* names;\n'
           '    const int max_count;\n'
           '    const size_t struct_size;\n'
           '    void* (*get_struct_base)(void);\n'
           '};')
    assert old in content, "NamedFieldSet struct not found in config.h"
    content = content.replace(old, new)

    write_file(path, content)
    print("Modified config.h")

# ===========================
# config.c changes
# ===========================
def modify_config_c():
    path = os.path.join(SRC, 'config.c')
    content = read_file(path)

    # value_name function
    old = ('    size_t offset = named_fields_set->struct_size * idx;\n'
           '    strncpy((char*)named_field->field + offset, value_text, COMMAND_WORD_LEN - 1);\n'
           '    ((char*)named_field->field + offset)[COMMAND_WORD_LEN - 1] = \'\\0\';')
    new = ('    void* field_ptr = (char*)named_fields_set->get_struct_base() + named_fields_set->struct_size * idx + (ptrdiff_t)named_field->field;\n'
           '    strncpy(field_ptr, value_text, COMMAND_WORD_LEN - 1);\n'
           '    ((char*)field_ptr)[COMMAND_WORD_LEN - 1] = \'\\0\';')
    assert old in content, "value_name not found in config.c"
    content = content.replace(old, new)

    # get_named_field_value and assign_default both have this same line
    old = '    void* field = (char*)named_field->field + named_fields_set->struct_size * idx;'
    new = '    void* field = (char*)named_fields_set->get_struct_base() + named_fields_set->struct_size * idx + (ptrdiff_t)named_field->field;'
    assert content.count(old) == 2, f"Expected 2 occurrences of field line, got {content.count(old)}"
    content = content.replace(old, new)

    # set_defaults: memset
    old = '  memset((void *)named_fields_set->struct_base, 0, named_fields_set->struct_size * named_fields_set->max_count);'
    new = '  memset(named_fields_set->get_struct_base(), 0, named_fields_set->struct_size * named_fields_set->max_count);'
    assert old in content, "memset line not found in config.c"
    content = content.replace(old, new)

    # set_defaults: names assignment
    old = '          named_fields_set->names[i].name = (char*)name_NamedField->field + i * named_fields_set->struct_size;'
    new = '          named_fields_set->names[i].name = (char*)named_fields_set->get_struct_base() + i * named_fields_set->struct_size + (ptrdiff_t)name_NamedField->field;'
    assert old in content, "names assignment not found in config.c"
    content = content.replace(old, new)

    # parse_named_field_blocks: count_field
    old = ('        } else if (i >= *named_fields_set->count_field) {\n'
           '            *named_fields_set->count_field = i + 1;')
    new = ('        } else if (i >= *named_fields_set->get_count()) {\n'
           '            *named_fields_set->get_count() = i + 1;')
    assert old in content, "count_field not found in config.c"
    content = content.replace(old, new)

    write_file(path, content)
    print("Modified config.c")

# ===========================
# Field() transformation helpers
# ===========================
def transform_game_field_arg(arg):
    """
    Transform 'game.conf.ARRAY[N].MEMBER' to 'field(gpGame->conf.ARRAY[N], MEMBER)'
    Special case: 'game.pay_day_progress[0]' -> expanded offsetof expression
    """
    arg = arg.strip()
    if arg == 'game.pay_day_progress[0]':
        return ('(void*)((ptrdiff_t)__builtin_offsetof(struct Game, pay_day_progress[0]) - '
                '(ptrdiff_t)__builtin_offsetof(struct Game, conf.rules[0])),\n'
                '   var_type(((struct Game*)0)->pay_day_progress[0])')
    # game.conf.SOMETHING[N].MEMBER_PATH
    m = re.match(r'^(game\..*?\[\d+\])\.(.+)$', arg)
    if m:
        elem0 = m.group(1).replace('game.', 'gpGame->', 1)
        return f'field({elem0}, {m.group(2)})'
    return None

def transform_compp_field_arg(arg):
    """Transform comp_player_conf-based field argument"""
    arg = arg.strip()
    # comp_player_conf.XXX[N].MEMBER
    m = re.match(r'^(comp_player_conf\..*?\[\d+\])\.(.+)$', arg)
    if m:
        return f'field({m.group(1)}, {m.group(2)})'
    # comp_player_conf.MEMBER (or comp_player_conf.ARRAY[N] with no further member)
    m = re.match(r'^(comp_player_conf)\.(.+)$', arg)
    if m:
        return f'field({m.group(1)}, {m.group(2)})'
    return None

def transform_lenses_field_arg(arg):
    """Transform lenses_conf-based field argument"""
    arg = arg.strip()
    m = re.match(r'^(lenses_conf\..*?\[\d+\])\.(.+)$', arg)
    if m:
        return f'field({m.group(1)}, {m.group(2)})'
    return None

def make_field_replacer(transform_fn):
    def replacer(m):
        inner = m.group(1)
        result = transform_fn(inner)
        if result is not None:
            return result
        return m.group(0)
    return replacer

# ===========================
# config_rules.c
# ===========================
def modify_config_rules_c():
    path = os.path.join(SRC, 'config_rules.c')
    content = read_file(path)

    # Find the region containing all static NamedField arrays
    # from "static const struct NamedField rules_game_named_fields" to end of rules_script_only
    first_array = 'static const struct NamedField rules_game_named_fields[] = {'
    last_array_end = '{"PayDayProgress",0,field(game.pay_day_progress[0]),0,0,INT32_MAX,NULL,value_default,assign_default},\n{NULL},\n};'

    assert first_array in content, "rules_game_named_fields not found"
    assert last_array_end in content, "rules_script_only end not found"

    # Replace field() calls in the arrays region first
    # We'll process the entire file but only transform game.xxx field() calls
    # since the pragma prevents other expansions
    def field_replacer(m):
        inner = m.group(1)
        result = transform_game_field_arg(inner)
        if result is not None:
            return result
        return m.group(0)

    content = re.sub(r'field\(([^)]+)\)', field_replacer, content)

    # Now add pragma guards around all static NamedField arrays
    # The pragma wraps from the first array to after the last one
    pragma_push = '#pragma push_macro("game")\n#undef game\n'
    pragma_pop = '#pragma pop_macro("game")\n'

    # The first array starts at "static const struct NamedField rules_game_named_fields"
    # We need to insert pragma BEFORE that line
    # Find its position
    idx_start = content.find(first_array)
    assert idx_start != -1

    # Find the end of the last static array (rules_script_only ends with {NULL},\n};)
    # After transformation, pay_day_progress is changed, so find the new end
    last_array_end_new = ('{NULL},\n'
                          '};\n')
    # Find the LAST occurrence of '{NULL},\n};\n' before the ruleblocks line
    ruleblocks_pos = content.find('\nconst struct NamedField* ruleblocks[]')
    assert ruleblocks_pos != -1

    # Find the last '};\n' before ruleblocks
    region_before_ruleblocks = content[:ruleblocks_pos]
    last_close = region_before_ruleblocks.rfind('};\n')
    assert last_close != -1
    idx_end = last_close + len('};\n')

    # Insert pragma guards
    content = (content[:idx_start] +
               pragma_push +
               content[idx_start:idx_end] +
               pragma_pop +
               '\n' +
               content[idx_end:])

    # Add getter function after pragma pop and before ruleblocks
    getter = 'static void* get_rules_base(void) { return game.conf.rules; }\n\n'

    # Find where to insert getter (after the new pragma_pop, before ruleblocks)
    # pragma_pop + '\n' is now inserted, find it and insert getter after
    insert_pos = content.find(pragma_pop + '\n\nconst struct NamedField* ruleblocks[]')
    if insert_pos != -1:
        insert_after = insert_pos + len(pragma_pop) + 1  # after '\n'
        content = content[:insert_after] + getter + content[insert_after:]
    else:
        # Try simpler: insert after pragma_pop + '\n'
        pp_pos = content.rfind(pragma_pop)
        assert pp_pos != -1
        insert_after = pp_pos + len(pragma_pop)
        content = content[:insert_after] + '\n' + getter + content[insert_after:]

    # Update rules_named_fields_set
    old_set = ('const struct NamedFieldSet rules_named_fields_set = {\n'
               '  NULL,\n'
               '  "",\n'
               '  NULL,\n'
               '  NULL,\n'
               '  PLAYERS_COUNT,\n'
               '  sizeof(game.conf.rules[0]),\n'
               '  &game.conf.rules,\n'
               '};')
    new_set = ('const struct NamedFieldSet rules_named_fields_set = {\n'
               '  NULL,\n'
               '  "",\n'
               '  NULL,\n'
               '  NULL,\n'
               '  PLAYERS_COUNT,\n'
               '  sizeof(gpGame->conf.rules[0]),\n'
               '  get_rules_base,\n'
               '};')
    assert old_set in content, f"rules_named_fields_set not found:\n{old_set}"
    content = content.replace(old_set, new_set)

    write_file(path, content)
    print("Modified config_rules.c")

# ===========================
# config_magic.c
# ===========================
def modify_config_magic_c():
    path = os.path.join(SRC, 'config_magic.c')
    content = read_file(path)

    def field_replacer(m):
        inner = m.group(1)
        result = transform_game_field_arg(inner)
        if result is not None:
            return result
        return m.group(0)
    content = re.sub(r'field\(([^)]+)\)', field_replacer, content)

    # Wrap array with pragma
    first_array = 'static const struct NamedField magic_powers_named_fields[] = {'
    idx_start = content.find(first_array)
    assert idx_start != -1

    # Find end: look for the line right before 'const struct NamedFieldSet magic_powers_named_fields_set'
    nfs_pos = content.find('\nconst struct NamedFieldSet magic_powers_named_fields_set')
    assert nfs_pos != -1
    region = content[:nfs_pos]
    last_close = region.rfind('};\n')
    idx_end = last_close + len('};\n')

    pragma_push = '#pragma push_macro("game")\n#undef game\n'
    pragma_pop = '#pragma pop_macro("game")\n'
    getter = ('static int32_t* get_powers_count(void) { return &game.conf.magic_conf.power_types_count; }\n'
              'static void* get_powers_base(void) { return game.conf.magic_conf.power_cfgstats; }\n\n')

    content = (content[:idx_start] +
               pragma_push +
               content[idx_start:idx_end] +
               pragma_pop + '\n' +
               getter +
               content[idx_end:])

    # Update NamedFieldSet
    old_set = ('const struct NamedFieldSet magic_powers_named_fields_set = {\n'
               '    &game.conf.magic_conf.power_types_count,\n'
               '    "power",\n'
               '    magic_powers_named_fields,\n'
               '    power_desc,\n'
               '    MAGIC_ITEMS_MAX,\n'
               '    sizeof(game.conf.magic_conf.power_cfgstats[0]),\n'
               '    game.conf.magic_conf.power_cfgstats,\n'
               '};')
    new_set = ('const struct NamedFieldSet magic_powers_named_fields_set = {\n'
               '    get_powers_count,\n'
               '    "power",\n'
               '    magic_powers_named_fields,\n'
               '    power_desc,\n'
               '    MAGIC_ITEMS_MAX,\n'
               '    sizeof(gpGame->conf.magic_conf.power_cfgstats[0]),\n'
               '    get_powers_base,\n'
               '};')
    assert old_set in content, "magic_powers_named_fields_set not found"
    content = content.replace(old_set, new_set)

    write_file(path, content)
    print("Modified config_magic.c")

# ===========================
# config_terrain.c (two arrays, two NamedFieldSets)
# ===========================
def modify_config_terrain_c():
    path = os.path.join(SRC, 'config_terrain.c')
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

    # --- Slab array ---
    slab_array_start = 'static const struct NamedField terrain_slab_named_fields[] = {'
    slab_nfs_start = '\nconst struct NamedFieldSet terrain_slab_named_fields_set = {'

    idx_slab_start = content.find(slab_array_start)
    assert idx_slab_start != -1

    slab_nfs_pos = content.find(slab_nfs_start)
    assert slab_nfs_pos != -1
    region = content[:slab_nfs_pos]
    last_close = region.rfind('};\n')
    idx_slab_end = last_close + len('};\n')

    slab_getter = ('static int32_t* get_slab_count(void) { return &game.conf.slab_conf.slab_types_count; }\n'
                   'static void* get_slab_base(void) { return game.conf.slab_conf.slab_cfgstats; }\n\n')

    content = (content[:idx_slab_start] +
               pragma_push +
               content[idx_slab_start:idx_slab_end] +
               pragma_pop + '\n' +
               slab_getter +
               content[idx_slab_end:])

    # Update slab NamedFieldSet
    old_slab_set = ('const struct NamedFieldSet terrain_slab_named_fields_set = {\n'
                    '    &game.conf.slab_conf.slab_types_count,\n'
                    '    "slab",\n'
                    '    terrain_slab_named_fields,\n'
                    '    slab_desc,\n'
                    '    TERRAIN_ITEMS_MAX,\n'
                    '    sizeof(game.conf.slab_conf.slab_cfgstats[0]),\n'
                    '    game.conf.slab_conf.slab_cfgstats,\n'
                    '};')
    new_slab_set = ('const struct NamedFieldSet terrain_slab_named_fields_set = {\n'
                    '    get_slab_count,\n'
                    '    "slab",\n'
                    '    terrain_slab_named_fields,\n'
                    '    slab_desc,\n'
                    '    TERRAIN_ITEMS_MAX,\n'
                    '    sizeof(gpGame->conf.slab_conf.slab_cfgstats[0]),\n'
                    '    get_slab_base,\n'
                    '};')
    assert old_slab_set in content, "terrain_slab_named_fields_set not found"
    content = content.replace(old_slab_set, new_slab_set)

    # --- Room array ---
    room_array_start = 'static const struct NamedField terrain_room_named_fields[] = {'
    room_nfs_start = '\nconst struct NamedFieldSet terrain_room_named_fields_set = {'

    idx_room_start = content.find(room_array_start)
    assert idx_room_start != -1

    room_nfs_pos = content.find(room_nfs_start)
    assert room_nfs_pos != -1
    region = content[:room_nfs_pos]
    last_close = region.rfind('};\n')
    idx_room_end = last_close + len('};\n')

    room_getter = ('static int32_t* get_room_count(void) { return &game.conf.slab_conf.room_types_count; }\n'
                   'static void* get_room_base(void) { return game.conf.slab_conf.room_cfgstats; }\n\n')

    content = (content[:idx_room_start] +
               pragma_push +
               content[idx_room_start:idx_room_end] +
               pragma_pop + '\n' +
               room_getter +
               content[idx_room_end:])

    # Update room NamedFieldSet
    old_room_set = ('const struct NamedFieldSet terrain_room_named_fields_set = {\n'
                    '    &game.conf.slab_conf.room_types_count,\n'
                    '    "room",\n'
                    '    terrain_room_named_fields,\n'
                    '    room_desc,\n'
                    '    TERRAIN_ITEMS_MAX,\n'
                    '    sizeof(game.conf.slab_conf.room_cfgstats[0]),\n'
                    '    game.conf.slab_conf.room_cfgstats,\n'
                    '};')
    new_room_set = ('const struct NamedFieldSet terrain_room_named_fields_set = {\n'
                    '    get_room_count,\n'
                    '    "room",\n'
                    '    terrain_room_named_fields,\n'
                    '    room_desc,\n'
                    '    TERRAIN_ITEMS_MAX,\n'
                    '    sizeof(gpGame->conf.slab_conf.room_cfgstats[0]),\n'
                    '    get_room_base,\n'
                    '};')
    assert old_room_set in content, "terrain_room_named_fields_set not found"
    content = content.replace(old_room_set, new_room_set)

    write_file(path, content)
    print("Modified config_terrain.c")

# ===========================
# Generic game-based config file handler
# ===========================
def modify_game_config_file(filename, arrays_info, namedfieldsets_info):
    """
    arrays_info: list of (array_start_text, nfs_start_text, getter_code)
    namedfieldsets_info: list of (old_set_text, new_set_text)
    """
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

    for (array_start, nfs_start, getter_code) in arrays_info:
        idx_start = content.find(array_start)
        assert idx_start != -1, f"Array start not found in {filename}: {array_start[:60]}"

        nfs_pos = content.find(nfs_start)
        assert nfs_pos != -1, f"NFS start not found in {filename}: {nfs_start[:60]}"

        region = content[:nfs_pos]
        last_close = region.rfind('};\n')
        idx_end = last_close + len('};\n')

        content = (content[:idx_start] +
                   pragma_push +
                   content[idx_start:idx_end] +
                   pragma_pop + '\n' +
                   getter_code +
                   content[idx_end:])

    for (old_set, new_set) in namedfieldsets_info:
        assert old_set in content, f"NamedFieldSet not found in {filename}:\n{old_set[:120]}"
        content = content.replace(old_set, new_set)

    write_file(path, content)
    print(f"Modified {filename}")

# ===========================
# config_trapdoor.c
# ===========================
def modify_config_trapdoor_c():
    # Read to get exact NamedFieldSet text
    path = os.path.join(SRC, 'config_trapdoor.c')
    content = read_file(path)

    arrays_info = [
        (
            'const struct NamedField trapdoor_door_named_fields[] = {',
            '\nconst struct NamedFieldSet trapdoor_door_named_fields_set = {',
            ('static int32_t* get_door_count(void) { return &game.conf.trapdoor_conf.door_types_count; }\n'
             'static void* get_door_base(void) { return game.conf.trapdoor_conf.door_cfgstats; }\n\n')
        ),
        (
            'const struct NamedField trapdoor_trap_named_fields[] = {',
            '\nconst struct NamedFieldSet trapdoor_trap_named_fields_set = {',
            ('static int32_t* get_trap_count(void) { return &game.conf.trapdoor_conf.trap_types_count; }\n'
             'static void* get_trap_base(void) { return game.conf.trapdoor_conf.trap_cfgstats; }\n\n')
        ),
    ]

    # Find exact NamedFieldSet text
    old_door = ('const struct NamedFieldSet trapdoor_door_named_fields_set = {\n'
                '    &game.conf.trapdoor_conf.door_types_count,\n'
                '    "door",\n'
                '    trapdoor_door_named_fields,\n'
                '    door_desc,\n'
                '    TRAPDOOR_TYPES_MAX,\n'
                '    sizeof(game.conf.trapdoor_conf.door_cfgstats[0]),\n'
                '    game.conf.trapdoor_conf.door_cfgstats,\n'
                '};')
    new_door = ('const struct NamedFieldSet trapdoor_door_named_fields_set = {\n'
                '    get_door_count,\n'
                '    "door",\n'
                '    trapdoor_door_named_fields,\n'
                '    door_desc,\n'
                '    TRAPDOOR_TYPES_MAX,\n'
                '    sizeof(gpGame->conf.trapdoor_conf.door_cfgstats[0]),\n'
                '    get_door_base,\n'
                '};')

    old_trap = ('const struct NamedFieldSet trapdoor_trap_named_fields_set = {\n'
                '    &game.conf.trapdoor_conf.trap_types_count,\n'
                '    "trap",\n'
                '    trapdoor_trap_named_fields,\n'
                '    trap_desc,\n'
                '    TRAPDOOR_TYPES_MAX,\n'
                '    sizeof(game.conf.trapdoor_conf.trap_cfgstats[0]),\n'
                '    game.conf.trapdoor_conf.trap_cfgstats,\n'
                '};')
    new_trap = ('const struct NamedFieldSet trapdoor_trap_named_fields_set = {\n'
                '    get_trap_count,\n'
                '    "trap",\n'
                '    trapdoor_trap_named_fields,\n'
                '    trap_desc,\n'
                '    TRAPDOOR_TYPES_MAX,\n'
                '    sizeof(gpGame->conf.trapdoor_conf.trap_cfgstats[0]),\n'
                '    get_trap_base,\n'
                '};')

    namedfieldsets_info = [(old_door, new_door), (old_trap, new_trap)]
    modify_game_config_file('config_trapdoor.c', arrays_info, namedfieldsets_info)

# ===========================
# config_objects.c
# ===========================
def modify_config_objects_c():
    arrays_info = [
        (
            'static const struct NamedField objects_named_fields[] = {',
            '\nconst struct NamedFieldSet objects_named_fields_set = {',
            ('static int32_t* get_objects_count(void) { return &game.conf.object_conf.object_types_count; }\n'
             'static void* get_objects_base(void) { return game.conf.object_conf.object_cfgstats; }\n\n')
        ),
    ]
    old_set = ('const struct NamedFieldSet objects_named_fields_set = {\n'
               '    &game.conf.object_conf.object_types_count,\n'
               '    "object",\n'
               '    objects_named_fields,\n'
               '    object_desc,\n'
               '    OBJECT_TYPES_MAX,\n'
               '    sizeof(game.conf.object_conf.object_cfgstats[0]),\n'
               '    game.conf.object_conf.object_cfgstats,\n'
               '};')
    new_set = ('const struct NamedFieldSet objects_named_fields_set = {\n'
               '    get_objects_count,\n'
               '    "object",\n'
               '    objects_named_fields,\n'
               '    object_desc,\n'
               '    OBJECT_TYPES_MAX,\n'
               '    sizeof(gpGame->conf.object_conf.object_cfgstats[0]),\n'
               '    get_objects_base,\n'
               '};')
    modify_game_config_file('config_objects.c', arrays_info, [(old_set, new_set)])

# ===========================
# config_crtrstates.c
# ===========================
def modify_config_crtrstates_c():
    # Need to read file to get exact NamedFieldSet text
    path = os.path.join(SRC, 'config_crtrstates.c')
    content = read_file(path)

    arrays_info = [
        (
            'static const struct NamedField crstates_states_named_fields[] = {',
            '\nconst struct NamedFieldSet crstates_states_named_fields_set = {',
            ('static int32_t* get_crstates_count(void) { return &game.conf.crtr_conf.states_count; }\n'
             'static void* get_crstates_base(void) { return game.conf.crtr_conf.states; }\n\n')
        ),
    ]
    old_set = ('const struct NamedFieldSet crstates_states_named_fields_set = {\n'
               '    &game.conf.crtr_conf.states_count,\n'
               '    "state",\n'
               '    crstates_states_named_fields,\n'
               '    creatrstate_desc,\n'
               '    CREATURE_STATES_MAX,\n'
               '    sizeof(game.conf.crtr_conf.states[0]),\n'
               '    game.conf.crtr_conf.states,\n'
               '};')
    new_set = ('const struct NamedFieldSet crstates_states_named_fields_set = {\n'
               '    get_crstates_count,\n'
               '    "state",\n'
               '    crstates_states_named_fields,\n'
               '    creatrstate_desc,\n'
               '    CREATURE_STATES_MAX,\n'
               '    sizeof(gpGame->conf.crtr_conf.states[0]),\n'
               '    get_crstates_base,\n'
               '};')
    modify_game_config_file('config_crtrstates.c', arrays_info, [(old_set, new_set)])

# ===========================
# config_effects.c
# ===========================
def modify_config_effects_c():
    arrays_info = [
        (
            'static const struct NamedField effects_effectgenerator_named_fields[] = {',
            '\nconst struct NamedFieldSet effects_effectgenerator_named_fields_set = {',
            ('static int32_t* get_effectgen_count(void) { return &game.conf.effects_conf.effectgen_cfgstats_count; }\n'
             'static void* get_effectgen_base(void) { return game.conf.effects_conf.effectgen_cfgstats; }\n\n')
        ),
    ]
    old_set = ('const struct NamedFieldSet effects_effectgenerator_named_fields_set = {\n'
               '    &game.conf.effects_conf.effectgen_cfgstats_count,\n'
               '    "effectGenerator",\n'
               '    effects_effectgenerator_named_fields,\n'
               '    effectgen_desc,\n'
               '    EFFECTSGEN_TYPES_MAX,\n'
               '    sizeof(game.conf.effects_conf.effectgen_cfgstats[0]),\n'
               '    game.conf.effects_conf.effectgen_cfgstats,\n'
               '};')
    new_set = ('const struct NamedFieldSet effects_effectgenerator_named_fields_set = {\n'
               '    get_effectgen_count,\n'
               '    "effectGenerator",\n'
               '    effects_effectgenerator_named_fields,\n'
               '    effectgen_desc,\n'
               '    EFFECTSGEN_TYPES_MAX,\n'
               '    sizeof(gpGame->conf.effects_conf.effectgen_cfgstats[0]),\n'
               '    get_effectgen_base,\n'
               '};')
    modify_game_config_file('config_effects.c', arrays_info, [(old_set, new_set)])

# ===========================
# config_cubes.c
# ===========================
def modify_config_cubes_c():
    arrays_info = [
        (
            'static const struct NamedField cubes_named_fields[] = {',
            '\nconst struct NamedFieldSet cubes_named_fields_set = {',
            ('static int32_t* get_cubes_count(void) { return &game.conf.cube_conf.cube_types_count; }\n'
             'static void* get_cubes_base(void) { return game.conf.cube_conf.cube_cfgstats; }\n\n')
        ),
    ]
    old_set = ('const struct NamedFieldSet cubes_named_fields_set = {\n'
               '    &game.conf.cube_conf.cube_types_count,\n'
               '    "cube",\n'
               '    cubes_named_fields,\n'
               '    cube_desc,\n'
               '    CUBE_ITEMS_MAX,\n'
               '    sizeof(game.conf.cube_conf.cube_cfgstats[0]),\n'
               '    game.conf.cube_conf.cube_cfgstats,\n'
               '};')
    new_set = ('const struct NamedFieldSet cubes_named_fields_set = {\n'
               '    get_cubes_count,\n'
               '    "cube",\n'
               '    cubes_named_fields,\n'
               '    cube_desc,\n'
               '    CUBE_ITEMS_MAX,\n'
               '    sizeof(gpGame->conf.cube_conf.cube_cfgstats[0]),\n'
               '    get_cubes_base,\n'
               '};')
    modify_game_config_file('config_cubes.c', arrays_info, [(old_set, new_set)])

# ===========================
# config_compp.c
# ===========================
def modify_config_compp_c():
    path = os.path.join(SRC, 'config_compp.c')
    content = read_file(path)

    def field_replacer(m):
        inner = m.group(1)
        result = transform_compp_field_arg(inner)
        if result is not None:
            return result
        return m.group(0)
    content = re.sub(r'field\(([^)]+)\)', field_replacer, content)

    # Add getter functions before the NamedFieldSets
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

    # Insert getters before the first NamedFieldSet
    first_nfs = '\nconst struct NamedFieldSet compp_common_named_fields_set = {'
    idx = content.find(first_nfs)
    assert idx != -1, "compp_common_named_fields_set not found"
    content = content[:idx] + '\n' + getters + content[idx+1:]

    # Update NamedFieldSets
    old_common = ('const struct NamedFieldSet compp_common_named_fields_set = {\n'
                  '  NULL,\n'
                  '  "common",\n'
                  '  compp_common_named_fields,\n'
                  '  NULL,\n'
                  '  0,\n'
                  '  0,\n'
                  '  NULL,\n'
                  '};')
    new_common = ('const struct NamedFieldSet compp_common_named_fields_set = {\n'
                  '  NULL,\n'
                  '  "common",\n'
                  '  compp_common_named_fields,\n'
                  '  NULL,\n'
                  '  0,\n'
                  '  0,\n'
                  '  get_compp_common_base,\n'
                  '};')
    assert old_common in content, "compp_common_named_fields_set not found"
    content = content.replace(old_common, new_common)

    old_process = ('const struct NamedFieldSet compp_process_named_fields_set = {\n'
                   '  &comp_player_conf.processes_count,\n'
                   '  "process",\n'
                   '  compp_process_named_fields,\n'
                   '  NULL,\n'
                   '  COMPUTER_PROCESS_TYPES_COUNT,\n'
                   '  sizeof(comp_player_conf.process_types[0]),\n'
                   '  comp_player_conf.process_types,\n'
                   '};')
    new_process = ('const struct NamedFieldSet compp_process_named_fields_set = {\n'
                   '  get_processes_count,\n'
                   '  "process",\n'
                   '  compp_process_named_fields,\n'
                   '  NULL,\n'
                   '  COMPUTER_PROCESS_TYPES_COUNT,\n'
                   '  sizeof(comp_player_conf.process_types[0]),\n'
                   '  get_processes_base,\n'
                   '};')
    assert old_process in content, "compp_process_named_fields_set not found"
    content = content.replace(old_process, new_process)

    old_check = ('const struct NamedFieldSet compp_check_named_fields_set = {\n'
                 '  &comp_player_conf.checks_count,\n'
                 '  "check",\n'
                 '  compp_check_named_fields,\n'
                 '  NULL,\n'
                 '  COMPUTER_CHECKS_TYPES_COUNT,\n'
                 '  sizeof(comp_player_conf.check_types[0]),\n'
                 '  comp_player_conf.check_types,\n'
                 '};')
    new_check = ('const struct NamedFieldSet compp_check_named_fields_set = {\n'
                 '  get_checks_count,\n'
                 '  "check",\n'
                 '  compp_check_named_fields,\n'
                 '  NULL,\n'
                 '  COMPUTER_CHECKS_TYPES_COUNT,\n'
                 '  sizeof(comp_player_conf.check_types[0]),\n'
                 '  get_checks_base,\n'
                 '};')
    assert old_check in content, "compp_check_named_fields_set not found"
    content = content.replace(old_check, new_check)

    old_event = ('const struct NamedFieldSet compp_event_named_fields_set = {\n'
                 '  &comp_player_conf.events_count,\n'
                 '  "event",\n'
                 '  compp_event_named_fields,\n'
                 '  NULL,\n'
                 '  COMPUTER_EVENTS_TYPES_COUNT,\n'
                 '  sizeof(comp_player_conf.event_types[0]),\n'
                 '  comp_player_conf.event_types,\n'
                 '};')
    new_event = ('const struct NamedFieldSet compp_event_named_fields_set = {\n'
                 '  get_events_count,\n'
                 '  "event",\n'
                 '  compp_event_named_fields,\n'
                 '  NULL,\n'
                 '  COMPUTER_EVENTS_TYPES_COUNT,\n'
                 '  sizeof(comp_player_conf.event_types[0]),\n'
                 '  get_events_base,\n'
                 '};')
    assert old_event in content, "compp_event_named_fields_set not found"
    content = content.replace(old_event, new_event)

    old_computer = ('const struct NamedFieldSet compp_computer_named_fields_set = {\n'
                    '  &comp_player_conf.computers_count,\n'
                    '  "computer",\n'
                    '  compp_computer_named_fields,\n'
                    '  NULL,\n'
                    '  COMPUTER_MODELS_COUNT,\n'
                    '  sizeof(comp_player_conf.computer_types[0]),\n'
                    '  comp_player_conf.computer_types,\n'
                    '};')
    new_computer = ('const struct NamedFieldSet compp_computer_named_fields_set = {\n'
                    '  get_computers_count,\n'
                    '  "computer",\n'
                    '  compp_computer_named_fields,\n'
                    '  NULL,\n'
                    '  COMPUTER_MODELS_COUNT,\n'
                    '  sizeof(comp_player_conf.computer_types[0]),\n'
                    '  get_computers_base,\n'
                    '};')
    assert old_computer in content, "compp_computer_named_fields_set not found"
    content = content.replace(old_computer, new_computer)

    write_file(path, content)
    print("Modified config_compp.c")

# ===========================
# config_lenses.c
# ===========================
def modify_config_lenses_c():
    path = os.path.join(SRC, 'config_lenses.c')
    content = read_file(path)

    def field_replacer(m):
        inner = m.group(1)
        result = transform_lenses_field_arg(inner)
        if result is not None:
            return result
        return m.group(0)
    content = re.sub(r'field\(([^)]+)\)', field_replacer, content)

    # Add getter functions before NamedFieldSet
    getters = ('static int32_t* get_lenses_count(void) { return &lenses_conf.lenses_count; }\n'
               'static void* get_lenses_base(void) { return lenses_conf.lenses; }\n\n')
    nfs_pos = content.find('\nconst struct NamedFieldSet lenses_data_named_fields_set = {')
    assert nfs_pos != -1
    content = content[:nfs_pos] + '\n' + getters + content[nfs_pos+1:]

    # Update NamedFieldSet
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

    # Fix value_pallete function: uses named_field->field as pointer
    old_pallete = ('    if (LbFileLoadAt(fname, (char*)(named_field->field) + named_fields_set->struct_size * idx) != PALETTE_SIZE)')
    new_pallete = ('    if (LbFileLoadAt(fname, (char*)named_fields_set->get_struct_base() + named_fields_set->struct_size * idx + (ptrdiff_t)named_field->field) != PALETTE_SIZE)')
    assert old_pallete in content, "value_pallete not found in config_lenses.c"
    content = content.replace(old_pallete, new_pallete)

    write_file(path, content)
    print("Modified config_lenses.c")

# ===========================
# Main
# ===========================
if __name__ == '__main__':
    print("Starting transformations...")
    modify_config_h()
    modify_config_c()
    modify_config_rules_c()
    modify_config_magic_c()
    modify_config_terrain_c()
    modify_config_trapdoor_c()
    modify_config_objects_c()
    modify_config_crtrstates_c()
    modify_config_effects_c()
    modify_config_cubes_c()
    modify_config_compp_c()
    modify_config_lenses_c()
    print("All transformations complete!")
