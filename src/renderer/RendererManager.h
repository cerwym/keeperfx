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

#include "bflib_video.h"          /* TbPixel */
#include "renderer/RendererSettings.h" /* RendererSettings, g_renderer_settings */
#include <stdint.h>                /* int32_t */

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

/** Append pointer_sprites into the live atlas after load_pointer_file().
 *  pointer_sprites are loaded separately from the main GUI sprite sheets,
 *  so they must be registered after loading completes. */
void RendererNotifyPointerSpritesLoaded(void);

/** Append frontend_sprite into the live atlas after frontend_load_data().
 *  frontend_sprite is loaded independently from the main GUI sprite sheets
 *  and must be registered so UIRenderer_SubmitPanelSpriteRaw can resolve
 *  handles for frontend menu / button sprites. */
void RendererNotifyFrontendSpritesLoaded(void);

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

/** Returns non-zero when the active world-view renderer is GPU-accelerated.
 *  Use to skip CPU staging-buffer writes (lens effects, swipe, front-view
 *  rasterisation) that would pollute the GPU-composited frame. */
TbBool WorldViewRenderer_IsGpuActive(void);

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
/* C-callable text renderer wrappers                                          */
/******************************************************************************/

struct TbSpriteSheet;

/** Set the active font for subsequent text operations. */
void TextRenderer_SetFont(const struct TbSpriteSheet* font);

/** Return the active Western font, or NULL if none has been set. */
const struct TbSpriteSheet* TextRenderer_GetFont(void);

/** Set both justify and clip windows to the same rectangle. */
void TextRenderer_SetWindow(int32_t x, int32_t y, int32_t w, int32_t h);

/** Set the justify window (origin + width for word-wrap). */
void TextRenderer_SetJustifyWindow(int32_t x, int32_t y, int32_t w);

/** Set the clip window (visible rectangle for text clipping). */
void TextRenderer_SetClipWindow(int32_t x, int32_t y, int32_t w, int32_t h);

/** Query the current justify window. Any pointer may be NULL. */
void TextRenderer_GetJustifyWindow(int32_t* x, int32_t* y, int32_t* w);

/** Query the current clip window. Any pointer may be NULL. */
void TextRenderer_GetClipWindow(int32_t* x, int32_t* y, int32_t* w, int32_t* h);

/** Draw text at (posx, posy) relative to the text window with word-wrap.
 *  GPU backends queue the draw; call TextRenderer_Flush() to emit it. */
TbBool TextRenderer_DrawTextResized(int32_t posx, int32_t posy, int32_t units_per_px, const char* text);

/** Draw text at absolute screen coordinates. No window setup needed.
 *  GPU backends queue the draw; call TextRenderer_Flush() to emit it. */
TbBool TextRenderer_DrawTextAt(int32_t screen_x, int32_t screen_y, int32_t units_per_px, const char* text);

/** Flush all deferred text draws to the framebuffer.
 *  Must be called after the staging-buffer blit quad and before buffer swap. */
void TextRenderer_Flush(void);

/** Height of one line of text in the current font (unscaled). */
int32_t TextRenderer_LineHeight(void);

/** Width of a single character (unscaled). */
int32_t TextRenderer_CharWidth(uint32_t chr);

/** Width of a single character (scaled by units_per_px). */
int32_t TextRenderer_CharWidthScaled(uint32_t chr, int32_t units_per_px);

/** Width of a complete string (unscaled). */
int32_t TextRenderer_StringWidth(const char* text);

/** Width of a complete string (scaled by units_per_px). */
int32_t TextRenderer_StringWidthScaled(const char* text, int32_t units_per_px);

/** Width of the next word in a string (unscaled). */
int32_t TextRenderer_WordWidth(const char* str);

/** Width of the next word in a string (scaled by units_per_px). */
int32_t TextRenderer_WordWidthScaled(const char* str, int32_t units_per_px);

/** Height of a string (accounts for newlines, unscaled). */
int32_t TextRenderer_TextHeight(const char* text);

