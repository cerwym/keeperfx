/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file FrameState.h
 *     Per-frame snapshot of game-thread globals used by the render thread.
 * @par Purpose:
 *     RendererOpenGL runs a dedicated render thread.  Several game-logic globals
 *     (palette, tint values, lens mode) are written by the game thread and read
 *     by the render thread inside EndFrame_GL().  Without snapshotting, those
 *     reads race against game-thread writes that occur concurrently once
 *     FlipBuffers() returns.
 *
 *     FrameState is a plain-old-data struct captured inside FlipBuffers() (under
 *     the frame mutex) before the render thread is signalled.  EndFrame_GL() reads
 *     exclusively from the snapshot, eliminating all such races.
 *
 * @par Usage:
 *     - Captured once per frame in RendererOpenGL::FlipBuffers() as m_rt_frame_state.
 *     - All render-thread code must read from m_rt_frame_state, never from the
 *       original globals.
 */
/******************************************************************************/
#pragma once

#include <cstdint>

/******************************************************************************/

struct FrameState
{
    /** Copy of lbPalette[768]: 256 entries of (R,G,B) 6-bit values. */
    uint8_t palette[768];

    /** Copy of g_palette_possession_tint at frame-submit time.
     *  Controls the alpha of the palette-shift overlay during possession. */
    float   possession_tint;

    /** Copy of g_screen_tint[4] at frame-submit time (RGBA floats).
     *  Drives the fullscreen colour overlay (possession, death-flash, etc.). */
    float   screen_tint[4];

    /** Copy of ::lens_mode at frame-submit time.
     *  2 = GPU lens pass active; other values = no GPU lens redirect. */
    int     lens_mode;

    /** Physical screen dimensions snapshotted from m_screenW/H at flip time.
     *  Avoids a race where BeginFrame() (game thread) updates m_screenW/H for
     *  the next frame while EndFrame_GL() (render thread) is still using the
     *  current frame's values. */
    int     screen_w;
    int     screen_h;
};

/******************************************************************************/
