/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererManager.h
 *     Renderer backend registration and lifecycle management.
 * @par Purpose:
 *     Manages the active IRenderer backend. Provides functions to initialise,
 *     switch, and shut down renderer backends. The rest of the codebase
 *     interacts with the renderer exclusively through this module.
 *
 *     C++ code may use RendererGetActive() for full interface access.
 *     C code (e.g. bflib_video.c) uses the thin C-callable wrappers below.
 */
/******************************************************************************/
#ifndef RENDERER_MANAGER_H
#define RENDERER_MANAGER_H

/* IRenderer.h and RendererType are only visible in C++ translation units */
#ifdef __cplusplus
#  include "IRenderer.h"
#  include "IWorldViewRenderer.h"
#  include "IMapFadePass.h"
#  include "ITextRenderer.h"
#else
/* In C translation units, RendererType is an opaque int */
typedef int RendererType;
#  define RENDERER_INVALID  (-1)
#  define RENDERER_AUTO     0
#  define RENDERER_SOFTWARE 1
#  define RENDERER_OPENGL   2
#  define RENDERER_VITA     3
#  define RENDERER_3DS      4
#endif

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

/** Initialise the renderer subsystem with the requested backend type. */
int  RendererInit(RendererType type);

/** Switch to a different renderer backend at runtime. */
int  RendererSwitch(RendererType type);

/** Shut down the active renderer backend and release all resources. */
void RendererShutdown(void);

/** Returns the RendererType enum value currently in use. */
RendererType RendererGetActiveType(void);

/******************************************************************************/
/* C-callable framebuffer / frame wrappers (safe to call from bflib_video.c) */
/******************************************************************************/

/** Lock the framebuffer for CPU writes.
 *  @param out_pitch  Receives the row pitch in bytes.
 *  @return Pointer to the framebuffer, or NULL on failure. */
unsigned char* RendererLockFramebuffer(int* out_pitch);

/** Unlock the framebuffer after CPU writes are complete. */
void RendererUnlockFramebuffer(void);

/** Called at the start of each frame. Returns non-zero if the frame should proceed. */
int RendererBeginFrame(void);

/** Present the completed frame to the display. */
void RendererEndFrame(void);

/******************************************************************************/
/* C-callable world-view renderer wrappers (safe to call from C files)        */
/******************************************************************************/

struct Camera; // forward declaration (defined in game_legacy.h)

/** Bind the target framebuffer and configure the software rasterizer.
 *  Call this once per view before adding geometry to the bucket list.
 *  vp_x/vp_y are the viewport's top-left corner in screen pixels (0,0 when
 *  no sidebar is present). */
void WorldViewRenderer_BeginWorldPass(unsigned char* framebuf, int pitch, int w, int h,
                                      int vp_x, int vp_y);

/** Flush the isometric/1st-person bucket list to the framebuffer.
 *  Call this after draw_view() has filled the bucket list. */
void WorldViewRenderer_FlushIsometricView(void);

/** Flush the front-view bucket list to the framebuffer.
 *  Call this after the front-view geometry has been added to the bucket list. */
void WorldViewRenderer_FlushFrontView(struct Camera* cam);

/******************************************************************************/
/* C-callable map fade pass wrappers                                          */
/******************************************************************************/

/** Render one fade-in step (parchment → 3D view) and return next step value. */
long MapFadePass_StepFadeIn(long step);

/** Render one fade-out step (3D view → parchment) and return next step value. */
long MapFadePass_StepFadeOut(long step);

/******************************************************************************/
/* C-callable text renderer wrapper                                           */
/******************************************************************************/

/** Draw text at (posx, posy) with the given scale through the active ITextRenderer.
 *  GPU backends queue the draw; call TextRenderer_Flush() to emit it. */
TbBool TextRenderer_DrawTextResized(int posx, int posy, int units_per_px, const char* text);

/** Flush all deferred text draws to the framebuffer.
 *  Must be called after the staging-buffer blit quad and before buffer swap. */
void TextRenderer_Flush(void);

/******************************************************************************/
/* C-callable raw framebuffer blit                                            */
/******************************************************************************/

/**
 * Blit a RAW8 source image into the renderer's active framebuffer.
 * The destination buffer, scanline, and screen dimensions are supplied by the renderer.
 * Equivalent to copy_raw8_image_buffer(lbDisplay.WScreen, ...) but renderer-routed.
 */
TbBool RendererBlitRaw8(int dst_width, int dst_height, int dst_x, int dst_y,
                        const unsigned char* src_buf, int src_width, int src_height);

/******************************************************************************/
#ifdef __cplusplus
}
/* C++ only: direct access to the active IRenderer* */
IRenderer* RendererGetActive();
/* C++ only: direct access to the active IWorldViewRenderer* */
IWorldViewRenderer* RendererGetWorldViewRenderer();
/* C++ only: direct access to the active IMapFadePass* */
IMapFadePass* RendererGetMapFadePass();
/* C++ only: direct access to the active ITextRenderer* */
ITextRenderer* RendererGetTextRenderer();
#endif
#endif // RENDERER_MANAGER_H
