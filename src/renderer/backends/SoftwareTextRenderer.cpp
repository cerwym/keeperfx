/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareTextRenderer.cpp
 *     CPU software implementation of ITextRenderer.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/backends/SoftwareTextRenderer.h"

#include "bflib_sprfnt.h"
#include "post_inc.h"

/******************************************************************************/

TbBool SoftwareTextRenderer::DrawTextResized(int posx, int posy, int units_per_px, const char* text)
{
    return LbTextDrawResized_sw(posx, posy, units_per_px, text);
}
