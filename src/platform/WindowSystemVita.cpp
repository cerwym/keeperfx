/******************************************************************************/
// Dungeon Keeper - Platform Abstraction Layer
/******************************************************************************/
/** @file WindowSystemVita.cpp
 *     PS Vita window-system implementation of IWindowSystem.
 * @par Purpose:
 *     On Vita there is no OS windowing system.  This class bridges the gap
 *     between the virtual cursor maintained by input_vita.c (analog stick +
 *     touchscreen) and the engine's mouse handler by injecting MOUSEMOVE
 *     events each frame via PollInput().
 */
/******************************************************************************/
#ifdef PLATFORM_VITA

#include "kfx_memory.h"
#include "pre_inc.h"
#include "platform/WindowSystemVita.h"
#include "bflib_planar.h"
#include "bflib_mouse.h"
#include "bflib_video.h"
#include "post_inc.h"

// Exposed by input_vita.c — returns the current virtual cursor position
// maintained by analog-stick and front-touchscreen logic.
extern "C" void vita_get_virtual_cursor(int* x, int* y);

// mouseControl() is the engine entry-point for mouse movement events.
// Declared in bflib_mouse.h / bflib_inputctrl (C linkage).
extern "C" void mouseControl(unsigned int action, struct TbPoint* pos);

// Action code for mouse movement — matches MActn_MOUSEMOVE in bflib_mouse.h.
#ifndef MActn_MOUSEMOVE
#define MActn_MOUSEMOVE 0
#endif

/******************************************************************************/

void WindowSystemVita::PollInput()
{
    int newX = 0, newY = 0;
    vita_get_virtual_cursor(&newX, &newY);

    // Compute delta from current engine cursor position so that the pointer
    // handler's absolute clamp logic works correctly.
    struct TbPoint delta;
    delta.x = newX - lbDisplay.MMouseX;
    delta.y = newY - lbDisplay.MMouseY;

    if (delta.x != 0 || delta.y != 0)
    {
        mouseControl(MActn_MOUSEMOVE, &delta);
    }
}

#endif // PLATFORM_VITA
