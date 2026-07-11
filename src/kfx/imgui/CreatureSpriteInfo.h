/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file CreatureSpriteInfo.h
 *     Read-only accessors over the (static-after-load) creature graphics config,
 *     for the ImGui creature-sprite debug viewer.
 *
 *     These wrap engine globals that are written only at config/level load, so
 *     they are safe to read from the render-thread debug overlay. Keeping them
 *     behind a C shim keeps the C++ panel free of the engine's C headers.
 */
/******************************************************************************/
#ifndef KEEPERFX_DEBUG_CREATURE_SPRITE_INFO_H
#define KEEPERFX_DEBUG_CREATURE_SPRITE_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

/** Number of creature model slots (CREATURE_TYPES_MAX). */
int dbg_creature_model_count(void);

/** Code name for a creature model, or NULL if out of range. */
const char* dbg_creature_name(int crmodel);

/** Non-zero if this model has at least one valid graphics animation. */
int dbg_creature_has_graphics(int crmodel);

/** Number of per-creature animation/action slots (CREATURE_GRAPHICS_INSTANCES). */
int dbg_creature_action_count(void);

/** Human-readable action name (Stand, Walk, Attack, ...), never NULL. */
const char* dbg_action_name(int action);

/** Raw animation id for (creature, action), or -1 when none is assigned. */
int dbg_creature_action_anim(int crmodel, int action);

/** Base keepsprite index for an animation id. */
int dbg_anim_base_index(int anim);

/** Frame count of an animation id (per rotation group). */
int dbg_anim_frames(int anim);

/** Rotable flag of an animation id: 0=flat, 1=omni(5 groups), 2=8-dir mirrored. */
int dbg_anim_rotable(int anim);

/** Number of rotation groups implied by a rotable value (1, 5 or 8). */
int dbg_anim_rot_groups(int rotable);

/** Per-frame pivot offset + frame box for animation `anim`, `rel_index` frames
 *  into its contiguous run (rel_index = group*frames + frame). Fills any non-NULL
 *  out params. Returns 1 on success, 0 if the animation is invalid. */
int dbg_anim_frame_offset(int anim, int rel_index,
                          int* ox, int* oy, int* fw, int* fh);

#ifdef __cplusplus
}
#endif

#endif // KEEPERFX_DEBUG_CREATURE_SPRITE_INFO_H
