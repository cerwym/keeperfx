/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file FxSprAnimSelect.h
 *     C-callable direction selector bridge for creature sprite facing.
 */
/******************************************************************************/
#ifndef KEEPERFX_KFX_ASSETS_FXSPRANIMSELECT_H
#define KEEPERFX_KFX_ASSETS_FXSPRANIMSELECT_H

#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Select creature direction group for an angle; writes whether it should be
 *  mirrored to out_mirror (non-null). Returns direction group index. */
int kfx_anim_select_dir_group(int angle, int* out_mirror);

#ifdef __cplusplus
}
#endif

#endif // KEEPERFX_KFX_ASSETS_FXSPRANIMSELECT_H