/** Height a string would occupy with word-wrap at the given scale. */
int32_t TextRenderer_StringHeight(int32_t units_per_px, const char* text);

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

/** Begin a world-depth-tested submission batch.  All UIRenderer_Submit* calls
 *  issued between this and UIRenderer_EndWorldDepth() are tagged with ndc_z
 *  ([-1,1]) and rendered with GL depth-test ON against the tile depth buffer,
 *  so non-spatial world elements (status flowers, room flags, etc.) are
 *  correctly occluded by walls in front of them.
 *  No-op when no renderer is active. */
void UIRenderer_BeginWorldDepth(float ndc_z);

/** End the world-depth-tested batch started by UIRenderer_BeginWorldDepth(). */
void UIRenderer_EndWorldDepth(void);

/** Begin a top-overlay batch: subsequent submissions are drawn dead-last,
 *  on top of all other UI and world-depth elements (depth test OFF).
 *  Use for cursor-driven affordances (slab selector) that must never be
 *  obscured by room flags, status flowers or any other world element. */
void UIRenderer_BeginTopOverlay(void);

/** End the top-overlay batch. */
void UIRenderer_EndTopOverlay(void);

/** Flush deferred keeper-hand / cursor sprites.  Call this AFTER
 *  TextRenderer_Flush() so the cursor always composites above all text. */
void UIRenderer_FlushHandSprites(void);

/** Submit a power-hand keeper sprite; deferred in OpenGL mode until after glClear(). */
void UIRenderer_SubmitKeeperSprite(short x, short y, unsigned short kspr_base,
                                   short kspr_angle, unsigned char sprgroup, long scale);

/** Submit a panel sprite (gui_panel_sprites) at screen-left alignment.
 *  Resolves player coloring for spridx and submits GPU quad; immediate in software mode. */
void UIRenderer_SubmitPanelSprite(int32_t x, int32_t y, int units_per_px, int32_t spridx);

/** Submit a panel sprite from a pre-resolved TbSprite pointer.
 *  Use when the caller has already called get_panel_sprite(); avoids a second lookup. */
struct TbSprite;
void UIRenderer_SubmitPanelSpriteRaw(int32_t x, int32_t y, int units_per_px, const struct TbSprite* spr);

/** Submit a panel sprite drawn entirely in a single flat colour (sprite used as discard mask).
 *  GPU: atlas R8 index used to discard transparent pixels; all opaque pixels output color_idx.
 *  CPU fallback: LbSpriteDrawResizedOneColour. */
void UIRenderer_SubmitPanelSpriteRawColored(int32_t x, int32_t y, int units_per_px, const struct TbSprite* spr, unsigned char color_idx);

/** Submit a 1-pixel-thick outline rectangle (border only, no fill).
 *  Decomposes into four thin UIRenderer_SubmitSolidBox strips.
 *  Replaces LbDrawBox with Lb_SPRITE_OUTLINE set. */
void UIRenderer_SubmitOutlineBox(int32_t x, int32_t y, int32_t w, int32_t h, unsigned char color_idx);

/** Route a palette-remapped panel/button sprite draw through the active UI renderer.
 *  The remap is performed by indexing pixmap.fade_tables[remap_row*256].
 *  GPU backends override with a fall-table shader; the base uses LbSpriteDrawResizedRemap. */
void UIRenderer_SubmitPanelSpriteRemap(int32_t x, int32_t y, int units_per_px, const struct TbSprite* spr, int remap_row);

/** Submit a panel sprite centered on (x, y). */
void UIRenderer_SubmitPanelSpriteCentered(int32_t x, int32_t y, int units_per_px, int32_t spridx);

/** Submit a button sprite (button_sprites) at screen-left alignment. */
void UIRenderer_SubmitButtonSprite(int32_t x, int32_t y, int units_per_px, short spridx);

/** Submit a button sprite horizontally flipped.
 *  In GPU mode: swaps U texture coordinates for a mirror effect.
 *  In software mode: temporarily sets Lb_SPRITE_FLIP_HORIZ before blitting. */
