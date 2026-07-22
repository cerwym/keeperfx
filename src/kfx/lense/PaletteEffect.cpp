/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file PaletteEffect.cpp
 *     Palette color change lens effect implementation.
 * @par Purpose:
 *     Color palette modification effect.
 * @par Comment:
 *     None.
 * @author   Peter Lockett, KeeperFX Team
 * @date     09 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "../../pre_inc.h"
#include "PaletteEffect.h"

#include "../../config_lenses.h"
#include "../../player_data.h"
#include "../../game_legacy.h"

#include "../../keeperfx.hpp"
#include "../../post_inc.h"

/******************************************************************************/

PaletteEffect::PaletteEffect()
    : LensEffect(LensEffectType::Palette, "Palette")
    , m_current_lens(-1)
{
}

PaletteEffect::~PaletteEffect()
{
    Cleanup();
}

TbBool PaletteEffect::Setup(long lens_idx)
{
    SYNCDBG(8, "Setting up palette effect for lens %ld", lens_idx);
    
    struct LensConfig* cfg = &lenses_conf.lenses[lens_idx];
    struct PlayerInfo* player = get_my_player();
    
    // Register the lens palette as the player's active lens palette, then apply it.
    // The original engine's set_lens_palette() set BOTH the applied palette and the
    // lens palette; the C++ migration dropped the apply step, so palette lenses never
    // reached the hardware/GL palette upload. PaletteSetPlayerPalette() folds the lens
    // palette into main_palette and uploads it (the (pal == lens_palette) branch of its
    // condition), which is what actually tints the world (and, on software, the UI too).
    // Do NOT assign main_palette by hand here — that breaks PaletteSetPlayerPalette()'s
    // (pal != main_palette) guard and the upload is skipped.
    player->lens_palette = cfg->palette;
    PaletteSetPlayerPalette(player, cfg->palette);
    
    m_current_lens = lens_idx;
    SYNCDBG(7, "Palette effect ready");
    return true;
}

void PaletteEffect::Cleanup()
{
    if (m_current_lens >= 0) {
        struct PlayerInfo* player = get_my_player();
        // Clear the lens palette first so PaletteSetPlayerPalette()'s (lens_palette == 0)
        // branch is taken and the base engine palette is folded back in and re-uploaded.
        player->lens_palette = NULL;
        PaletteSetPlayerPalette(player, engine_palette);
        m_current_lens = -1;
        SYNCDBG(9, "Palette effect cleaned up");
    }
}

TbBool PaletteEffect::Draw(LensRenderContext* ctx)
{
    // Palette is a side-channel effect: Setup() sets player->lens_palette, and
    // PaletteSetPlayerPalette() applies it each frame via the normal palette upload.
    // There is no pixel-buffer draw step — return false so LensManager::Draw()
    // executes the standard copy fallback.
    (void)ctx;
    return false;
}

/******************************************************************************/
