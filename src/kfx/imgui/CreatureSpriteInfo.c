/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file CreatureSpriteInfo.c
 *     Read-only accessors over the creature graphics config — implementation.
 */
/******************************************************************************/
#include "pre_inc.h"

#include "kfx/imgui/CreatureSpriteInfo.h"

#include "creature_control.h"   // CREATURE_TYPES_MAX
#include "creature_graphics.h"  // CREATURE_GRAPHICS_INSTANCES, keepersprite_*
#include "config_creature.h"    // creature_code_name()
#include "game_legacy.h"        // game.conf.crtr_conf.creature_graphics

#include "post_inc.h"
/******************************************************************************/

static const char* const dbg_action_names[CREATURE_GRAPHICS_INSTANCES] = {
    "Stand", "Ambulate", "Drag", "Attack", "Dig",
    "Smoke", "Relax", "PrettyDance", "GotHit", "PowerGrab",
    "GotSlapped", "Celebrate", "Sleep", "EatChicken", "Torture",
    "Scream", "DropDead", "DeadSplat", "Roar", "QuerySymbol",
    "HandSymbol", "Piss", "CastSpell", "RangedAttack", "Custom",
};

int dbg_creature_model_count(void)
{
    return CREATURE_TYPES_MAX;
}

const char* dbg_creature_name(int crmodel)
{
    if (crmodel < 0 || crmodel >= CREATURE_TYPES_MAX)
        return NULL;
    return creature_code_name((ThingModel)crmodel);
}

int dbg_creature_has_graphics(int crmodel)
{
    if (crmodel < 0 || crmodel >= CREATURE_TYPES_MAX)
        return 0;
    for (int a = 0; a < CREATURE_GRAPHICS_INSTANCES; a++)
    {
        if (game.conf.crtr_conf.creature_graphics[crmodel][a] >= 0)
            return 1;
    }
    return 0;
}

int dbg_creature_action_count(void)
{
    return CREATURE_GRAPHICS_INSTANCES;
}

const char* dbg_action_name(int action)
{
    if (action < 0 || action >= CREATURE_GRAPHICS_INSTANCES)
        return "?";
    return dbg_action_names[action];
}

int dbg_creature_action_anim(int crmodel, int action)
{
    if (crmodel < 0 || crmodel >= CREATURE_TYPES_MAX)
        return -1;
    if (action < 0 || action >= CREATURE_GRAPHICS_INSTANCES)
        return -1;
    return game.conf.crtr_conf.creature_graphics[crmodel][action];
}

int dbg_anim_base_index(int anim)
{
    if (anim < 0)
        return -1;
    return (int)keepersprite_index((unsigned short)anim);
}

int dbg_anim_frames(int anim)
{
    if (anim < 0)
        return 0;
    return (int)keepersprite_frames((unsigned short)anim);
}

int dbg_anim_rotable(int anim)
{
    if (anim < 0)
        return 0;
    return (int)keepersprite_rotable((unsigned short)anim);
}

int dbg_anim_rot_groups(int rotable)
{
    // Draw path (process_keeper_sprite_with_remap) stores:
    //   rotable 0 -> 1 group  (FramesCount entries)
    //   rotable 2 -> 5 groups (FramesCount*5 entries; angles 0..180, rest mirrored)
    // rotable 1 is not addressed by that path; treat as a single group.
    return (rotable == 2) ? 5 : 1;
}
/******************************************************************************/
