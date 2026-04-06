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

#include "bflib_video.h"   /* TbPixel */

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

/** Call immediately after load_texture_map_file() to discard the cached GPU tile
 *  atlas so it is rebuilt on the next frame with fresh block_mem data.  Safe to
 *  call at any time; no-op if no GL renderer is active. */
void RendererNotifyTexturesReloaded(void);

/** Call immediately after LoadVRes256Data() / LoadMcgaData() to rebuild the
 *  sprite atlas with the freshly-loaded gui_panel_sprites / button_sprites.
 *  Safe to call at any time; no-op if no GL renderer is active. */
void RendererNotifySpritesReloaded(void);

/** Append per-level custom_sprites into the live atlas after init_custom_sprites().
 *  Does NOT reinit the atlas — existing gui_panel_sprites / button_sprites entries
 *  are preserved.  Safe to call whenever custom_sprites is rebuilt. */
void RendererNotifyCustomSpritesReloaded(void);

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

/** Submit a keeper-sprite (creature/object) for GPU rendering during the bucket walk.
 *  Returns 1 if the GPU handled it (CPU blit should be skipped), 0 to fall back. */
int WorldViewRenderer_SubmitKeeperSprite(long dst_x, long dst_y, long dst_w, long dst_h,
                                         const unsigned char* data, int src_w, int src_h,
                                         unsigned int draw_flags, const unsigned char* remap);

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
/* C-callable UI renderer wrappers                                            */
/******************************************************************************/

struct Thing;

void UIRenderer_SubmitSlabSelector(int x1, int y1, int x2, int y2, unsigned char color, float z_depth);
/** Submit a power-hand keeper sprite; deferred in OpenGL mode until after glClear(). */
void UIRenderer_SubmitKeeperSprite(short x, short y, unsigned short kspr_base,
                                   short kspr_angle, unsigned char sprgroup, long scale);

/** Submit a panel sprite (gui_panel_sprites) at screen-left alignment.
 *  Resolves player coloring for spridx and submits GPU quad; immediate in software mode. */
void UIRenderer_SubmitPanelSprite(long x, long y, int units_per_px, long spridx);

/** Submit a panel sprite from a pre-resolved TbSprite pointer.
 *  Use when the caller has already called get_panel_sprite(); avoids a second lookup. */
struct TbSprite;
void UIRenderer_SubmitPanelSpriteRaw(long x, long y, int units_per_px, const struct TbSprite* spr);

/** Submit a panel sprite centered on (x, y). */
void UIRenderer_SubmitPanelSpriteCentered(long x, long y, int units_per_px, long spridx);

/** Submit a button sprite (button_sprites) at screen-left alignment. */
void UIRenderer_SubmitButtonSprite(long x, long y, int units_per_px, short spridx);

/** Submit a button sprite horizontally flipped.
 *  In GPU mode: swaps U texture coordinates for a mirror effect.
 *  In software mode: temporarily sets Lb_SPRITE_FLIP_HORIZ before blitting. */
void UIRenderer_SubmitButtonSpriteFlipped(long x, long y, int units_per_px, short spridx);

/** Submit a sprite with explicit pixel dimensions to the GPU batch.
 *  Called by game-logic hooks (draw_status_sprites, draw_engine_number, etc.)
 *  instead of LbSpriteDrawScaled when the GPU renderer is active. */
void UIRenderer_SubmitScaledSprite(long x, long y, long w, long h, const struct TbSprite *spr);

/** Submit a solid-color rectangle to the GPU batch.
 *  Called instead of LbDrawBox when the GPU renderer is active.
 *  color_idx is a DK palette index (0-255). */
void UIRenderer_SubmitSolidBox(long x, long y, long w, long h, unsigned char color_idx);

/** Acquire the renderer's minimap pixel buffer for this frame.
 *  Returns a renderer-owned size×size buffer (GPU mode) that the caller fills
 *  with palette indices (0 = transparent), or NULL (software mode) in which case
 *  the caller writes directly to lbDisplay.WScreen at the minimap position.
 *  Call UIRenderer_SubmitMinimap() once drawing into the buffer is complete. */
unsigned char* UIRenderer_AcquireMinimapBuffer(int size);

/** Finalise the minimap submitted via UIRenderer_AcquireMinimapBuffer().
 *  In GL mode: uploads the buffer to a GL_R8 texture and queues a palette-lookup quad.
 *  In software mode: no-op (pixels were already in WScreen). */
void UIRenderer_SubmitMinimap(int screen_x, int screen_y, int size);

/** Submit a TiledSprite (like the status panel) through the UI renderer.
 *  Iterates tiles in the same order as LbTiledSpriteDraw, resolving each sprite
 *  to a SpriteHandle and calling SubmitScaledSprite.  Replaces LbTiledSpriteDraw
 *  for GPU-routed rendering. */
struct TiledSprite;
void UIRenderer_SubmitTiledSprite(long x, long y, int units_per_px, const struct TiledSprite* bigspr);

void UIRenderer_SetLayer(int layer);
void UIRenderer_FlushBack(void);
void UIRenderer_FlushFront(void);
void UIRenderer_Flush(void);
void UIRenderer_Clear(void);

/** Returns non-zero when the active UI renderer is GPU-accelerated.
 *  Use to skip CPU-only fallback paths (e.g. LbSpriteDrawResized with DrawFlags) 
 *  that are redundant when GPU sprites are already submitted. */
TbBool UIRenderer_IsGpuActive(void);

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
/* C++ only: direct access to the active IUIRenderer* */
IUIRenderer* RendererGetUIRenderer();
#endif
#endif // RENDERER_MANAGER_H