void UIRenderer_SubmitButtonSpriteFlipped(int32_t x, int32_t y, int units_per_px, short spridx);

/** Draw a decimal number using GBS_fontchars_number_dig0..9 button sprites.
 *  Digits are drawn right-to-left, horizontally centered on center_x.
 *  Each digit is scaled to w * h pixels.  Values <= 0 draw nothing.
 *  Used for floating gold text, HUD numbers, etc. */
void UIRenderer_SubmitDigitSprites(int32_t center_x, int32_t y, int32_t w, int32_t h, long long value);

/** Submit a sprite with explicit pixel dimensions to the GPU batch.
 *  Called by game-logic hooks (draw_status_sprites, draw_engine_number, etc.)
 *  instead of LbSpriteDrawScaled when the GPU renderer is active. */
void UIRenderer_SubmitScaledSprite(int32_t x, int32_t y, int32_t w, int32_t h, const struct TbSprite *spr);

/** Submit a solid-color rectangle to the GPU batch.
 *  Called instead of LbDrawBox when the GPU renderer is active.
 *  color_idx is a DK palette index (0-255). */
void UIRenderer_SubmitSolidBox(int32_t x, int32_t y, int32_t w, int32_t h, unsigned char color_idx);

/** Submit a semi-transparent solid-color rectangle.
 *  alpha=0.5 approximates Lb_SPRITE_TRANSPAR4 darkening toward the given color.
 *  GPU: solid shader with vertex alpha; CPU: GlassMap blend via Lb_SPRITE_TRANSPAR4. */
void UIRenderer_SubmitSolidBoxAlpha(int32_t x, int32_t y, int32_t w, int32_t h, unsigned char color_idx, float alpha);

/** Upload the 64×64 R8 gui_slab tile to the GPU for use by UIRenderer_SubmitSlabBackground.
 *  Call after gui_slab data is loaded (typically inside RendererNotifySpritesReloaded). */
void UIRenderer_SetSlabTexture(void);

/** Submit a tiled slab-background quad covering (x,y,w,h).
 *  GPU: queued as a back-layer (layer 0) palette-indexed quad with GL_REPEAT UV.
 *  @return 1 if the GPU path handled it and the caller must NOT write to WScreen;
 *          0 if the caller should fall through to the CPU path. */
TbBool UIRenderer_SubmitSlabBackground(int x, int y, int w, int h);

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
void UIRenderer_SubmitTiledSprite(int32_t x, int32_t y, int units_per_px, const struct TiledSprite* bigspr);

void UIRenderer_SetLayer(int layer);
void UIRenderer_FlushBack(void);
void UIRenderer_FlushFront(void);
void UIRenderer_Flush(void);
void UIRenderer_Clear(void);

/** Returns non-zero when the active UI renderer is GPU-accelerated.
 *  Use to skip CPU-only fallback paths (e.g. LbSpriteDrawResized with DrawFlags) 
 *  that are redundant when GPU sprites are already submitted. */
TbBool UIRenderer_IsGpuActive(void);

/** Apply a complete RendererSettings snapshot to the active renderer.
 *  Also copies *s into g_renderer_settings so subsequent reads are consistent.
 *  Safe to call from C translation units. */
void RendererApplySettings(const RendererSettings* s);

/** Return a pointer to the current renderer settings.
 *  The returned pointer is valid for the lifetime of the process. */
const RendererSettings* RendererGetSettings(void);

/******************************************************************************/
/* C-callable screen-tint overlay                                             */
/******************************************************************************/

/** Current screen-tint RGBA (each channel 0.0–1.0).  Alpha 0 disables the tint. *
 *  Set by palette-effect callbacks (possession, pain, dungeon-heart flash, etc.). */
extern float g_screen_tint[4];

/** Set the full-screen tint applied over all rendered content this frame.
 *  r/g/b/a are in [0,1].  Alpha 0 disables the tint entirely.
 *  No-op when no renderer is active. */
void RendererSetScreenTint(float r, float g, float b, float a);

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
