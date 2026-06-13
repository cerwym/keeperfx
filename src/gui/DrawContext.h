/******************************************************************************/
// KeeperFX — GUI Layer
/******************************************************************************/
/** @file DrawContext.h
 *     Immutable per-frame snapshot of screen/input globals.
 *
 *     Captured once at the start of each frame by SceneManager before any
 *     scene draws.  Passed by const-ref down to every IScene and IView so
 *     that no draw code ever touches RendererGetScreenWidth(), pixel_size, GetMouseX()
 *     etc. directly.  This is the single crossing-point where C globals
 *     enter the new C++ GUI layer.
 */
/******************************************************************************/
#pragma once

#ifdef __cplusplus

struct DrawContext
{
    int screen_w;           ///< RendererGetScreenWidth()  (physical pixels)
    int screen_h;           ///< RendererGetScreenHeight() (physical pixels)
    int pixel_size;         ///< Logical-to-physical scale factor (usually 1)
    int units_per_pixel;    ///< Sprite scaling factor for current resolution
    int mouse_x;            ///< GetMouseX() — physical pixel coordinate
    int mouse_y;            ///< GetMouseY() — physical pixel coordinate

    /// Capture all values from globals.  Call once per frame, before drawing.
    static DrawContext capture();
};

#endif // __cplusplus
