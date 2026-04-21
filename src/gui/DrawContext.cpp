/******************************************************************************/
// KeeperFX — GUI Layer
/******************************************************************************/
/** @file DrawContext.cpp
 *     DrawContext::capture() — reads globals, returns value.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "gui/DrawContext.h"
#include "bflib_video.h"    // MyScreenWidth, MyScreenHeight, pixel_size, units_per_pixel
#include "kjm_input.h"      // GetMouseX, GetMouseY

DrawContext DrawContext::capture()
{
    DrawContext ctx;
    ctx.screen_w        = (int)MyScreenWidth;
    ctx.screen_h        = (int)MyScreenHeight;
    ctx.pixel_size      = (int)::pixel_size;
    ctx.units_per_pixel = (int)::units_per_pixel;
    ctx.mouse_x         = (int)GetMouseX();
    ctx.mouse_y         = (int)GetMouseY();
    return ctx;
}
